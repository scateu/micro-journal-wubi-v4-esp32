#pragma once

//
#include <Arduino.h>
#include <vector>

//
// Wubi 86 Chinese Input Method.
//
// The IME sits in front of the editor: while it is active it consumes a-z keys
// to build a Wubi code, looks up candidate hanzi in the dictionary, and emits
// the chosen hanzi as a UTF-8 string for the editor to insert. ASCII typing is
// untouched when the IME is inactive.
//
// The dictionary (tools/gen_wubi.py, WUB2 format) is COMPILED INTO FLASH via
// board_build.embed_files = data/wubi86.bin (see WUBI86.md) and read in place
// as memory-mapped flash: no SD access, no file handle, no per-lookup seeks.
// Only the 2.7 KB prefix index is copied to RAM. Because the dictionary never
// touches the SD card, IME lookups can never contend with journal writes.
//
class IME
{
public:
    // Point at the embedded dictionary and parse its header/index. Safe to call
    // more than once; returns true when the table is available.
    bool begin();
    bool loaded() const { return _loaded; }

    // Chinese input mode on/off. Toggling clears any in-progress composition.
    bool active() const { return _active; }
    void setActive(bool on);
    void toggle() { setActive(!_active); }

    // Feed a key. Returns true when the IME consumed it (so the caller must NOT
    // forward it to the editor). Returns false to let the editor handle it.
    // On a successful commit, `out` receives the UTF-8 hanzi to insert.
    bool handleKey(int key, String &out);

    // Current composition (the typed Wubi letters), for the candidate bar.
    const String &composition() const { return _code; }

    // Candidates visible on the current page (already best-first).
    const std::vector<String> &candidates() const { return _page; }

    // True when there is something to draw in the candidate bar.
    bool composing() const { return _code.length() > 0; }

    //////////////////////////////////
    // SINGLETON
    static IME &getInstance()
    {
        static IME instance;
        return instance;
    }
    IME(const IME &) = delete;
    IME &operator=(const IME &) = delete;
    //////////////////////////////////

private:
    IME() {}

    // dictionary record: fixed 8 bytes (code[4] + hanzi[3] + flag[1])
    static const int RECORD_SIZE = 8;
    static const int HEADER_SIZE = 8; // magic[4] + count[4]

    // First-two-letter prefix index (see tools/gen_wubi.py). A flat lower-bound
    // table: entry k = (c0-'a')*26 + (c1-'a') holds the first record index whose
    // code sorts >= that two-letter prefix. Entry INDEX_ENTRIES-1 is a sentinel
    // == _count, so any bucket k covers records [_index[k], _index[k+1]).
    // Copied to RAM once at begin() (2708 bytes) so a query jumps straight to a
    // hundreds-record window instead of binary-searching all ~33k records.
    static const int INDEX_ENTRIES = 26 * 26 + 1; // 677

    bool _loaded = false;
    bool _active = false;

    // Pointer to the flash-mapped dictionary blob (board_build.embed_files) and
    // its size. Records are read straight from here - no RAM copy, no SD.
    const uint8_t *_blob = nullptr;
    size_t _blobSize = 0;
    uint32_t _count = 0;

    // Byte offset of record 0 in the blob: HEADER_SIZE for a legacy WUB1 blob,
    // HEADER_SIZE + INDEX_ENTRIES*4 for WUB2 (which carries the prefix index).
    size_t _recordBase = HEADER_SIZE;
    // The prefix index, or empty for a legacy WUB1 blob without one.
    std::vector<uint32_t> _index;

    // Narrow the search to the record window [lo,hi) that could match the typed
    // code, using the RAM index. Falls back to the full table when no index is
    // loaded or the code has no usable leading letters.
    void searchWindow(const char *code, int len, uint32_t &lo, uint32_t &hi);

    // Parse the WUB1/WUB2 header + prefix index from the start of the blob.
    // Fills _count, _recordBase and _index. `total` is the whole blob size.
    bool parseHeader(const uint8_t *hdrIndex, size_t total);

    // Read record `i`'s 4-byte code / 3-byte hanzi from the flash blob.
    bool readCode(uint32_t i, char out[5]);
    bool readHanzi(uint32_t i, char out[4]);

    String _code;                   // typed Wubi letters
    std::vector<String> _all;       // all candidates for the current code
    std::vector<String> _page;      // candidates on the current page
    int _pageStart = 0;             // index into _all of the first page entry
    static const int PAGE_SIZE = 9; // candidates shown / selectable per page

    void reset();          // clear composition state
    void lookup();         // refresh _all from _code
    void buildPage();      // refresh _page from _all + _pageStart
    bool commit(int idx, String &out); // commit page candidate idx (0-based)
};
