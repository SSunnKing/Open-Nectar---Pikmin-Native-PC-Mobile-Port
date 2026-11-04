// Real SDL virtual controllers; only the physical USB transport is simulated.
#include <SDL2/SDL.h>
#include <libusb.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pc_xinput_usb.h"

struct libusb_context {};
struct libusb_device { int index; };
struct libusb_device_handle { int index; };
namespace {
libusb_context context;
libusb_device devices[2] = {{0}, {1}};
libusb_device_handle handles[2] = {{0}, {1}};
std::atomic<int> attached {0};
std::atomic<int> rumbleLow {0}, rumbleHigh {0};
int contexts = 0, opened = 0, claimed = 0;
int openError = 0, claimError = 0;
bool failThread = false;
int baseJoysticks = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #condition, SDL_GetError()); \
    std::exit(1); } } while (false)

template<class Predicate> void waitFor(Predicate predicate)
{
    const Uint32 start = SDL_GetTicks();
    do {
        pc_xinput_usb_update();
        SDL_JoystickUpdate();
        SDL_PumpEvents();
        if (predicate()) return;
        SDL_Delay(5);
    } while (SDL_GetTicks() - start < 2500);
    CHECK(predicate());
}
void checkClean()
{
    CHECK(contexts == 0);
    CHECK(opened == 0);
    CHECK(claimed == 0);
    CHECK(SDL_NumJoysticks() == baseJoysticks);
}
} // namespace

extern "C" {
int libusb_init(libusb_context** ctx) { *ctx = &context; ++contexts; return 0; }
void libusb_exit(libusb_context*) { --contexts; }
const char* libusb_error_name(int) { return "simulated USB result"; }
ssize_t libusb_get_device_list(libusb_context*, libusb_device*** result)
{
    static libusb_device* list[] = {&devices[0], &devices[1], nullptr};
    *result = list;
    return attached;
}
void libusb_free_device_list(libusb_device**, int) {}
int libusb_get_device_descriptor(libusb_device* dev, libusb_device_descriptor* result)
{
    *result = {};
    result->idVendor = 0x146b;
    result->idProduct = 0x1234 + dev->index;
    return 0;
}
int libusb_get_config_descriptor(libusb_device*, uint8_t, libusb_config_descriptor** result)
{
    static libusb_endpoint_descriptor endpoints[2] = {};
    endpoints[0].bEndpointAddress = 0x81;
    endpoints[1].bEndpointAddress = 0x02;
    endpoints[0].bmAttributes = endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    static libusb_interface_descriptor descriptor = {};
    descriptor.bInterfaceClass = 0xff;
    descriptor.bInterfaceSubClass = 0x5d;
    descriptor.bInterfaceProtocol = 0x01;
    descriptor.bNumEndpoints = 2;
    descriptor.endpoint = endpoints;
    static libusb_interface interface = {&descriptor, 1};
    static libusb_config_descriptor config = {};
    config.bConfigurationValue = 1;
    config.bNumInterfaces = 1;
    config.interface = &interface;
    *result = &config;
    return 0;
}
void libusb_free_config_descriptor(libusb_config_descriptor*) {}
int libusb_get_configuration(libusb_device_handle*, int* config) { *config = 0; return 0; }
int libusb_set_configuration(libusb_device_handle*, int) { return 0; }
int libusb_open(libusb_device* dev, libusb_device_handle** handle)
{
    if (openError) return openError;
    *handle = &handles[dev->index];
    ++opened;
    return 0;
}
void libusb_close(libusb_device_handle*) { --opened; }
int libusb_claim_interface(libusb_device_handle*, int)
{
    if (claimError) return claimError;
    ++claimed;
    return 0;
}
int libusb_release_interface(libusb_device_handle*, int) { --claimed; return 0; }
int libusb_interrupt_transfer(libusb_device_handle*, unsigned char endpoint, unsigned char* data,
                              int length, int* transferred, unsigned int)
{
    if (!(endpoint & 0x80)) {
        CHECK(length == 8);
        rumbleLow = data[3];
        rumbleHigh = data[4];
        *transferred = length;
        return 0;
    }
    SDL_Delay(5);
    if (!attached) return LIBUSB_ERROR_NO_DEVICE;
    std::memset(data, 0, length);
    data[1] = 20;
    data[3] = 0x10; // A held
    data[4] = 255; // left trigger fully pressed
    data[6] = 0x34; data[7] = 0x12;
    data[9] = 0x80; // -32768 Y must saturate when flipped
    *transferred = 20;
    return 0;
}
// Exercise worker creation failure while using SDL's real threads otherwise.
SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data)
{
    return failThread ? nullptr : SDL_CreateThreadWithStackSize(fn, name, 0, data);
}
} // extern "C"

int main()
{
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    baseJoysticks = SDL_NumJoysticks();
    pc_xinput_usb_init();
    CHECK(SDL_NumJoysticks() == baseJoysticks);
    attached = 2;
    waitFor([] { return SDL_NumJoysticks() == baseJoysticks + 1; });
    CHECK(opened == 1); // Existing single-controller policy, even with two devices.
    SDL_GameController* pad = SDL_GameControllerOpen(SDL_NumJoysticks() - 1);
    CHECK(pad);
    waitFor([&] { return SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0; });
    CHECK(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) == 0x1234);
    CHECK(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) == 32767);
    CHECK(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 32767);
    CHECK(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    CHECK(SDL_GameControllerRumble(pad, 0xab00, 0xcd00, 10000) == 0);
    CHECK(rumbleLow == 0xab && rumbleHigh == 0xcd);
    attached = 0;
    waitFor([] { return SDL_NumJoysticks() == baseJoysticks; });
    CHECK(!SDL_GameControllerGetAttached(pad));
    SDL_GameControllerClose(pad);
    CHECK(opened == 0 && claimed == 0);
    attached = 1;
    waitFor([] { return SDL_NumJoysticks() == baseJoysticks + 1; });
    pc_xinput_usb_shutdown();
    CHECK(rumbleLow == 0 && rumbleHigh == 0);
    checkClean();
    pc_xinput_usb_shutdown();
    checkClean();

    for (int failure = 0; failure < 3; ++failure) {
        openError = failure == 0 ? LIBUSB_ERROR_ACCESS : 0;
        claimError = failure == 1 ? LIBUSB_ERROR_BUSY : 0;
        failThread = failure == 2;
        pc_xinput_usb_init();
        CHECK(SDL_NumJoysticks() == baseJoysticks);
        CHECK(opened == 0 && claimed == 0);
        pc_xinput_usb_shutdown();
        checkClean();
    }
    SDL_Quit();
    std::puts("USB late connection, input, rumble, reconnect, shutdown and failure cleanup passed");
}
