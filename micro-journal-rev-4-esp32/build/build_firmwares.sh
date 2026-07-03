#cp ../.pio/build/cardputer-adv/bootloader.bin .
#cp ../.pio/build/cardputer-adv/partitions.bin .
#cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin .

echo "firmware-pinyin.bin firmware-wubi.bin firmware-shuangpin.bin cannot be flashed into Cardputer directly."
echo "Combining them with bootloader.bin + partitions.bin + boot_app0.bin"

cd ..

echo "Build ../firmware-pinyin.bin ../firmware-wubi.bin ../firmware-shuangpin.bin"
make firmware-pinyin.bin
make firmware-wubi.bin
make firmware-shuangpin.bin

mv firmware-pinyin.bin firmware-wubi.bin firmware-shuangpin.bin build/

cd build


echo "Generating firmware-wubi.bin ... "
esptool --chip esp32s3 merge-bin -o typewriter-wubi.bin --flash-mode dio --flash-size 8MB 0x0000 ../.pio/build/cardputer-adv/bootloader.bin 0x8000 ../.pio/build/cardputer-adv/partitions.bin 0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 firmware-wubi.bin


echo "Generating firmware-pinyin.bin ... "
esptool --chip esp32s3 merge-bin -o typewriter-pinyin.bin --flash-mode dio --flash-size 8MB 0x0000 ../.pio/build/cardputer-adv/bootloader.bin 0x8000 ../.pio/build/cardputer-adv/partitions.bin 0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 firmware-pinyin.bin

echo "Generating firmware-shuangpin.bin ... "
esptool --chip esp32s3 merge-bin -o typewriter-shuangpin.bin --flash-mode dio --flash-size 8MB 0x0000 ../.pio/build/cardputer-adv/bootloader.bin 0x8000 ../.pio/build/cardputer-adv/partitions.bin 0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 firmware-shuangpin.bin


echo "cleaning up firmware-pinyin.bin firmware-wubi.bin firmware-shuangpin.bin"
rm -i firmware-pinyin.bin firmware-wubi.bin firmware-shuangpin.bin


echo "Now, please send it to your friends .."
echo "esptool --chip auto --port /dev/tty.usbmodem21201 --baud 921600 --before default_reset write_flash -z 0x0 typewriter-wubi.bin"
echo "or .. use M5Burner"
