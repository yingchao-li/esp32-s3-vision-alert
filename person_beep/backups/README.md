# Original firmware backup taken before flashing

`original_first_1MB.bin` is the first 1MB of the original flash, containing the original bootloader, partition table, NVS/OTA metadata and the complete app0; it is not a full 8MB flash or all-data-partition backup.

app0 was located from the original partition table and extracted as `original_app_10000.bin` (307,328 bytes), verified with esptool checksums and Validation Hash. The original OTA sequence numbers are 1/0, corresponding to app0. See `manifest.json` and `original_app_info.txt`.

Recover the original buzzer firmware (from the project root, with serial monitors closed):

```sh
source scripts/env.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem5C930528211 -b 115200 write_flash 0x0 backups/original_first_1MB.bin
```

This restores the original partition table and app0. The old app1 and SPIFFS are not in this backup, so full device-data recovery cannot be promised. You can also re-upload the unmodified `../ESP32_USB_Camera.ino` from the Arduino IDE.

Original source SHA256 (before the English-only comment pass): `0381a6798c5ba6931b9b8864fdfeac933d4ca5a45d282bda1db7e9a33213142a`. After translating only the sketch comments, the current `../ESP32_USB_Camera.ino` is `e76f980331ba396bad5d8c96f095f6b5fd27437901d54272d4355645c7e3cf1a`; the code is unchanged.
