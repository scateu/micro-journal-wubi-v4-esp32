#include "Editor.h"
#include "app/app.h"
#include "display/display.h"

//
#include "service/WordCounter/WordCounter.h"
#include "service/Tools/Tools.h"

#ifdef USE_IME
#include "service/IME/IME.h"
#endif

//
// EDITOR CLASS IMPLEMENTATION
//

//
// Editor Initialization with column and row setup
void Editor::init(int cols, int rows)
{
    // Define Screen Size
    this->cols = cols;
    this->rows = rows;

    //
    resetBuffer();
    updateScreen();

    //
    savingInProgress = false;

    // You can add any additional setup logic here
    _log("Editor initialized with columns: %d, rows: %d\n", cols, rows);
}

// Given the fileName, go through the loading process
// initialize FileBuffer and ScreenBuffer
void Editor::loadFile(String fileName)
{
    if (savingInProgress)
    {
        _log("Save is is progress. Load file skipped\n");
        return;
    }

    // app status
    JsonDocument &app = status();

    // if no current file is specified then skip
    if (fileName.length() == 0)
    {
        //
        app["error"] = "Filename is empty. Can't load.\n";
        app["screen"] = ERRORSCREEN;

        //
        _debug(app["error"]);

        return;
    }

    //
    _log("Editor loading file [%s]\n", fileName.c_str());

    // Step 1. Create file if necessary
    if (!gfs()->exists(fileName.c_str()))
    {
        _debug("Creating an empty file since it's new\n");
        File file = gfs()->open(fileName.c_str(), "w");
        if (!file)
        {
            //
            app["error"] = format("Failed to create a file. %s\n", fileName.c_str());
            app["screen"] = ERRORSCREEN;

            //
            _log(app["error"].as<const char *>());

            return;
        }

        //
        file.close();
        delay(100);

        //
        _debug("File created. %s\n", fileName.c_str());
    }

    // Save filen name
    this->fileName = fileName;

    // Open File
    File file = gfs()->open(fileName.c_str(), "r");
    if (!file)
    {
        //
        app["error"] = format("file open failed %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;

        //
        _debug(app["error"]);

        return;
    }

    // Determine file size and set buffer accordingly
    fileSize = file.size();
    _debug("File: %s of size: %d\n", fileName.c_str(), fileSize);

    // calcualte the file offset
    seekPos = 0;
    int stepSize = BUFFER_SIZE / 2; // use half of the buffer
    if (fileSize > 0)
    {
        // this offset will offer last portion of the buffer
        seekPos = (fileSize / stepSize) * stepSize;
    }

    // when it is exactly the buffer end
    // go one buffer behind so that screen will show something
    if (fileSize == seekPos && seekPos > 0)
    {
        if (seekPos > stepSize)
            seekPos -= stepSize;
        else
            // defensive code in order for the offset not to go negative (MAX in unsigned in)
            seekPos = 0;
    }

    _log("File seekPos: %d\n", seekPos);

    // move the file position to offset
    if (!file.seek(seekPos))
    {
        //
        file.close();
        delay(100);

        //
        app["error"] = format("Failed to seek file pointer. fileSize: %d seekPos: %d\n", fileSize, seekPos);
        app["screen"] = ERRORSCREEN;
        _debug(app["error"].as<const char *>());

        return;
    }

    // reset the buffer
    resetBuffer();

    // Read file content into text buffer
    int bufferSize = 0;
    while (file.available())
    {
        buffer[bufferSize++] = file.read();
    }
    cursorPos = bufferSize;

    // this window mirrors [seekPos, seekPos+bufferSize) on disk exactly
    loadedLength = bufferSize;
    pageChanged = true;

    //
    file.close();
    delay(100);

    // log
    _debug("Editor::loadFile size: %d, seek: %d, buffer: %d, cursor: %d\n",
           fileSize, seekPos, bufferSize, cursorPos);

    // Update the Screen Buffer
    updateScreen();

    // Update the word count
    wordCountFile = wordcounter_file(fileName.c_str());
    wordCountBuffer = wordcounter_buffer(buffer);

    // update the word count in config
    int file_index = app["config"]["file_index"].as<int>();
    app["config"][format("wordcount_file_%d", file_index)] = wordCountFile;
    app["config"][format("wordcount_buffer_%d", file_index)] = wordCountBuffer;

    //
    config_save();
}

// Copy `count` bytes from the current position of `src` to `dst`.
// Returns false if fewer than `count` bytes could be read.
static bool copyFileChunk(File &src, File &dst, size_t count)
{
    const size_t chunkSize = 512;
    uint8_t chunk[chunkSize];
    size_t remaining = count;
    while (remaining > 0)
    {
        size_t toRead = remaining < chunkSize ? remaining : chunkSize;
        size_t readSize = src.read(chunk, toRead);
        if (readSize == 0)
            return false;
        dst.write(chunk, readSize);
        remaining -= readSize;
    }
    return true;
}

bool Editor::saveFile()
{
    if (savingInProgress)
    {
        _log("Save already in progress, skipping.\n");
        return false;
    }
    savingInProgress = true;

    //
    JsonDocument &app = status();

    // if no current file is specified then skip
    if (fileName.length() == 0)
    {
        //
        app["error"] = "Filename is empty. Can't save.\n";
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        savingInProgress = false;
        return false;
    }

    // if already saved nothing to do
    if (this->saved)
    {
        _log("File already saved. No operation required.\n");
        savingInProgress = false;
        return true;
    }

    //
    _log("Saving file %s\n", fileName.c_str());

#ifdef USE_IME
    // Release the Wubi dictionary's open file handle before we touch the SD
    // FAT tree below (open/rename/remove). Holding a handle open on the volume
    // during those operations froze the device mid-composition. The handle
    // reopens lazily on the next lookup; the RAM index survives.
    IME::getInstance().suspend();
#endif

    // The buffer is a window onto [seekPos, seekPos+loadedLength) on disk.
    // Anything outside that window (before seekPos, after windowEnd) must
    // survive the save untouched.
    size_t newLength = getBufferSize();
    size_t windowEnd = seekPos + loadedLength;
    bool hasTrailingData = windowEnd < fileSize;

    // FAST PATH: window is the tail of the file and isn't shrinking -
    // just overwrite/extend in place, no rewrite of the rest of the file needed.
    if (!hasTrailingData && newLength >= loadedLength)
    {
        File file = gfs()->open(fileName.c_str(), "r+"); // read/write, no truncate
        if (!file)
        {
            // If file doesn't exist, create it
            file = gfs()->open(fileName.c_str(), "w+"); // create + read/write
        }

        if (!file)
        {
            //
            app["error"] = "Failed to open file for writing\n";
            app["screen"] = ERRORSCREEN;

            //
            _log(app["error"]);
            savingInProgress = false;
            return false;
        }

        // Seek to the last loaded offset
        if (!file.seek(seekPos))
        {
            _log("Failed to seek file pointer\n");
            file.close();
            delay(100);

            savingInProgress = false;
            return false;
        }
        _log("Writing file at: %d\n", seekPos);

        // writing the file content
        size_t length = file.print(buffer);
        _log("File written: %d bytes\n", length);

        file.close();
        delay(100);

        fileSize = seekPos + length;
        loadedLength = length;
    }

    // SPLICE PATH: either the tail is shrinking, or this window sits in the
    // middle of the file and there is trailing data after it that must be
    // preserved. Rewrite the file as [prefix][new window content][suffix].
    else
    {
        String tempFileName = format("%s.tmp", fileName.c_str());
        if (gfs()->exists(tempFileName.c_str()))
        {
            gfs()->remove(tempFileName.c_str());
        }

        if (!gfs()->rename(fileName.c_str(), tempFileName.c_str()))
        {
            app["error"] = "Save failed (rename for splice)\n";
            app["screen"] = ERRORSCREEN;
            _log(app["error"].as<const char *>());
            savingInProgress = false;
            return false;
        }

        File src = gfs()->open(tempFileName.c_str(), "r");
        File dst = gfs()->open(fileName.c_str(), "w");
        bool ok = src && dst;

        if (ok)
            ok = copyFileChunk(src, dst, seekPos); // prefix, unchanged

        if (ok)
            dst.print(buffer); // this window's edited content

        if (ok && fileSize > windowEnd)
        {
            // suffix, unchanged
            ok = src.seek(windowEnd) && copyFileChunk(src, dst, fileSize - windowEnd);
        }

        if (src)
            src.close();
        if (dst)
            dst.close();
        delay(100);

        if (!ok)
        {
            // Restore the original file so a transient I/O error never
            // leaves the journal missing or half-written.
            app["error"] = "Save failed (splice)\n";
            app["screen"] = ERRORSCREEN;
            _log(app["error"].as<const char *>());

            gfs()->remove(fileName.c_str());
            gfs()->rename(tempFileName.c_str(), fileName.c_str());

            savingInProgress = false;
            return false;
        }

        gfs()->remove(tempFileName.c_str());

        fileSize = fileSize + (newLength - loadedLength);
        loadedLength = newLength;
    }

    // flag to save
    this->saved = true;

    // Persist the word count alongside the file itself, instead of on a
    // fixed timer (see wordcounter_service()) - that way writing config.json
    // only ever happens at a moment a flash write was going to happen anyway,
    // and never interrupts active typing.
    wordCountBuffer = wordcounter_buffer(buffer);
    int file_index = app["config"]["file_index"].as<int>();
    app["config"][format("wordcount_buffer_%d", file_index)] = wordCountBuffer;
    config_save();

    //
    savingInProgress = false;

#if defined(DEBUG) && defined(BOARD_PICO)
    printMemoryUsage();
#endif

    return true;
}

// Read `length` bytes starting at `offset` from disk into the buffer,
// replacing whatever window was loaded before. Used by paging so the
// in-memory buffer can slide to any part of the file, not just the tail.
bool Editor::loadWindow(size_t offset, size_t length)
{
    JsonDocument &app = status();

    File file = gfs()->open(fileName.c_str(), "r");
    if (!file)
    {
        app["error"] = format("file open failed %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;
        _debug(app["error"]);
        return false;
    }

    if (!file.seek(offset))
    {
        file.close();
        delay(100);

        app["error"] = format("Failed to seek file pointer. offset: %d\n", offset);
        app["screen"] = ERRORSCREEN;
        _debug(app["error"].as<const char *>());
        return false;
    }

    resetBuffer();
    size_t bytesRead = 0;
    while (bytesRead < length && file.available())
    {
        buffer[bytesRead++] = file.read();
    }

    file.close();
    delay(100);

    seekPos = offset;
    loadedLength = bytesRead;
    pageChanged = true;

    _debug("Editor::loadWindow offset: %d length: %d read: %d\n", offset, length, bytesRead);

    return true;
}

// Load the chunk of the file that ends exactly where the current window
// starts, so the writer can keep scrolling back through earlier text.
void Editor::pageBackward()
{
    if (seekPos == 0)
    {
        _log("pageBackward: already at the start of the file\n");
        return;
    }

    if (!saved && !saveFile())
    {
        _log("pageBackward: flush failed, staying on current page\n");
        return;
    }

    size_t windowEnd = seekPos;
    size_t stepSize = BUFFER_SIZE / 2;
    size_t newSeekPos = (windowEnd > stepSize) ? windowEnd - stepSize : 0;

    if (!loadWindow(newSeekPos, windowEnd - newSeekPos))
        return;

    // land at the end, continuing the upward motion seamlessly
    cursorPos = getBufferSize();
    updateScreen();
}

// Load the chunk of the file that starts exactly where the current window
// ends, so the writer can scroll forward again after paging backward.
void Editor::pageForward()
{
    if (!saved && !saveFile())
    {
        _log("pageForward: flush failed, staying on current page\n");
        return;
    }

    size_t windowEnd = seekPos + loadedLength;
    if (windowEnd >= fileSize)
    {
        // already at the live tail - nothing further on disk
        return;
    }

    size_t stepSize = BUFFER_SIZE / 2;
    size_t remaining = fileSize - windowEnd;
    size_t toLoad = (remaining <= stepSize) ? remaining : stepSize;

    if (!loadWindow(windowEnd, toLoad))
        return;

    // land at the start, continuing the downward motion seamlessly
    cursorPos = 0;
    updateScreen();
}

// The buffer filled up while typing. Flush it, then keep going from
// wherever this window ended - either a fresh empty tail window, or the
// next chunk of on-disk content if this wasn't the tail. Replaces the old
// behaviour of unconditionally jumping back to the tail, which would have
// discarded the writer's place (and unsaved on-disk content past it) when
// triggered on a non-tail window.
void Editor::advanceWindow()
{
    if (!saveFile())
    {
        _log("advanceWindow: flush failed, buffer is full and can't advance\n");
        return;
    }

    size_t windowEnd = seekPos + loadedLength;
    if (windowEnd >= fileSize)
    {
        // was at the tail - open a fresh empty window to keep typing into
        seekPos = windowEnd;
        loadedLength = 0;
        resetBuffer();
        cursorPos = 0;
        pageChanged = true;
    }
    else
    {
        size_t stepSize = BUFFER_SIZE / 2;
        size_t remaining = fileSize - windowEnd;
        size_t toLoad = (remaining <= stepSize) ? remaining : stepSize;

        if (!loadWindow(windowEnd, toLoad))
            return;

        cursorPos = 0;
    }

    updateScreen();
}

// Make the current file empty
void Editor::clearFile()
{
    if (savingInProgress)
    {
        _log("Save is is progress. Clear file skipped\n");
        return;
    }

    //
    JsonDocument &app = status();

    // if no current file is specified then skip
    if (fileName.length() == 0)
    {
        //
        app["error"] = "Filename is empty. Can't clear.\n";
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return;
    }

    // Step 1. Check if the backup file exists
    // remove it if already exists
    String backupFileName = format("/%s_backup.txt", fileName.c_str());
    _debug("backupFilename: %s\n", backupFileName);
    if (gfs()->exists(backupFileName.c_str()))
    {
        // remove the backup file
        gfs()->remove(backupFileName.c_str());
    }

    // Step 2. Rename the current file to the backup.txt
    if (gfs()->rename(fileName.c_str(), backupFileName.c_str()))
    {
        _debug("File renamed successfully: %s -> %s.\n", fileName.c_str(), backupFileName.c_str());
    }
    else
    {
        //
        app["error"] = format("Error making a backup file. %s\n", backupFileName);
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return;
    }

    // Step 3. Empty the current file by opening it with FILE_WRITE
    File file = gfs()->open(fileName.c_str(), "w");
    if (!file)
    {
        //
        app["error"] = format("Failed to create an empty file %s\n", fileName.c_str());
        app["screen"] = ERRORSCREEN;

        //
        _log(app["error"]);

        return;
    }

    // clean up file
    file.close();
    delay(100);

    // Go through the loading process of the empty file
    loadFile(fileName);
}

// House Keeping Tasks
void Editor::loop()
{
    unsigned long now = millis();
    // Auto Repeat is activated
    if (lastKey != 0)
    {
        // check if past point of repeatDelay
        if (now - lastPressTime > repeatDelay)
        {
            // Check if past repeatInterval
            if (now - lastPressTime - repeatDelay >= repeatInterval)
            {
                keyboard(lastKey, true);
                lastPressTime = now - repeatDelay;
            }
        }
    }
}

// Handle Keyboard Input
void Editor::keyboard(int key, bool pressed)
{
    // ignore non printable character
    if (key == 0 || key == 27 || key == MENU)
        return;

    //
    _debug("Editor::keyboard:: %c [%d] pressed: %d cursorPos: %d\n", key, key, pressed, cursorPos);

#ifdef DEBUG
    _debug("Buffer: %d\n", getBufferSize());
#endif

    // when any key is pressed track the last key pressed and if they don't release
    // keep issueing press events so that it keeps on typing on the screen
    if (pressed)
    {
        if (key != lastKey)
        {
            // New key or new press: process immediately
            lastKey = key;
            lastPressTime = millis();
            _debug("Auto Repeat %d %d\n", lastKey, lastPressTime);
        }
    }
    else
    {
        _debug("Auto Repeat Release %d\n", lastKey);
        // Key released: reset state
        lastKey = 0;
        lastPressTime = 0;
    }

    // below is only for when the key is pressed
    if (pressed)
    {
        // Any key other than a kill (C-k / M-d) breaks a run of kills, so the
        // NEXT kill starts a fresh kill buffer instead of appending. The kill
        // handlers re-set this to true when they run later in this call.
        if (key != KEY_KILL_LINE && key != KEY_KILL_WORD_FWD)
            lastActionWasKill = false;

        //////////////////////////
        // BACKWARD EDITING
        //////////////////////////
        if (key == '\b')
        {
            // buffer has more than 1 character
            if (getBufferSize() > 0)
            {
                //
                removeLastChar();

                // set saved flag to false
                this->saved = false;

                // set flag
                this->backSpacePressed = true;
            }
            // buffer emptied
            else
            {
                // Load previous contents from the file if at the beginning of the buffer
                _log("Backspace reached the beginning of the buffer\n");
            }
        }

        // DEL key deletes the word
        else if (key == 127)
        {
            if (getBufferSize() > 0)
            {
                // if editing at the end of the line then remove word
                if (cursorPos == getBufferSize())
                {
                    removeLastWord();
                }

                else
                {
                    // remove word in front
                    removeCharAtCursor();
                }

                // set saved flag to false
                this->saved = false;

                // set flag
                this->backSpacePressed = true;
            }
            // buffer emptied
            else
            {
                // Load previous contents from the file if at the beginning of the buffer
                _log("Delete word reached the beginning of the buffer\n");
            }
        }

        //////////////////////////
        // EMACS / READLINE EDITING
        //////////////////////////
        else if (key == KEY_WORD_FWD || key == KEY_WORD_BACK ||
                 key == KEY_KILL_WORD_FWD || key == KEY_YANK ||
                 key == KEY_KILL_LINE)
        {
            if (key == KEY_WORD_FWD)
                moveWordForward();
            else if (key == KEY_WORD_BACK)
                moveWordBackward();
            else if (key == KEY_KILL_LINE)
                killToEndOfLine();
            else if (key == KEY_KILL_WORD_FWD)
                killWordForward();
            else if (key == KEY_YANK)
                yank();
        }

        //////////////////////////
        // CURSORS
        //////////////////////////
        else if (key >= 18 && key <= 23 || key == 2 || key == 3)
        {
            // arrow keys
            // 18 - Left, 19 - Right, 20 - Up, 21 - Down
            // 22 - Page Up, 23 - Page Down
            // 2 - Home 3 - End
            if (key == 18)
            {
                if (cursorPos == 0)
                {
                    // already at the start of the buffer - load the previous page
                    pageBackward();
                }
                else
                {
                    // left - step back over a whole UTF-8 character so the
                    // cursor never stops between the bytes of one hanzi
                    --cursorPos;
                    while (cursorPos > 0 &&
                           utf8_is_continuation((uint8_t)buffer[cursorPos]))
                        --cursorPos;
                }
            }
            else if (key == 19)
            {
                // cursor can't move outside the last text
                if (cursorPos < getBufferSize())
                    // right - advance by the length of the character here
                    cursorPos += utf8_char_len((uint8_t)buffer[cursorPos]);
                else
                    // already at the end of the buffer - load the next page
                    pageForward();

                if (cursorPos > getBufferSize())
                    cursorPos = getBufferSize();
            }

            // UP
            else if (key == 20)
            {
                // move the cursorPos to the start of the previous line
                if (cursorLine == 0)
                {
                    // already at the top line - load the previous page
                    pageBackward();
                }
                else if (cursorLine > 0)
                {
                    // look at the previous line and move to the start of the cursor
                    int newCursorPos =
                        linePositions[cursorLine - 1] - linePositions[0];

                    // if previous line length is shorter than cursorLinePos
                    int previousLineLength = lineLengths[cursorLine - 1];
                    if (previousLineLength < cursorLinePos)
                    {
                        // then move to the end of the previous line
                        newCursorPos += previousLineLength - 1;
                    }
                    else
                    {
                        // if previous line length is long enough
                        // then move as much as the currentLineCursorPos
                        newCursorPos += cursorLinePos;
                    }

                    // edge case
                    if (newCursorPos < 0)
                        newCursorPos = 0;

                    //
                    cursorPos = newCursorPos;
                }
            }

            // DOWN
            else if (key == 21)
            {
                // move the cursorPos to the start of the next line
                if (cursorLine < totalLine)
                {
                    //
                    int newCursorPos =
                        linePositions[cursorLine + 1] - linePositions[0];

                    // next line length
                    int nextLineLength = max(lineLengths[cursorLine + 1], 1);

                    // if next line has shorter length of the current cursor pos
                    // then move to the end of the next line
                    if (cursorLinePos > nextLineLength)
                    {
                        newCursorPos += nextLineLength - 1;
                    }
                    else
                    {
                        //
                        newCursorPos += cursorLinePos;
                    }

                    // handle edge case
                    if (newCursorPos > getBufferSize())
                        newCursorPos = getBufferSize();

                    //
                    cursorPos = newCursorPos;
                    cursorLine += 1;
                }

                // when trying to go down at the last line, move the cursor to the end
                else if (cursorLine == totalLine)
                {
                    // already at the end of the buffer - load the next page
                    if (cursorPos == getBufferSize())
                    {
                        pageForward();
                    }
                    else
                    {
                        // if last line then move to the end of the buffer
                        cursorPos = getBufferSize();

                        _debug("Editor::keyboard::DOWN last line condition met cursorPos %d\n",
                               cursorPos);
                    }
                }
            }

            // HOME
            else if (key == 2)
            {
                // home - move to the start of the line
                int newCursorPos =
                    linePositions[cursorLine] - linePositions[0];
                //
                cursorPos = newCursorPos;
            }

            // END
            else if (key == 3)
            {
                // end - move to the end of the line
                int lineLength = max(lineLengths[cursorLine], 1);
                int newCursorPos =
                    linePositions[cursorLine] - linePositions[0] + lineLength - 1;

                // if last line then move to the end of the buffer
                if (cursorLine == totalLine)
                    newCursorPos = getBufferSize();

                //
                cursorPos = newCursorPos;
            }

            // PAGE UP
            else if (key == 22)
            {
                if (cursorLine == 0)
                {
                    // already at the top line - load the previous page
                    pageBackward();
                }
                else
                {
                    int newCursorLine = max(cursorLine - rows, 0);
                    int newCursorPos =
                        linePositions[newCursorLine] - linePositions[0];

                    //
                    cursorPos = newCursorPos;
                }
            }

            // PAGE DOWN
            else if (key == 23)
            {
                if (cursorLine == totalLine && cursorPos == getBufferSize())
                {
                    // already at the end of the buffer - load the next page
                    pageForward();
                }
                else
                {
                    int newCursorLine = min(cursorLine + rows, totalLine);
                    int lineLength = max(lineLengths[newCursorLine], 1);
                    int newCursorPos =
                        linePositions[newCursorLine] - linePositions[0] + lineLength - 1;

                    // if last line then move to the end of the buffer
                    if (cursorLine == totalLine)
                        newCursorPos = getBufferSize();

                    //
                    cursorPos = newCursorPos;
                }
            }
        }

        //////////////////////////
        // FORWARD EDITING
        //////////////////////////
        else
        {
            // add to the edit buffer new character
            if (getBufferSize() >= BUFFER_SIZE)
            {
                _log("Text buffer full\n");

                //
                advanceWindow();
            }

            //
            addChar(key);

            // set saved flag to false
            this->saved = false;
        }

        // update the screen buffer
        updateScreen();
    }
}

//
void Editor::updateScreen()
{
    // Loop through the text buffer
    // and product the data structure that is splitted in each line
    _debug("Editor::updateScreen called\n");

    // Handle empty buffer
    if (buffer[0] == '\0')
    {
        totalLine = 0;
        linePositions[0] = buffer;
        lineLengths[0] = 0;
        cursorLine = 0;
        cursorLinePos = 0;
        return;
    }

    // first line is the first of the buffer
    linePositions[0] = &buffer[0];
    lineLengths[0] = 0;

    //
    this->totalLine = 0;
    int line_count = 0; // BYTES on the current line (drives lineLengths / cursor)
    int display_col = 0; // VISUAL columns on the current line (CJK counts as 2)

    // remember the last space position to use it for the word wrap
    int last_space_index = -1;
    int last_space_position = -1;

    //
    // BUFFER -> SPLIT IN LINES
    //
    // Iterate one WHOLE character at a time (clen bytes). Advancing per
    // character - never per byte - is what guarantees a multi-byte hanzi is
    // never split across a line boundary (which used to show up as "ä\nä").
    int i = 0;
    while (i < BUFFER_SIZE)
    {
        uint8_t b = (uint8_t)buffer[i];

        // When reaching the end of text, break
        if (b == '\0')
        {
            // Update the length of the last line
            lineLengths[totalLine] = line_count;
            break;
        }

        // Length in bytes and visual columns of the character at `i`.
        int clen = utf8_char_len(b);
        if (i + clen > BUFFER_SIZE)
            clen = 1; // guard against a truncated trailing sequence
        int char_cols = (clen >= 2) ? 2 : 1;

        // A hard newline always breaks the line here.
        if (b == '\n')
        {
            lineLengths[totalLine] = line_count + 1; // include the '\n'
            linePositions[++totalLine] = &buffer[i + 1];
            line_count = 0;
            display_col = 0;
            last_space_index = -1;
            last_space_position = -1;
            i += 1;
            continue;
        }

        // PRE-EMPTIVE WRAP: if placing this whole character would overflow the
        // line width, break BEFORE it. Wrap at the last space when there is one
        // (word wrap), otherwise break right here (hard wrap). Either way the
        // break lands on a character boundary, so no glyph is ever cut.
        if (line_count > 0 && display_col + char_cols > cols)
        {
            if (last_space_index != -1)
            {
                // word wrap: end this line at the last space
                lineLengths[totalLine] = last_space_position;
                linePositions[++totalLine] = &buffer[last_space_index + 1];

                // carry the trailing word onto the new line and recompute width
                line_count -= last_space_position;
                display_col = 0;
                for (char *p = &buffer[last_space_index + 1]; p < &buffer[i]; p++)
                {
                    uint8_t pb = (uint8_t)*p;
                    if (!utf8_is_continuation(pb))
                        display_col += (utf8_char_len(pb) >= 2) ? 2 : 1;
                }
            }
            else
            {
                // hard wrap: this character starts a fresh line
                lineLengths[totalLine] = line_count;
                linePositions[++totalLine] = &buffer[i];
                line_count = 0;
                display_col = 0;
            }
            last_space_index = -1;
            last_space_position = -1;
        }

        // Place the whole character on the current line
        line_count += clen;
        display_col += char_cols;

        // Track the position of the last space for word wrapping
        if (b == ' ')
        {
            last_space_index = i;
            last_space_position = line_count;
        }

        i += clen;
    }

    // Handle cursor position beyond buffer
    if (cursorPos >= BUFFER_SIZE)
    {
        cursorPos = BUFFER_SIZE - 1;
    }

    //
    // CALCULATE CURSOR INFORMATION
    //
    char *pCursorPos = &buffer[cursorPos];

    //
    cursorLine = 0;
    cursorLinePos = lineLengths[0];

    // caculate the which line cursor is located and the line position
    for (int i = totalLine; i >= 0; i--)
    {
        //
        if (pCursorPos >= linePositions[i])
        {
            // found the line index
            cursorLine = i;
            // calculate the cursor position within the line
            cursorLinePos = pCursorPos - linePositions[i];
            break;
        }
    }

    //
    _debug("Editor::updateScreen cursorPos: %d\n", cursorPos);
}

void Editor::addChar(int c)
{
    int bufferSize = getBufferSize();
    if (bufferSize < BUFFER_SIZE)
    {
        // shift the trailing texts
        if (bufferSize > cursorPos)
            memmove(buffer + cursorPos + 1, buffer + cursorPos, bufferSize - cursorPos);

        //
        buffer[cursorPos++] = c;
        buffer[++bufferSize] = '\0';

        _debug("FileBuffer::addChar::cursorPos %d %c\n", cursorPos, c);
    }
}

void Editor::addString(const char *utf8)
{
    for (const char *p = utf8; *p; p++)
        addChar((uint8_t)*p);
}

void Editor::removeLastChar()
{
    int bufferSize = getBufferSize();

    //
    _debug("FileBuffer::removeLastChar cusorPos: %d bufferSize: %d\n", cursorPos, bufferSize);

    if (bufferSize > 0 && cursorPos > 0)
    {
        // Delete a whole UTF-8 character: walk back over any continuation
        // bytes so one backspace removes one hanzi rather than one byte.
        int removeBytes = 1;
        int from = cursorPos - 1;
        while (from > 0 && utf8_is_continuation((uint8_t)buffer[from]))
        {
            --from;
            ++removeBytes;
        }

        // Shift the trailing text left over the deleted character
        if (bufferSize > cursorPos)
        {
            memmove(buffer + from, buffer + cursorPos, bufferSize - cursorPos);
        }

        // Null terminate the buffer
        buffer[bufferSize - removeBytes] = 0;
        cursorPos = from;

        bufferSize = getBufferSize();
        _debug("FileBuffer::removeLastChar After cusorPos: %d bufferSize: %d\n", cursorPos, bufferSize);
    }
}

void Editor::removeCharAtCursor()
{
    int bufferSize = getBufferSize();
    if (bufferSize > 0 && cursorPos < bufferSize)
    {
        // Delete the whole UTF-8 character that starts at the cursor
        int removeBytes = utf8_char_len((uint8_t)buffer[cursorPos]);
        if (cursorPos + removeBytes > bufferSize)
            removeBytes = bufferSize - cursorPos;

        // Shift the trailing text left over the deleted character
        if (bufferSize > cursorPos + removeBytes)
        {
            memmove(buffer + cursorPos, buffer + cursorPos + removeBytes,
                    bufferSize - cursorPos - removeBytes);
        }

        // Decrease buffer size
        bufferSize -= removeBytes;

        // Null terminate the buffer
        buffer[bufferSize] = '\0';
    }
}

void Editor::removeLastWord()
{
    int length = getBufferSize();
    if (length == 0)
        return;

    int end = length - 1;
    while (end >= 0 && buffer[end] == ' ')
        end--;

    if (end < 0)
        return;

    int start = end;
    while (start >= 0 && buffer[start] != ' ' && buffer[start] != '\n')
        start--;

    if (start <= 0)
    {
        start = 0;
        buffer[0] = '\0';
    }
    else
    {
        buffer[start] = ' ';
        buffer[start + 1] = '\0';
    }

    cursorPos = getBufferSize();

    //
    _debug("FileBuffer::removeLastWord %d\n", cursorPos);
}

//
// Emacs / readline editing helpers
//

// A "word" is a run of alphanumeric ASCII or any multi-byte (>= 0x80) byte, so
// hanzi count as word characters. Everything else (space, punctuation, newline)
// is a separator.
static bool editor_is_word_byte(uint8_t b)
{
    if (b >= 0x80)
        return true; // any UTF-8 lead/continuation byte -> part of a word
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
           (b >= 'a' && b <= 'z');
}

// Delete `count` bytes starting at `start`, shifting the tail left. Does not
// move the cursor. Safe against overrun.
static void editor_delete_bytes(char *buffer, int start, int count, int bufferSize)
{
    if (count <= 0 || start < 0 || start >= bufferSize)
        return;
    if (start + count > bufferSize)
        count = bufferSize - start;
    memmove(buffer + start, buffer + start + count, bufferSize - start - count);
    buffer[bufferSize - count] = '\0';
}

void Editor::moveWordForward()
{
    int size = getBufferSize();
    int p = cursorPos;
    // skip separators, then skip the word
    while (p < size && !editor_is_word_byte((uint8_t)buffer[p]))
        p++;
    while (p < size && editor_is_word_byte((uint8_t)buffer[p]))
        p++;
    cursorPos = p;
    lastActionWasKill = false;
}

void Editor::moveWordBackward()
{
    int p = cursorPos;
    // step back over separators, then over the word
    while (p > 0 && !editor_is_word_byte((uint8_t)buffer[p - 1]))
        p--;
    while (p > 0 && editor_is_word_byte((uint8_t)buffer[p - 1]))
        p--;
    cursorPos = p;
    lastActionWasKill = false;
}

void Editor::killToEndOfLine()
{
    int size = getBufferSize();
    if (cursorPos >= size)
    {
        lastActionWasKill = false;
        return;
    }

    // find the end of the current line (the next '\n', or end of buffer)
    int eol = cursorPos;
    while (eol < size && buffer[eol] != '\n')
        eol++;

    String killed;
    if (eol > cursorPos)
    {
        // kill the text up to (not including) the newline
        killed.reserve(eol - cursorPos + 1);
        for (int i = cursorPos; i < eol; i++)
            killed += buffer[i];
        editor_delete_bytes(buffer, cursorPos, eol - cursorPos, size);
    }
    else
    {
        // already at end of line: kill the newline itself (join next line up)
        killed += '\n';
        editor_delete_bytes(buffer, cursorPos, 1, size);
    }

    // consecutive C-k presses accumulate into the kill buffer (Emacs behavior)
    if (lastActionWasKill)
        killBuffer += killed;
    else
        killBuffer = killed;

    lastActionWasKill = true;
    this->saved = false;
}

void Editor::killWordForward()
{
    int size = getBufferSize();
    int p = cursorPos;
    while (p < size && !editor_is_word_byte((uint8_t)buffer[p]))
        p++;
    while (p < size && editor_is_word_byte((uint8_t)buffer[p]))
        p++;

    int count = p - cursorPos;
    if (count <= 0)
    {
        lastActionWasKill = false;
        return;
    }

    String killed;
    killed.reserve(count + 1);
    for (int i = cursorPos; i < p; i++)
        killed += buffer[i];
    editor_delete_bytes(buffer, cursorPos, count, size);

    if (lastActionWasKill)
        killBuffer += killed;
    else
        killBuffer = killed;

    lastActionWasKill = true;
    this->saved = false;
}

void Editor::yank()
{
    lastActionWasKill = false;
    if (killBuffer.length() == 0)
        return;

    // reuse the normal insertion path so buffer-full windowing still applies
    if (getBufferSize() + (int)killBuffer.length() >= BUFFER_SIZE)
    {
        _log("Text buffer full - yank skipped\n");
        return;
    }

    addString(killBuffer.c_str());
    this->saved = false;
}
