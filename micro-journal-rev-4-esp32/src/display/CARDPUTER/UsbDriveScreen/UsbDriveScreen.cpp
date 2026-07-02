#include "UsbDriveScreen.h"
#include "app/app.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"

//
void UsbDriveScreen_setup()
{
    M5Cardputer.Display.fillRect(
        0, 0,
        M5Cardputer.Display.width(),
        M5Cardputer.Display.height(),
        TFT_BLACK);
}

//
// Big, unmistakable "export mode" screen while the SD is exposed over USB.
// The title/instructions are drawn once; only the status line is repainted
// (change-detected) so it never flickers.
void UsbDriveScreen_render()
{
    JsonDocument &app = status();
    const int W = M5Cardputer.Display.width();

    // Title + static info, drawn once after setup's clear.
    static bool titleDrawn = false;
    if (!titleDrawn)
    {
        // Big word - fills most of the screen width.
        M5Cardputer.Display.setTextDatum(top_center);
        M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5Cardputer.Display.setFont(&fonts::Orbitron_Light_24);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.drawString("USB", W / 2, 4);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.drawString("DRIVE MODE", W / 2, 52);

        // SD size.
        int sizeMB = app["usbDriveSizeMB"].as<int>();
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.drawString(
            String("TF card: ") + sizeMB + " MB", W / 2, 74);

        // Instruction.
        M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5Cardputer.Display.drawString("Eject on PC, then reboot to edit", W / 2, 122);

        M5Cardputer.Display.setTextDatum(top_left);
        titleDrawn = true;
    }

    // Status line, repainted only when it changes.
    static String status_prev = "";
    String s = app["usbDriveStatus"].as<String>();
    if (s.length() == 0)
        s = "WAITING";
    if (s != status_prev)
    {
        status_prev = s;

        String label = s;
        uint16_t color = TFT_YELLOW;
        if (s == "MOUNTED")
        {
            label = "CONNECTED";
            color = TFT_GREEN;
        }
        else if (s == "TRANSFERRING")
        {
            label = "TRANSFERRING";
            color = TFT_CYAN;
        }
        else if (s == "EJECTED")
        {
            label = "SAFE TO UNPLUG";
            color = TFT_DARKGREY;
        }
        else // WAITING
        {
            label = "WAITING FOR PC";
        }

        M5Cardputer.Display.fillRect(0, 92, W, 22, TFT_BLACK);
        M5Cardputer.Display.setTextDatum(top_center);
        M5Cardputer.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5Cardputer.Display.setTextColor(color, TFT_BLACK);
        M5Cardputer.Display.drawString(label, W / 2, 94);
        M5Cardputer.Display.setTextDatum(top_left);
    }
}
