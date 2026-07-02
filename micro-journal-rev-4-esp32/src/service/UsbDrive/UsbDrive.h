#pragma once

//
// USB Drive (export) mode: expose the SD / TF card to a host PC over USB Mass
// Storage so journals can be copied off. Entered by holding 'e' at power-on
// (see app_setup); the normal editor + USB-host keyboard are NOT started in
// this mode, so the firmware never touches the SD while the host owns it.
//
// This is SD-backed and intentionally SEPARATE from the flash-based USE_MSC
// path (MassStorage/esp32), which exposes an internal flash partition instead.
//
#if defined(USE_USB_DRIVE) && defined(BOARD_ESP32_S3)

#include <Arduino.h>

// Mount the SD as a USB MSC device (raw sectors). Returns false if the SD
// card is missing / unreadable, in which case the caller shows an error.
bool usbdrive_begin();

// Poll USB / connection state; keeps app["usbDriveStatus"] up to date for the
// status screen. Cheap; call from the main loop.
void usbdrive_loop();

#endif
