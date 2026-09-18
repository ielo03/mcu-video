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

### Dual frame-buffer quarter-screen transfers

Using blocking SPI writes with four 76,800-byte strips per 320 × 480 RGB565 frame:

- **Average strip send time:** 46.729 ms across five samples.
- **Estimated full-frame send time:** 186.916 ms (four strips).
- **Estimated throughput:** 5.35 FPS, excluding display-command and logging overhead.
- **Buffer fill time:** Approximately 0.81 ms per strip on the other core; sending was the bottleneck.

### DMA

We added DMA to feed the SPI transmit FIFO directly from RAM instead of having the CPU copy each byte. The aim was to reduce any gaps caused by CPU feeding overhead and allow CPU work to overlap transfers. The current implementation still waits for DMA and SPI completion, so core 0 does not yet use that time for other work.

At the original SPI setting, average strip send time changed from **46.729 ms without DMA** to approximately **46.697 ms with DMA**: only **0.032 ms (0.07%)** faster. This is too small to claim a meaningful performance gain from these samples. The results suggest the blocking implementation already kept SPI supplied; DMA did not materially improve frame rate at the same clock.

### Increasing the SPI clock

With the same four-strip layout and DMA pixel transfers, increasing the requested SPI clock improved measured throughput:

| Requested SPI clock (`SPI_MHZ`) | Actual SPI clock | Strip send time | Estimated FPS including command/logging overhead |
| ------------------------------- | ---------------- | --------------- | ------------------------------------------------ |
| 20 MHz                          | 15,625,000 Hz    | 46.70 ms        | 5.33                                             |
| 40 MHz                          | 31,250,000 Hz    | 23.35 ms        | 10.59                                            |
| 80 MHz                          | 62,500,000 Hz    | 11.68 ms        | 20.96                                            |
| 160 MHz                         | 62,500,000 Hz    | 11.68 ms        | 20.95                                            |

Requesting 80 MHz achieved an actual SPI clock of 62.5 MHz. Raising the request to 160 MHz produced the same actual clock and no further throughput improvement. Compared with the 20 MHz request, estimated frame rate increased from about 5.33 to 21 FPS (approximately 3.9×).

FPS is calculated as `1,000,000 / (4 × (average core0_send + average core0_other))`, using microsecond timings. These are transfer-throughput estimates, not measurements of the panel's refresh rate. DMA alone had little effect at the original SPI clock; the higher actual clock produced the speedup.

## Visual observations

Viewing the screen in slow motion revealed visible tearing: pixels appeared to update in an unexpected order rather than as a clean, sequential frame update. The throughput measurements above therefore do not establish tear-free output.

Reducing the SPI setting to 1 MHz showed no visible tearing in the test. At higher speeds, all pixels eventually displayed correctly. Together, these observations suggest a mismatch between the timing of display-memory updates and the panel's refresh scan, rather than corrupted SPI pixel data. This is the working hypothesis, not a confirmed diagnosis; a correct final image does not by itself rule out transfer or ordering issues.

The cause has not yet been confirmed. Possible contributors include the panel scanning display memory while SPI updates it, the current A-first buffer scheduling allowing chunks to be sent out of order, and the camera's own rolling-shutter timing. The slow-motion recording alone does not establish the actual order of SPI pixel writes.
