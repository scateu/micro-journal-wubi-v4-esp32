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

extern const uint8_t u8g2_font_profont12_tf[];

//
int screen_width = 240;
int screen_height = 135;

// Body font is profont12 (half of the former profont22). profont12 glyphs are
// 6px wide / 12px tall; we advance 7px per Latin cell for a little tracking.
const int font_width = 7;
const int font_height = 13;

// Baseline drop from the top of a cell to where drawChar expects the origin
// (profont12 has ascent 8, descent 2).
const int font_baseline = 10;

// A CJK glyph is rendered double-width so it stays squared with the
// monospaced Latin font used for the body text.
const int cjk_width = font_width * 2;

// Lines will be rendered at the bottom on the screen
// need to calculate the Y position considering the status bar height
const int editY = 96;
const int cursorY = editY + font_height - 1;
const int cursorHeight = 2;

// Some flags
bool clear_background = true;
// When true, only the current edit line's strip is repainted (no full-screen
// wipe). This is the common typing case and avoids the whole-screen flash.
bool clear_editline = false;
unsigned int last_sleep = millis();

static const lgfx::U8g2font g_profont22(u8g2_font_profont12_tf);

//
void WP_setup()
{
    // Editor Init - setup screen size.
    // cols is measured in display columns (Latin = 1 col of font_width px, a
    // CJK glyph = 2 cols). 34 cols * 7px = 238px fits the 240px width, whether
    // the line is 34 Latin chars or 17 hanzi. rows is how many lines fit above
    // the edit line: editY(96) / font_height(13) ~= 7.
    Editor::getInstance().init(34, 7);

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
    const int barH = font_height + cursorHeight + 3;

    if (!composing)
    {
        // Just stopped composing (candidate committed or cancelled). Clear only
        // the bar strip and repaint the edit line underneath it - a full-screen
        // clear here caused a visible flash on every committed hanzi.
        if (was_composing)
        {
            M5Cardputer.Display.fillRect(0, barY, screen_width, barH, background_color);

            M5Cardputer.Display.setFont(&g_profont22);
            M5Cardputer.Display.setTextColor(foreground_color, background_color);
            WP_render_line(Editor::getInstance().cursorLine, editY);

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
    int x = code.length() * font_width + 8;
    const std::vector<String> &cands = ime.candidates();
    for (size_t i = 0; i < cands.size(); i++)
    {
        // index digit
        M5Cardputer.Display.setFont(&g_profont22);
        M5Cardputer.Display.drawString(String((int)i + 1), x, barY + 1);
        x += font_width;

        // hanzi
        M5Cardputer.Display.setFont(&fonts::efontCN_12);
        M5Cardputer.Display.drawString(cands[i], x, barY + 1);
        x += 14;

        if (x > screen_width - 16)
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

        // the edit line itself
        WP_render_line(cursorLine, editY);
    }
    else if (clear_editline)
    {
        // Only the edit line changed - repaint just that line. Doing this only
        // on an actual change (not every idle frame) stops the shimmer.
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
            M5Cardputer.Display.drawChar((char)value, x, y + font_baseline);
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

            M5Cardputer.Display.setFont(&fonts::efontCN_12);
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
            M5Cardputer.Display.drawChar((char)value, x, y + font_baseline);
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

    // Only repaint each status element when its value changes (or on a full
    // redraw). Repainting the whole bar every 150 ms frame was a flicker source.
    static int fileIndex_prev = -1;
    static int wordCount_prev = -1;

    // file index number
    int file_index = app["config"]["file_index"].as<int>();
    if (file_index != fileIndex_prev || clear_background)
    {
        fileIndex_prev = file_index;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(background_color, foreground_color);
        M5Cardputer.Display.fillRect(4, STATUSBAR_Y, 24, 16, foreground_color);
        M5Cardputer.Display.drawString(String(file_index), 4, STATUSBAR_Y);
    }

    // WORD COUNT
    int wordCount = Editor::getInstance().wordCountFile + Editor::getInstance().wordCountBuffer;
    if (wordCount != wordCount_prev || clear_background)
    {
        wordCount_prev = wordCount;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(foreground_color, background_color);
        M5Cardputer.Display.fillRect(30, STATUSBAR_Y, 60, 16, background_color);
        M5Cardputer.Display.drawString(formatNumber(wordCount), 30, STATUSBAR_Y);
    }

#ifdef USE_IME
    // INPUT-MODE INDICATOR: [五] when the Wubi IME is on, [En] otherwise.
    // Sits between the word count and the battery %. Only repainted when the
    // mode actually changes (or on a full redraw) so it never flickers.
    {
        const int IME_X = 95;
        static int ime_prev = -1;
        int ime_now = IME::getInstance().active() ? 1 : 0;

        if (ime_now != ime_prev || clear_background)
        {
            ime_prev = ime_now;

            // clear the cell first so the previous label doesn't bleed through
            M5Cardputer.Display.fillRect(IME_X, STATUSBAR_Y, 34, 16, background_color);
            M5Cardputer.Display.setTextColor(foreground_color, background_color);

            if (ime_now)
            {
                // [五] - Chinese font for the glyph, ASCII font for the brackets
                M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
                M5Cardputer.Display.drawString("[", IME_X, STATUSBAR_Y);
                M5Cardputer.Display.setFont(&fonts::efontCN_12);
                M5Cardputer.Display.drawString("五", IME_X + 8, STATUSBAR_Y + 1);
                M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
                M5Cardputer.Display.drawString("]", IME_X + 22, STATUSBAR_Y);
            }
            else
            {
                M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
                M5Cardputer.Display.drawString("[En]", IME_X, STATUSBAR_Y);
            }
        }
    }
#endif

    // SAVE STATUS
    static int saved_prev = -1;
    int saved_now = Editor::getInstance().saved ? 1 : 0;
    if (saved_now != saved_prev || clear_background)
    {
        saved_prev = saved_now;
        M5Cardputer.Display.fillCircle(screen_width - 15, STATUSBAR_Y + 8, 5,
                                       saved_now ? TFT_GREEN : TFT_RED);
        M5Cardputer.Display.drawCircle(screen_width - 15, STATUSBAR_Y + 8, 5, TFT_BLACK);
    }

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

    // Draw smoothed / stabilized value, only when it changes
    static int battery_prev = -999;
    if (displayedBattery != battery_prev || clear_background)
    {
        battery_prev = displayedBattery;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(foreground_color, background_color);
        M5Cardputer.Display.fillRect(screen_width - 85, STATUSBAR_Y, 40, 16, background_color);
        M5Cardputer.Display.drawString(
            format("%d%%", displayedBattery),
            screen_width - 85,
            STATUSBAR_Y);
    }
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

    //
    static int bufferSize_prev = 0;
    int bufferSize = Editor::getInstance().getBufferSize();

    // start each frame assuming nothing needs erasing
    clear_editline = false;

    // A change of line (new line, word-wrap onto a new row, arrow to another
    // row, paging) needs the whole text area redrawn.
    if (cursorLine_prev != cursorLine)
    {
        clear_background = true;
        cursorLine_prev = cursorLine;
    }

    // Any edit or cursor move that stays on the same line only needs that one
    // line repainted. Repainting a single strip - instead of wiping the whole
    // screen every keystroke - is what removes the typing flash.
    else if (cursorPos_prev != cursorPos || bufferSize_prev != bufferSize)
    {
        clear_editline = true;
    }

    cursorPos_prev = cursorPos;
    bufferSize_prev = bufferSize;

    // FULL CLEAR
    if (clear_background)
    {
        M5Cardputer.Display.fillRect(
            0,
            0,
            M5Cardputer.Display.width(),
            M5Cardputer.Display.height(),
            background_color);
    }
    // EDIT-LINE CLEAR: wipe just the strip the edit line occupies (including
    // the cursor underline row) so stale glyphs don't linger after a backspace.
    else if (clear_editline)
    {
        M5Cardputer.Display.fillRect(
            0,
            editY,
            M5Cardputer.Display.width(),
            font_height + cursorHeight + 1,
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
