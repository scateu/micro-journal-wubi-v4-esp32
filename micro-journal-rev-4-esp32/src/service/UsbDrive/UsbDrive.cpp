#include "UsbDrive.h"

#if defined(USE_USB_DRIVE) && defined(BOARD_ESP32_S3)

#include "app/app.h"
#include <SD.h>
#include <SPI.h>
#include <USB.h>
#include <USBMSC.h>

//
// USB Mass Storage device backed by the SD card's RAW sectors.
//
static USBMSC msc;
static bool started = false;
static uint16_t sectorSize = 0;
static uint32_t sectorCount = 0;

// Status is tracked with plain flags updated in the MSC callbacks (which run in
// the USB task) and rendered from usbdrive_loop(). Once the host ejects we LATCH
// `ejected` so subsequent probe reads the OS issues can't flip the screen back
// to "connected" (that was the flicker: eject -> a stray read -> connected).
static volatile bool ejected = false;      // host issued STOP+eject (sticky)
static volatile uint32_t lastRwMs = 0;     // last read/write activity
static volatile bool sawTransfer = false;  // any read/write seen at all

// Host reads a block -> pull it straight off the SD card.
static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (sectorSize == 0 || offset != 0 || (bufsize % sectorSize) != 0)
        return -1;

    uint32_t count = bufsize / sectorSize;
    uint8_t *dst = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++)
    {
        if (!SD.readRAW(dst + (size_t)i * sectorSize, lba + i))
            return -1;
    }
    lastRwMs = millis();
    sawTransfer = true;
    return (int32_t)bufsize;
}

// Host writes a block -> write it straight to the SD card.
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (sectorSize == 0 || offset != 0 || (bufsize % sectorSize) != 0)
        return -1;

    uint32_t count = bufsize / sectorSize;
    for (uint32_t i = 0; i < count; i++)
    {
        // writeRAW takes a non-const buffer; the MSC buffer is writable.
        if (!SD.writeRAW(buffer + (size_t)i * sectorSize, lba + i))
            return -1;
    }
    lastRwMs = millis();
    sawTransfer = true;
    return (int32_t)bufsize;
}

// Host START/STOP UNIT. load_eject && !start = the user ejected on the PC;
// latch it so the screen stays "safe to unplug".
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    if (load_eject && !start)
        ejected = true;
    return true;
}

bool usbdrive_begin()
{
    if (started)
        return true;

    JsonDocument &app = status();

    // Bring up the SD card ourselves (the normal gfs() path is not used in
    // export mode). Pins come from platformio.ini (SD_SCLK/MISO/MOSI/CS).
#if defined(SD_MOSI)
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, SPI, SPI_FREQUENCY))
#else
    if (!SD.begin(SD_CS))
#endif
    {
        _log("[usbdrive] SD.begin failed - no card?\n");
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        _log("[usbdrive] no SD card inserted\n");
        return false;
    }

    sectorSize = (uint16_t)SD.sectorSize();
    sectorCount = (uint32_t)SD.numSectors();
    if (sectorSize == 0 || sectorCount == 0)
    {
        _log("[usbdrive] bad SD geometry: sectorSize=%u count=%u\n",
             (unsigned)sectorSize, (unsigned)sectorCount);
        return false;
    }

    _log("[usbdrive] SD raw: %u sectors x %u bytes (%.1f MB)\n",
         (unsigned)sectorCount, (unsigned)sectorSize,
         (double)sectorCount * sectorSize / (1024.0 * 1024.0));

    msc.vendorID("MicroJrnl");
    msc.productID("SD Card");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);

    if (!msc.begin((uint32_t)sectorCount, sectorSize))
    {
        _log("[usbdrive] MSC begin failed\n");
        return false;
    }

    USB.begin();

    // remember the SD size (MB) for the status screen
    app["usbDriveSizeMB"] = (int)((double)sectorCount * sectorSize / (1024.0 * 1024.0));
    app["usbDriveStatus"] = "WAITING";

    started = true;
    _log("[usbdrive] mounted SD as USB drive\n");
    return true;
}

void usbdrive_loop()
{
    if (!started)
        return;

    JsonDocument &app = status();

    // Derive the display status from the latched flags. EJECTED wins and is
    // sticky: once the user ejects on the PC we keep showing "safe to unplug"
    // and ignore any stray probe reads the OS issues afterward (those were
    // flipping the screen back to "connected").
    const char *s;
    if (ejected)
    {
        s = "EJECTED";
    }
    else if (!sawTransfer)
    {
        s = "WAITING"; // enumerated but the host hasn't read/written yet
    }
    else if (millis() - lastRwMs < 400)
    {
        s = "TRANSFERRING"; // read/write within the last 400 ms
    }
    else
    {
        s = "MOUNTED"; // connected, idle
    }

    // Only write when it changes, so we don't churn the JSON doc every loop.
    static const char *prev = "";
    if (strcmp(s, prev) != 0)
    {
        prev = s;
        app["usbDriveStatus"] = s;
    }
}

#endif
