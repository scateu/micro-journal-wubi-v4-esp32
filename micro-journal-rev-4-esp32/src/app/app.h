#pragma once

// app version
#define VERSION "2.0.0.0617"

// default utility headers
#include <ArduinoJson.h>
#include "Log/Log.h"
#include "FileSystem/FileSystem.h"
#include "Config/Config.h"
#include "Verification/Verification.h"
#include "service/Tools/Tools.h"
#include <HardwareSerial.h>

// 
void app_setup();
void app_loop();

// is app ready?
bool app_ready();

// True when the device booted into USB-drive (export) mode ('e' held at
// power-on). In this mode the SD is owned by the USB host, so the normal
// editor + USB-host keyboard must NOT be started.
bool usbdrive_mode();

// app status
JsonDocument &status();

// ESP32 has SD or SPIFFS 
// RP2040 has LittleFS
// This is a pattern to hide the implementation of the file system
// and provide a common interface to access the file system
FileSystem *gfs();

