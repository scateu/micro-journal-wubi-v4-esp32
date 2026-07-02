cp ../.pio/build/cardputer-adv/bootloader.bin .
cp ../.pio/build/cardputer-adv/partitions.bin .
cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin .

#cd ..
#make firmware-pinyin.bin
#make firmware-wubi.bin
#make firmware-shuangpin.bin
ln -sf firmware-pinyin.bin firmware.bin
