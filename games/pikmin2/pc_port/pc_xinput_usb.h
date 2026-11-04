#ifndef PC_XINPUT_USB_H
#define PC_XINPUT_USB_H

/* Mandos con protocolo Xbox 360 (XInput) por USB en macOS.
   macOS no trae driver para ellos: no aparecen como dispositivo HID y SDL no
   los ve. Con libusb se abre el mando directamente y se presenta a SDL como un
   mando virtual, de modo que el resto del código lo trata como cualquier otro.
   Solo con -DPIKMIN_XINPUT_USB=ON (necesita libusb). */

/// Una vez, tras SDL_Init y antes de enumerar los mandos.
void pc_xinput_usb_init(void);
/// Hilo principal, antes de recoger eventos SDL: recoge y reconecta un mando.
void pc_xinput_usb_update(void);
void pc_xinput_usb_shutdown(void);

#endif
