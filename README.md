# mcu-video

## Goal

This project is a challenge to reach the highest fps I can with the following hardware

### MCU

- **Board:** Raspberry Pi Pico
- **MCU:** RP2040
- **CPU:** Dual-core Arm Cortex-M0+ 133 MHz default
- **SRAM:** 264 KB
- **Flash:** 2 MB QSPI flash

### Display

- **Model:** Waveshare 3.5inch RPi LCD (G)
- **Resolution:** 320 × 480
- **Panel Type:** TFT LCD
- **Interface:** SPI
- **Touch:** Resistive touchscreen
- **Display Controller:** ST7796S
- **Refresh Rate:** Up to 60 Hz
- **Framebuffer Storage:** On-display GRAM (Supports writing to regions not just the full screen)

## Benchmarks

### Before DMA: quarter-screen transfers

Using blocking SPI writes with four 76,800-byte strips per 320 × 480 RGB565 frame:

- **Average strip send time:** 46.729 ms across five samples.
- **Estimated full-frame send time:** 186.916 ms (four strips).
- **Estimated throughput:** 5.35 FPS, excluding display-command and logging overhead.
- **Buffer fill time:** Approximately 0.81 ms per strip on the other core; sending was the bottleneck.
