#pragma once

#include <Arduino.h>

void keyboard_setup();
void keyboard_loop();

bool keyboard_capslock();
void keyboard_capslock_toggle();

// Chinese (Wubi) IME gateway. Every ASCII key from any keyboard (internal
// matrix or external USB host) is offered to this filter before reaching the
// editor. Returns true when the IME consumed the key, in which case the caller
// must NOT forward it. Committed hanzi are emitted from inside the filter.
// When USE_IME is not defined this always returns false.
bool keyboard_ime_filter(int key, bool pressed);

//
void keyboard_config_load(
    String filename,
    int *layers,
    int size,
    const char *keys[],
    int keyCount);
int keyboard_convert_HID(String _hid);
void keyboard_HID2Ascii(uint8_t keycode, uint8_t modifier, bool pressed);