esptool.py --chip esp32s3 --port /dev/tty.usbmodem* --baud 921600 \
	write_flash -z \
	--flash_mode dio --flash_freq 80m --flash_size 8MB \
	0x0000  bootloader.bin \
	0x8000  partitions.bin \
	0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
	0x10000 firmware.bin
