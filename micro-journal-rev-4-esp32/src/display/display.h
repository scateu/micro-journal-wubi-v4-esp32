#pragma once

//
#include <Arduino.h>

// Screen Type
#define WORDPROCESSOR 0

//
#define ERRORSCREEN 1
#define MENUSCREEN 2
#define WAKEUPSCREEN 3
#define SLEEPSCREEN 4
#define KEYBOARDSCREEN 5
#define UPDATESCREEN 6

// menu id
#define MENU_HOME 0
#define MENU_SYNC 1
#define MENU_CLEAR 2
#define MENU_LAYOUT 3
#define MENU_WIFI 4
#define MENU_FIRMWARE 5
#define MENU_BUTTONS 7
#define MENU_BACKGROUND 8
#define MENU_FONTCOLOR 9
#define MENU_BLUETOOTH 11
#define MENU_STORAGE 12
#define MENU_BRIGHTNESS 13
#define MENU_INFO 14
#define MENU_LANGUAGE 15

// MENU button
#define FN 28

// special key
#define EMPTY 0x0
#define MENU 0x6

// Editor action key codes (Emacs / readline style shortcuts). These are
// synthesised by the keyboard layer from Ctrl/Meta combos and handled in
// Editor::keyboard() / WP_keyboard(). Values are chosen from control codes not
// already used for cursor motion (2,3,18-23), backspace(8), enter(10), ESC(27),
// MENU(6) or DEL(127).
//   Cursor motion reuses existing codes: C-a=Home(2) C-e=End(3)
//   C-b=Left(18) C-f=Right(19) C-p=Up(20) C-n=Down(21)
#define KEY_SAVE 0x07          // C-s : save in place
#define KEY_WORD_FWD 0x0E      // M-f : move forward one word
#define KEY_WORD_BACK 0x0F     // M-b : move backward one word
#define KEY_KILL_WORD_FWD 0x10 // M-d : kill word forward (into kill buffer)
#define KEY_YANK 0x11          // C-y : yank (paste kill buffer)
#define KEY_KILL_LINE 0x0B     // C-k : kill to end of line (into kill buffer)

//
void display_setup();
void display_loop();
int display_core(); // show which core to run display routine

//
void display_keyboard(int key, bool pressed, int index = -1);
void display_keyboard_report(uint8_t modifier, uint8_t reserved, uint8_t *keycodes);
