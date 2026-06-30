#include "WordProcessor.h"
#include "app/app.h"

//
#include "service/Editor/Editor.h"
#include "service/Tools/Tools.h"
#include "keyboard/keyboard.h"
#include "display/display.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"
#include <u8g2_fonts.h>

#ifdef USE_IME
#include "service/IME/IME.h"
#endif

extern const uint8_t u8g2_font_profont22_tf[];

//
int screen_width = 240;
int screen_height = 135;

//
const int font_width = 14;
const int font_height = 22;

// A CJK glyph is rendered double-width so it stays squared with the
// monospaced Latin font used for the body text.
const int cjk_width = font_width * 2;

// Lines will be rendered at the bottom on the screen
// need to calculate the Y position considering the status bar height
const int editY = 90;
const int cursorY = editY + font_height - 2;
const int cursorHeight = 2;

// Some flags
bool clear_background = true;
unsigned int last_sleep = millis();

static const lgfx::U8g2font g_profont22(u8g2_font_profont22_tf);

//
void WP_setup()
{
    // Editor Init - setup screen size
    Editor::getInstance().init(17, 4);

    // setup default color
    JsonDocument &app = status();

    // load file from the editor
    int file_index = app["config"]["file_index"].as<int>();
    String filename = format("/%d.txt", file_index);
    _log("[WP_setup] load file [%s]\n", filename.c_str());
    Editor::getInstance().loadFile(filename);

    //
    if (!app["config"]["foreground_color"].is<int>())
    {
        app["config"]["foreground_color"] = TFT_WHITE;
    }

    // start from clear background
    clear_background = true;

    // sleep timer reset
    last_sleep = millis();
}

//
void WP_render()
{
    // the editor swapped to a different window of the file - force a full redraw
    if (Editor::getInstance().pageChanged)
    {
        Editor::getInstance().pageChanged = false;
        clear_background = true;
    }

    // timers
    WP_check_saved();

    // CLEAR BACKGROUND
    WP_render_clear();

    // RENDER TEXT
    WP_render_text();

    // BLINK CURSOR
    WP_render_cursor();

    // STATUS
    WP_render_status();

    // CHINESE IME CANDIDATE BAR (drawn last so it sits on top)
    WP_render_ime();

    if (clear_background)
        clear_background = false;

    // Editor House Keeping Tasks
    Editor::getInstance().loop();
}

//
// Wubi IME candidate bar.
//
// While the user is composing a Wubi code, an overlay strip is drawn over the
// edit line showing the typed code and the numbered candidate hanzi. When the
// composition clears, the strip is wiped once so the underlying text reappears
// on the next frame.
void WP_render_ime()
{
#ifdef USE_IME
    JsonDocument &app = status();
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    IME &ime = IME::getInstance();

    static bool was_composing = false;
    bool composing = ime.active() && ime.composing();

    // The bar covers the edit line and the row just below it.
    const int barY = editY;
    const int barH = font_height + cursorHeight + 2;

    if (!composing)
    {
        // just stopped composing - clear the strip once and force a redraw of
        // the text underneath
        if (was_composing)
        {
            M5Cardputer.Display.fillRect(0, barY, screen_width, barH, background_color);
            clear_background = true;
            was_composing = false;
        }
        return;
    }
    was_composing = true;

    // background strip (inverted so it stands out from the document)
    M5Cardputer.Display.fillRect(0, barY, screen_width, barH, foreground_color);

    // typed Wubi code, drawn in the Latin body font
    M5Cardputer.Display.setFont(&g_profont22);
    M5Cardputer.Display.setTextColor(background_color, foreground_color);
    String code = ime.composition();
    M5Cardputer.Display.drawString(code, 2, barY);

    // candidates: "1字 2子 3自 ..." using the Chinese font
    int x = code.length() * font_width + 10;
    const std::vector<String> &cands = ime.candidates();
    for (size_t i = 0; i < cands.size(); i++)
    {
        // index digit
        M5Cardputer.Display.setFont(&g_profont22);
        M5Cardputer.Display.drawString(String((int)i + 1), x, barY);
        x += 9;

        // hanzi
        M5Cardputer.Display.setFont(&fonts::efontCN_16);
        M5Cardputer.Display.drawString(cands[i], x, barY + 2);
        x += 20;

        if (x > screen_width - 20)
            break;
    }

    // restore body font for subsequent renders
    M5Cardputer.Display.setFont(&g_profont22);
    M5Cardputer.Display.setTextColor(foreground_color, background_color);
#endif
}

//
void WP_render_text()
{
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // SET FONT
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(foreground_color, background_color);
    M5Cardputer.Display.setFont(&g_profont22);

    // Cursor Information
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int totalLine = Editor::getInstance().totalLine;

    //
    // Middle part of the text will be rendered
    // Only when refresh background is called
    //
    // initiate sprite
    if (clear_background)
    {
        // start line
        int rows = Editor::getInstance().rows;

        //
        int y = 0;
        for (int i = cursorLine - rows; i < cursorLine; i++)
        {
            if (i >= 0)
                WP_render_line(i, y);

            // new line
            y += font_height;
        }
    }
    else
    {
        //
        // Bottom line will the the edit area
        //
        WP_render_line(cursorLine, editY);
    }
}

//
// Advance in pixels of the glyph that starts at byte `line[i]`.
// ASCII / Latin-1 advance one cell; multi-byte UTF-8 (CJK) advance two.
static int WP_glyph_width(const char *line, int i)
{
    return utf8_char_len((uint8_t)line[i]) >= 2 ? cjk_width : font_width;
}

// Pixel X of the glyph boundary that sits `byteOffset` bytes into the line.
// Used by the cursor so it lands between glyphs, not between UTF-8 bytes.
int WP_line_width_bytes(const char *line, int byteOffset)
{
    int x = 0;
    int i = 0;
    while (i < byteOffset)
    {
        int len = utf8_char_len((uint8_t)line[i]);
        x += (len >= 2) ? cjk_width : font_width;
        i += len;
    }
    return x;
}

//
//
void WP_render_line(int line_num, int y)
{
    char *line = Editor::getInstance().linePositions[line_num];
    int length = Editor::getInstance().lineLengths[line_num];

    // render, walking the line one UTF-8 character at a time
    int x = 0;
    int i = 0;
    while (i < length)
    {
        uint8_t value = (uint8_t)line[i];

        // newline is a layout marker, never drawn
        if (value == '\n')
        {
            i += 1;
            continue;
        }

        int clen = utf8_char_len(value);

        // plain ASCII - keep the existing crisp monospaced glyph
        if (clen == 1 && value < 0x80)
        {
            M5Cardputer.Display.drawChar((char)value, x, y + font_height - 4);
            x += font_width;
            i += 1;
            continue;
        }

#ifdef USE_IME
        // a multi-byte UTF-8 run (CJK and friends): draw it with the
        // built-in Chinese font, then restore the body font. Only compiled for
        // the Chinese-enabled build so the plain Cardputer firmware does not
        // link the (large) CJK glyph data.
        if (clen >= 2 && i + clen <= length)
        {
            char glyph[5];
            memcpy(glyph, line + i, clen);
            glyph[clen] = '\0';

            M5Cardputer.Display.setFont(&fonts::efontCN_24);
            M5Cardputer.Display.drawString(glyph, x, y);
            M5Cardputer.Display.setFont(&g_profont22);

            x += cjk_width;
            i += clen;
            continue;
        }
#endif

        // stray high byte (e.g. legacy Latin-1 content) - best-effort
        String str = asciiToUnicode(value);
        if (str.length() == 0)
            M5Cardputer.Display.drawChar((char)value, x, y + font_height - 4);
        else
            M5Cardputer.Display.drawString(str, x, y);
        x += font_width;
        i += 1;
    }
}

//
// Render Cursor
void WP_render_cursor()
{
    JsonDocument &app = status();

    // retrieve color information
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // Cursor information. cursorLinePos is a BYTE offset within the line;
    // convert it to pixels through the UTF-8 aware width helper so the cursor
    // lands on a glyph boundary instead of inside a multi-byte character.
    static int cursorX_prev = 0;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;

    char *line = Editor::getInstance().linePositions[cursorLine];

    // Calculate Cursor X position
    int cursorX = 0;
    if (Editor::getInstance().buffer[cursorPos - 1] != '\n' && cursorLinePos != 0)
    {
        cursorX = WP_line_width_bytes(line, cursorLinePos);
    }

    // The underline width matches the glyph the cursor sits under, so a hanzi
    // gets a full double-width underline.
    int cursorW = font_width;
    if (cursorLinePos < Editor::getInstance().lineLengths[cursorLine])
        cursorW = WP_glyph_width(line, cursorLinePos);

    // Blink the cursor every 500 ms
    static bool blink = false;
    static unsigned int last = millis();
    if (millis() - last > 500)
    {
        last = millis();
        blink = !blink;
    }

    // Delete previous cursor line
    if (cursorX != cursorX_prev)
    {
        //
        M5Cardputer.Display.fillRect(cursorX_prev, cursorY, cjk_width, cursorHeight, background_color);

        //
        cursorX_prev = cursorX;
    }

    // Cursor Blink will be always at the bottom of the screen
    if (blink)
        M5Cardputer.Display.fillRect(cursorX, cursorY, cursorW, cursorHeight, foreground_color);
    else
        M5Cardputer.Display.fillRect(cursorX, cursorY, cursorW, cursorHeight, background_color);
}

//
void WP_render_status()
{
    //
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    int STATUSBAR_Y = M5Cardputer.Display.height() - 18;

    // file index number
    const int font_width = 12;
    int file_index = app["config"]["file_index"].as<int>();

    //
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
    M5Cardputer.Display.setTextColor(background_color, foreground_color);
    M5Cardputer.Display.drawString(String(file_index), 4, STATUSBAR_Y);

    // WORD COUNT
    M5Cardputer.Display.setTextColor(foreground_color, background_color);

    int wordCount = Editor::getInstance().wordCountFile + Editor::getInstance().wordCountBuffer;
    String wordCountFormatted = formatNumber(wordCount);
    M5Cardputer.Display.drawString(wordCountFormatted, 30, STATUSBAR_Y);

    // SAVE STATUS
    if (Editor::getInstance().saved)
    {
        M5Cardputer.Display.fillCircle(screen_width - 15, STATUSBAR_Y + 8, 5, TFT_GREEN);
    }
    else
    {
        M5Cardputer.Display.fillCircle(screen_width - 15, STATUSBAR_Y + 8, 5, TFT_RED);
    }
    M5Cardputer.Display.drawCircle(screen_width - 15, STATUSBAR_Y + 8, 5, TFT_BLACK);

    // BATTERY
    static int displayedBattery = -1;     // the value shown on screen
    static int lastReadBattery = -1;      // last raw value read
    static uint32_t changeDetectedAt = 0; // when the change was first noticed

    int current = M5Cardputer.Power.getBatteryLevel();

    // First run
    if (displayedBattery < 0)
    {
        displayedBattery = current;
        lastReadBattery = current;
    }

    // If battery reading has not changed, reset timer & do nothing
    if (current == displayedBattery)
    {
        changeDetectedAt = 0;
        lastReadBattery = current;
    }
    // Battery reading changed (ex: 100 → 99)
    else
    {
        // If a change was just detected, start the timer
        if (changeDetectedAt == 0)
        {
            changeDetectedAt = millis();
            lastReadBattery = current;
        }

        // If reading fluctuates (ex: 100 → 99 → 100), cancel
        if (current != lastReadBattery)
        {
            changeDetectedAt = millis(); // restart timer with new reading
            lastReadBattery = current;
        }

        // If a second passed with stable new value → update UI
        if (millis() - changeDetectedAt >= 1000)
        {
            displayedBattery = current;
            changeDetectedAt = 0;
        }
    }

    // Draw smoothed / stabilized value
    M5Cardputer.Display.drawString(
        format("%d%%", displayedBattery),
        screen_width - 85,
        STATUSBAR_Y);
}

//
// Clear Screen
// Do it as less as possible so that there is the least amount of the flicker
//
void WP_render_clear()
{
    //
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    //
    static int cursorLine_prev = 0;
    static int cursorPos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int cursorLineLength = Editor::getInstance().lineLengths[cursorLine];

    //
    static int bufferSize_prev = 0;
    int bufferSize = Editor::getInstance().getBufferSize();

    // When new line clear everything
    if (cursorLine_prev != cursorLine)
    {
        //
        clear_background = true;

        //
        cursorLine_prev = cursorLine;
    }

    // When Backspace, trailing characters should be deleted
    // if it is backspace or del key
    if (cursorPos_prev >= cursorPos && bufferSize_prev != bufferSize)
        clear_background = true;

    if (cursorPos_prev != cursorPos)
    {
        // if it is typing at the end don't flicker
        if (cursorPos != bufferSize)
        {
            // if the edit line is empty then don't flicker
            //
            if (cursorLinePos + 1 != cursorLineLength)
                clear_background = true;
        }

        //
        cursorPos_prev = cursorPos;
    }

    if (bufferSize != bufferSize_prev)
    {
        bufferSize_prev = bufferSize;
    }

    // clear background
    if (clear_background)
    {
        // when clearing background
        M5Cardputer.Display.fillRect(
            0,
            0,
            M5Cardputer.Display.width(),
            M5Cardputer.Display.height(),
            background_color);
    }
}

//
//
void WP_keyboard(int key, bool pressed, int index)
{
    // ignore non pritable keys
    if (key == 0)
        return;

    JsonDocument &app = status();
    _debug("WP_keyboard key: %d, pressed: %d\n", key, pressed);

    if (key == 27 || key == 6)
    {
        if (!pressed)
        {
            // Save before transitioning to the menu
            Editor::getInstance().saveFile();

            // open menu
            _debug("WP_keyboard - Received ESC Key\n");
            app["screen"] = MENUSCREEN;
        }

        // ESC button is ignored
        return;
    }

    // Check if File Change request is pressed
    if (key >= 1000 && key <= 1010)
    {
        if (!pressed)
        {
            int fileIndex = key - 1000;
            _log("File Change Requested: %d\n", fileIndex);

            //
            Editor::getInstance().saveFile();

            // save config
            app["config"]["file_index"] = fileIndex;
            config_save();

            // load new file
            Editor::getInstance().loadFile(format("/%d.txt", fileIndex));
        }
    }

    else
    {
        // send the keys to the editor
        Editor::getInstance().keyboard(key, pressed);
    }
}

//
// Check if text is saved
void WP_check_saved()
{
    //
    static unsigned int last = millis();
    static int lastBufferSize = Editor::getInstance().getBufferSize();
    int bufferSize = Editor::getInstance().getBufferSize();

    //
    // when the file is saved then extend the autosave timer
    if (lastBufferSize != bufferSize)
    {
        last = millis();

        //
        lastBufferSize = bufferSize;
    }

    //
    // when idle for 3 seconds then auto save
    if (millis() - last > 3000)
    {
        //
        last = millis();

        if (!Editor::getInstance().saved)
            Editor::getInstance().saveFile();
    }
}
