#include "Tools.h"
#include "app/app.h"

String formatNumber(int num)
{
    String formattedNumber = "";
    int digitCount = 0;
    if (num < 0)
    {
        formattedNumber += "-";
        num = -num;
    }
    do
    {
        if (digitCount > 0 && digitCount % 3 == 0)
        {
            formattedNumber = "," + formattedNumber;
        }
        formattedNumber = String(num % 10) + formattedNumber;
        num /= 10;
        digitCount++;
    } while (num > 0);

    return formattedNumber;
}

// Get the size of a file in bytes
size_t fileSize(String fileName)
{
    size_t file_size = 0;
    if (gfs()->exists(fileName.c_str()))
    {
        File file = gfs()->open(fileName.c_str(), "r");
        if (!file)
        { // something bad happened
            char buffer[32];
            sprintf(buffer, "Failed to open a file. %s\n", fileName);
            _log(buffer);
            file_size = -1;
        }
        else
        { // file exists
            file_size = file.size();
        }
        //
        file.close();
        delay(100);
    }
    return file_size;
}

// Create an array of String objects
// Many of these ascii codes can be tracked back to:
// https://en.wikipedia.org/wiki/ISO/IEC_8859 column 1 of the table
static const String extended_ascii[128] = {
    "E", // 128 euro sign doesn't work on Micro Journal
    "",
    "", // 130
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 140
    "",
    "Ž", // 142
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // 150
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    " ", // 160
    "¡",
    "¢",
    "£",
    "¤",
    "¥",
    "¦",
    "§",
    "¨",
    "©",
    "ª", // 170 -
    "«",
    "¬",
    "­",
    "®",
    "¯",
    "°",
    "±",
    "²",
    "³",
    "", // 180
    "µ",
    "¶",
    "·",
    "¸",
    "¹",
    "º",
    "»",
    "¼",
    "½",
    "¾", // 190
    "¿",
    "À",
    "Á",
    "Â",
    "Ã",
    "Ä",
    "Å",
    "Æ",
    "Ç",
    "È", // 200
    "É",
    "Ê",
    "Ë",
    "Ì",
    "Í",
    "Î",
    "Ï",
    "Ð",
    "Ñ",
    "Ò", // 210
    "Ó",
    "Ô",
    "Õ",
    "Ö",
    "×",
    "Ø",
    "Ù",
    "Ú",
    "Û",
    "Ü", // 220
    "Ý",
    "Þ",
    "ß",
    "à",
    "á",
    "â",
    "ã",
    "ä",
    "å",
    "æ", // 230
    "ç",
    "è",
    "é",
    "ê",
    "ë",
    "ì",
    "í",
    "î",
    "ï",
    "ð", // 240
    "ñ",
    "ò",
    "ó",
    "ô",
    "õ",
    "ö",
    "÷",
    "ø",
    "ù",
    "ú", // 250
    "û",
    "ü",
    "ý",
    "þ",
    "ÿ", // 255
};

String asciiToUnicode(uint8_t value)
{
    if (value < 128)
        return "";

    uint8_t code = value - 128;
    return extended_ascii[code];
}

//
// UTF-8 helpers
//
int utf8_char_len(uint8_t lead)
{
    if (lead < 0x80)
        return 1; // ASCII
    if ((lead & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    if ((lead & 0xF0) == 0xE0)
        return 3; // 1110xxxx (covers all CJK in the BMP)
    if ((lead & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1;     // continuation or invalid lead - treat as single byte
}

bool utf8_is_continuation(uint8_t b)
{
    return (b & 0xC0) == 0x80;
}

uint32_t utf8_decode(const char *s, int &len)
{
    uint8_t c = (uint8_t)s[0];
    len = utf8_char_len(c);

    uint32_t cp;
    switch (len)
    {
    case 2:
        cp = c & 0x1F;
        break;
    case 3:
        cp = c & 0x0F;
        break;
    case 4:
        cp = c & 0x07;
        break;
    default: // ASCII or invalid lead
        len = 1;
        return c;
    }

    // gather continuation bytes; bail out to Latin-1 on truncation
    for (int i = 1; i < len; i++)
    {
        uint8_t cont = (uint8_t)s[i];
        if (!utf8_is_continuation(cont))
        {
            len = 1;
            return c;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }

    return cp;
}

String format(const char *format, ...)
{
    char buffer[256]; // Adjust the size according to your needs
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return String(buffer);
}

#if defined(DEBUG) && defined(BOARD_PICO)
extern "C" char* sbrk(int incr);


void printMemoryUsage()
{
    char top;
    ptrdiff_t free_memory = &top - reinterpret_cast<char*>(sbrk(0));
    Serial.printf("Stack Free: %td bytes\n", abs(free_memory));
}
#endif
