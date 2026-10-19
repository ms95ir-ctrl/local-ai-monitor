# ESP32-S3 DevKitC + ST7796 variant (480x320)

The firmware running on my AI box. An `esp32-s3-devkitc-1` drives a 480x320 ST7796 display with an
FT6336 touch controller. Pins are in `platformio.ini` and the top of `src/main.cpp`; the wiring table is
in the [main README](../../README.md#hardware).

```bash
pio run -t upload
```
