#pragma once

#include <Arduino.h>

//
void WP_setup();

// 
void WP_render();

//
void WP_check_saved();

//
void WP_render_clear();

//
void WP_render_status();

//
void WP_render_text();
void WP_render_line(int line_num, int y);

// Pixel X of the glyph boundary `byteOffset` bytes into a rendered line.
int WP_line_width_bytes(const char *line, int byteOffset);

//
void WP_render_cursor();

// Wubi IME candidate bar overlay (no-op unless USE_IME).
void WP_render_ime();

// 
void WP_keyboard(int key, bool pressed, int index);


