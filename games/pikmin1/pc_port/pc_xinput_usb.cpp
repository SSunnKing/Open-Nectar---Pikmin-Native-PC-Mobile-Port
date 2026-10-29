#include "pc_xinput_usb.h"

#if defined(PIKI_XINPUT_USB)

#include <SDL2/SDL.h>
#include <libusb.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {
constexpr uint16_t kNaconVendor = 0x146b;

libusb_context*       sCtx    = nullptr;
libusb_device_handle* sHandle = nullptr;
int                   sIface  = -1;
uint8_t               sEp     = 0;
SDL_Joystick*         sJoy    = nullptr;
SDL_JoystickID        sJoyId  = -1;
std::atomic<bool>     sRun { false };
std::thread           sThread;

// Interfaz Xbox 360 (clase 0xFF, subclase 0x5D, protocolo 0x01) y su
// endpoint de interrupción de entrada. Los mandos de Nacon/Bigben se aceptan
// con cualquier interfaz de clase 0xFF por si no declaran la subclase.
bool findInterface(libusb_device* dev, const libusb_device_descriptor& dd, int& iface, uint8_t& ep)
{
    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0 || !cfg) return false;
    bool found = false;
    for (int round = 0; round < 2 && !found; round++) {
        for (int i = 0; i < cfg->bNumInterfaces && !found; i++) {
            const libusb_interface& itf = cfg->interface[i];
            for (int a = 0; a < itf.num_altsetting && !found; a++) {
                const libusb_interface_descriptor& d = itf.altsetting[a];
                const bool exact = d.bInterfaceClass == 0xFF && d.bInterfaceSubClass == 0x5D && d.bInterfaceProtocol == 0x01;
                const bool loose = d.bInterfaceClass == 0xFF && dd.idVendor == kNaconVendor;
                if (!(round == 0 ? exact : loose)) continue;
                for (int e = 0; e < d.bNumEndpoints; e++) {
                    const libusb_endpoint_descriptor& ed = d.endpoint[e];
                    if ((ed.bEndpointAddress & 0x80) && (ed.bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
                        iface = d.bInterfaceNumber;
                        ep    = ed.bEndpointAddress;
                        found = true;
                        break;
                    }
                }
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

int16_t flipY(int16_t v) { return v == -32768 ? 32767 : static_cast<int16_t>(-v); }
int16_t trigger(uint8_t v) { return static_cast<int16_t>(v * 257 - 32768); }

// Informe XInput de 20 bytes: [0]=0x00 [1]=0x14, [2..3] botones, [4..5]
// gatillos, [6..13] sticks (int16 LE, Y hacia arriba positivo).
void apply(const unsigned char* b)
{
    const unsigned lo = b[2], hi = b[3];
    SDL_JoystickSetVirtualButton(sJoy, 0, (hi & 0x10) != 0);  // A
    SDL_JoystickSetVirtualButton(sJoy, 1, (hi & 0x20) != 0);  // B
    SDL_JoystickSetVirtualButton(sJoy, 2, (hi & 0x40) != 0);  // X
    SDL_JoystickSetVirtualButton(sJoy, 3, (hi & 0x80) != 0);  // Y
    SDL_JoystickSetVirtualButton(sJoy, 4, (lo & 0x20) != 0);  // Back
    SDL_JoystickSetVirtualButton(sJoy, 5, (hi & 0x04) != 0);  // Guide
    SDL_JoystickSetVirtualButton(sJoy, 6, (lo & 0x10) != 0);  // Start
    SDL_JoystickSetVirtualButton(sJoy, 7, (lo & 0x40) != 0);  // L3
    SDL_JoystickSetVirtualButton(sJoy, 8, (lo & 0x80) != 0);  // R3
    SDL_JoystickSetVirtualButton(sJoy, 9, (hi & 0x01) != 0);  // LB
    SDL_JoystickSetVirtualButton(sJoy, 10, (hi & 0x02) != 0); // RB
    SDL_JoystickSetVirtualButton(sJoy, 11, (lo & 0x01) != 0); // D-pad arriba
    SDL_JoystickSetVirtualButton(sJoy, 12, (lo & 0x02) != 0); // abajo
    SDL_JoystickSetVirtualButton(sJoy, 13, (lo & 0x04) != 0); // izquierda
    SDL_JoystickSetVirtualButton(sJoy, 14, (lo & 0x08) != 0); // derecha
    auto s16 = [&](int at) { return static_cast<int16_t>(b[at] | (b[at + 1] << 8)); };
    SDL_JoystickSetVirtualAxis(sJoy, 0, s16(6));
    SDL_JoystickSetVirtualAxis(sJoy, 1, flipY(s16(8)));
    SDL_JoystickSetVirtualAxis(sJoy, 2, s16(10));
    SDL_JoystickSetVirtualAxis(sJoy, 3, flipY(s16(12)));
    SDL_JoystickSetVirtualAxis(sJoy, 4, trigger(b[4]));
    SDL_JoystickSetVirtualAxis(sJoy, 5, trigger(b[5]));
}

void readLoop()
{
    unsigned char buf[64];
    while (sRun) {
        int n = 0;
        const int r = libusb_interrupt_transfer(sHandle, sEp, buf, sizeof buf, &n, 100);
        if (r == LIBUSB_ERROR_TIMEOUT) continue;
        if (r != 0) {
            printf("[PC Port] USB controller read stopped: %s\n", libusb_error_name(r));
            fflush(stdout);
            break;
        }
        if (n >= 14 && buf[0] == 0x00) apply(buf);
    }
    // Mando desconectado o cierre del juego: se retira el mando virtual.
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == sJoyId) {
            SDL_JoystickDetachVirtual(i);
            break;
        }
    }
}
} // namespace

void pc_xinput_usb_init(void)
{
    if (libusb_init(&sCtx) != 0) {
        printf("[PC Port] USB XInput: libusb_init failed\n");
        fflush(stdout);
        return;
    }
    printf("[PC Port] USB XInput: scanning for Xbox 360-protocol controllers\n");
    fflush(stdout);
    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(sCtx, &list);
    libusb_device_descriptor chosen = {};
    for (ssize_t i = 0; i < count && !sHandle; i++) {
        libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) != 0) continue;
        int iface;
        uint8_t ep;
        if (!findInterface(list[i], dd, iface, ep)) {
            if (dd.idVendor == kNaconVendor) {
                // El mando de Nacon está, pero ninguna interfaz encaja: volcar
                // las que tiene para poder ajustar la detección.
                libusb_config_descriptor* cfg = nullptr;
                const int cr = libusb_get_active_config_descriptor(list[i], &cfg);
                printf("[PC Port] USB %04x:%04x: no usable XInput interface (config: %s)\n", dd.idVendor, dd.idProduct,
                       cr == 0 ? "ok" : libusb_error_name(cr));
                if (cr == 0 && cfg) {
                    for (int k = 0; k < cfg->bNumInterfaces; k++)
                        for (int a = 0; a < cfg->interface[k].num_altsetting; a++) {
                            const libusb_interface_descriptor& d = cfg->interface[k].altsetting[a];
                            printf("[PC Port]   interface %d: class %02x subclass %02x protocol %02x endpoints %d\n",
                                   d.bInterfaceNumber, d.bInterfaceClass, d.bInterfaceSubClass, d.bInterfaceProtocol,
                                   d.bNumEndpoints);
                        }
                    libusb_free_config_descriptor(cfg);
                }
                fflush(stdout);
            }
            continue;
        }
        libusb_device_handle* h = nullptr;
        int r = libusb_open(list[i], &h);
        if (r == 0) r = libusb_claim_interface(h, iface);
        if (r != 0) {
            printf("[PC Port] USB controller %04x:%04x found but not usable: %s\n", dd.idVendor, dd.idProduct,
                   libusb_error_name(r));
            fflush(stdout);
            if (h) libusb_close(h);
            continue;
        }
        sHandle = h;
        sIface  = iface;
        sEp     = ep;
        chosen  = dd;
    }
    libusb_free_device_list(list, 1);
    if (!sHandle) {
        printf("[PC Port] USB XInput: no controller attached\n");
        fflush(stdout);
        return;
    }

    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version    = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type       = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes      = 6;
    desc.nbuttons   = 15;
    desc.vendor_id  = chosen.idVendor;
    desc.product_id = chosen.idProduct;
    desc.name       = "USB XInput Controller";
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    if (index < 0) {
        printf("[PC Port] Could not create the virtual controller: %s\n", SDL_GetError());
        fflush(stdout);
        libusb_release_interface(sHandle, sIface);
        libusb_close(sHandle);
        sHandle = nullptr;
        return;
    }
    char guid[40] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof guid);
    char mapping[512];
    snprintf(mapping, sizeof mapping,
             "%s,USB XInput Controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,"
             "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,platform:Mac OS X",
             guid);
    SDL_GameControllerAddMapping(mapping);
    sJoyId = SDL_JoystickGetDeviceInstanceID(index);
    sJoy   = SDL_JoystickOpen(index);
    if (!sJoy) return;
    printf("[PC Port] USB XInput controller %04x:%04x attached as a virtual SDL controller\n", chosen.idVendor,
           chosen.idProduct);
    fflush(stdout);
    sRun    = true;
    sThread = std::thread(readLoop);
}

void pc_xinput_usb_shutdown(void)
{
    if (sRun.exchange(false) && sThread.joinable()) sThread.join();
    if (sHandle) {
        libusb_release_interface(sHandle, sIface);
        libusb_close(sHandle);
        sHandle = nullptr;
    }
    if (sCtx) {
        libusb_exit(sCtx);
        sCtx = nullptr;
    }
}

#else
#include <cstdio>
void pc_xinput_usb_init(void)
{
    printf("[PC Port] USB XInput: not compiled in (build with -DPIKMIN_XINPUT_USB=ON and libusb)\n");
    fflush(stdout);
}
void pc_xinput_usb_shutdown(void) {}
#endif
