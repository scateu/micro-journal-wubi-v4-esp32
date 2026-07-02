DEMO:
 - <https://youtube.com/shorts/lltQv1eadMk>
 - <https://www.youtube.com/watch?v=CoNwv29xYIk>


INSTALL:

    cd micro-journal-rev-4-esp32
    #pio run -e cardputer-adv -t upload
    make help
    make upload-wubi
    #make upload-pinyin
    #make upload-shuangpin


Additional Features:
 - Readline Keybindings: C-n C-p C-k C-y M-f M-b C-b C-d
 - External USB keyboard support
 - C-space switch IME
 - C-s Save
 - Ctrl -/+ =/_  increase decrease font size.
 - Turn on while pressing `e` key to enter USB Drive Mode, to copy off txt files.

Features from upstream:
 - Fn-0..9 switch txt file

Burn manually:

    brew install esptool
    See FIRMWARE_FLASH.md
    esptool --chip auto --port /dev/tty.usbmodem21201 --baud 1500000 --before default_reset write_flash -z 0x10000 firmware-wubi.bin  # seems not enough

