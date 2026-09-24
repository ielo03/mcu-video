# mcu-video

## Goal

This project is a challenge to achieve the highest practical frame rate with the hardware below, while keeping display updates visually coherent.

### MCU

- **Board:** Raspberry Pi Pico
- **MCU:** RP2040
- **CPU:** Dual-core Arm Cortex-M0+, rated up to 133 MHz; these SPI measurements used a 125 MHz peripheral clock
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

## Improving transfer performance

### Starting point: one pixel at a time at 1 MHz

The initial implementation sent each RGB565 pixel with a separate two-byte SPI write. A full 320 × 480 screen contains 153,600 pixels, or 2,457,600 bits. At a nominal 1 MHz SPI clock, the pixel data alone takes **2.458 seconds per screen**, giving a theoretical ceiling of **~0.407 FPS**—about one full-screen update every 2.5 seconds.

Actual throughput would have been lower because of the overhead and gaps from 153,600 separate write calls, plus command setup. We did not record an end-to-end benchmark for that version, so this is a calculated upper bound, not a measured frame rate.

### Baseline: two quarter-screen buffers

Core 1 fills two reusable 76,800-byte buffers while core 0 sends them. Four strips make one 320 × 480 RGB565 frame. With blocking SPI writes at an actual 15.625 MHz, the initial measurements were:

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

These FPS figures estimate full-screen transfer throughput from four strip sends plus measured command/logging overhead. They do not measure the panel refresh rate or establish tear-free output.

## Tearing investigation

### Why faster SPI initially looked worse

I expected to encounter tearing and display-timing issues because SPI writes and the panel's refresh scan run independently. Initially, though, slow SPI transfers masked the tearing. Once we increased the SPI speed, I was confused by how much worse the display looked despite the higher throughput. At a requested 1 MHz, no tear had been visible in the test; faster transfers made the boundary between old and new image data much more apparent.

Slow-motion viewing showed that boundary moving through the screen, with pixels appearing to update out of order even though the final image became correct. This suggested that the panel was scanning GRAM while we were replacing its contents. Completing a DMA transfer does not present a frame atomically, and the controller has no documented second GRAM framebuffer that we can swap into view.

### Establishing a scanline timing reference

The board's normal connector did not appear to expose the controller's TE output, so we investigated the Get Scanline command (`GSCAN`, `0x45`) over SPI. The display is wired separately to match `src/config.hpp`, with MISO on GPIO 16.

We inspected the raw response bytes in hexadecimal and binary. The current experimental decoder treats the capture as one dummy bit, sixteen data bits, and seven trailing bits:

```cpp
scanline = (captured_24_bits >> 7) & 0xFFFF;
```

A separate fixed-register test read pixel format `0x55` correctly 1,000 times with no mismatches, supporting basic readback reliability. An independent [TFT_eSPI investigation](https://github.com/Bodmer/TFT_eSPI/issues/731) also found SPI scanline-read behavior that differed from the datasheet; its extra command clock and trailing dummy-byte sequence remain useful leads.

To give pixel transfers more time, we unlocked command set 2 and increased both vertical front and back porches to `0xFF` (255), their documented maximum. Unlocking the extended registers initially left the screen black, so we needed to return the initialization settings to documented defaults before continuing the porch experiments.

With the extended porches, analysis of 32,911 consecutive `GSCAN` samples found this repeating pattern:

```text
0 … 224 → sustained 1s → 257 … 511 → wrap to low values
```

Across 115 sustained runs of `1`, the last sampled value before the plateau was 219–224 and the first afterward was 257–262, with some values skipped by polling. Timestamped captures in `scanline_timing_log.txt` then established the timing:

| Measurement | Result |
| --- | ---: |
| Cycles analyzed without large capture gaps | 172 |
| Median cycle period | 34.166 ms (~29.27 Hz) |
| Median plateau exit to next plateau entry | 16.686 ms |
| Typical sustained `1` interval | ~17.46 ms |

We have not investigated enough to predict the plateau duration from the controller settings, explain the gap between 224 and 257, or explain why readings now reach 511. The number of repeated `1` samples also depends on polling speed. For now, this is a measured timing reference rather than a complete model of physical scan position or vertical blanking.

### Making polling responsive

Reading `GSCAN` required switching from the faster SPI write baud to a slower read baud. Initially, we switched back to write speed after every read, making repeated polling unnecessarily slow. We optimized this by staying at read speed while polling and restoring write speed only when ready to send pixels.

We measured **117–124 µs** to switch to read baud, **81–82 µs** to restore write baud, and approximately **83–84 µs** from the wait returning to the software write-start point. These measurements let us account for the delay between detecting a scanline and beginning the write sequence, rather than assuming transmission started immediately.

Diagnostics also affected timing. We used buffered capture followed by printing to avoid slowing individual reads, and immediate timestamped printing to observe longer cycles. Buffered batches have gaps between them, while immediate printing paces the reads. Both forms of logging had to be removed from the critical drawing path before evaluating synchronization.

### Finding and verifying the write-start point

Synchronizing updates to a repeatable point in the `GSCAN` cycle made the tear appear at a consistent location instead of drifting. Adjusting the delay before the pixel transfer then moved the boundary predictably through the screen. This gave us direct feedback for finding a working update window.

Simply accepting a reading of `1` was insufficient because it could occur late in the plateau. We instead tested delays relative to the plateau's end, increasing the delay by 1 ms every three seconds. **17–19 ms showed no visible tearing in the tested half-screen region**. Around 20 ms, the boundary reappeared at the bottom and moved upward again. The timestamped logs place that successful range approximately 0.4–2.4 ms into the next plateau.

Combining those observations with the baud-switch and write-start measurements let us calculate an earlier scanline trigger. We swept the threshold upward from 200 and empirically verified **222** as the working return point for the tested half-screen updates. The current `wait_for_blanking_with_gap(222)` first establishes the advancing low range, then returns when it reaches or crosses the threshold. This replaced the arbitrary delay with a starting point derived from measurements and checked on the display.

The consistent tear location and its predictable movement with delay strongly support a timing conflict between GRAM writes and panel scanning as the cause. Synchronization solved that conflict for the tested update size; increasing the amount of data eventually exceeded the available time.

A separate transaction issue also surfaced during testing: reading `GSCAN` between pixel buffers interrupted the active `RAMWR` stream. Consecutive buffers must remain in the same pixel-write transaction, with readback afterward. Issuing a new `RAMWR` restarts at the beginning of the configured address window.

## Results and current limits

The successful result was **~29.3 half-screen updates per second**, synchronized once per measured panel cycle. The SPI bandwidth explains why half-screen was the largest tested working size using whole quarter-screen buffers:

| Pixel payload | Transfer time at 62.5 MHz actual SPI |
| --- | ---: |
| Quarter screen, 76,800 bytes | 11.676 ms |
| Half screen | 23.352 ms, plus gaps |
| Three quarters | 35.028 ms, plus gaps |
| Full screen, 307,200 bytes | 46.704 ms, plus gaps |

Two buffers fit within the **34.166 ms refresh cycle** and worked without visible tearing at the tuned starting point. A third buffer already exceeds that cycle before command or software overhead. Synchronization chooses when the writes begin; it cannot make SPI deliver them faster.

Half-screen is not an exact 50% hardware limit. Smaller increments could explore the remaining margin: the measured transfer rate could carry roughly **73% of a screen per cycle** before overhead. Fitting the bytes into a cycle is necessary for sustained full updates each refresh, but avoiding tearing also requires the writes not to cross the panel's scan through the updated region.

### Current source and retained benchmarks

**The current source attempts four quarter-buffer sends after each wait**, retaining the full-screen bandwidth experiment. This differs from the successful half-screen test and is not a claim of tear-free full-screen output. Repeated writes beginning at the start of a full-screen window update the same region unless the address window or write position is deliberately advanced.

The buffers are reused as core 1 refills them. A-first selection can reorder ready buffers, and a loop iteration with neither buffer ready sends nothing. Enforcing producer order and counting completed sends remain necessary before treating this as a reliable arbitrary-frame renderer.

Per-buffer fill/send/other timings and optional loop/wait timings remain available, with their print statements commented out. `core0_other` includes synchronization waiting, not just command/logging overhead. Wait-start intervals measure loop cadence and represent panel cadence only when the loop synchronizes once per refresh. Legacy capture loops have been removed, and raw serial captures are ignored by default.

## Next performance experiments

### Send pixels as 16-bit SPI words

At the actual **62.5 MHz SPI clock**, the theoretical maximum is **62.5 Mbit/s (7.8125 MB/s)** with no gaps. Sending 76,800 bytes in 11.676 ms currently achieves approximately **52.62 Mbit/s (6.58 MB/s)** during pixel transfers—**84.2% utilization**. About **15.8% of the measured transfer time** is therefore beyond the ideal wire time, leaving room to investigate gaps between SPI words and other transfer overhead. This excludes the additional time spent waiting for synchronization between updates.

The current DMA and SPI path sends 8-bit words. Test **16-bit SPI words with 16-bit DMA transfers** for pixel payloads, returning to 8-bit transactions for commands and register reads. Keep the pixel bit count unchanged and preserve high-byte-first wire order; blindly reinterpreting the existing byte array as little-endian `uint16_t` values would swap the bytes. DMA transfer counts must become pixel/word counts rather than byte counts.

The goal is fewer gaps between words and less FIFO/DMA work per pixel; the transmitted bit count stays the same. Even perfect efficiency at the current clock gives a **39.322 ms** full-frame wire time, still longer than the current refresh cycle. This optimization therefore needs measurement and may need to be combined with slower panel refresh. PIO-generated SPI is another option for reducing inter-word gaps.

### Slow the panel's internal clock from /1 to /2

`B1` currently uses `{A0, 10}`. Its DIVA field selects the internal-clock divider; `{A1, 10}` selects **/2 instead of /1**. If measured timings scale accordingly, the cycle would increase from ~34.2 to **~68.3 ms**, lowering refresh from ~29.3 to **~14.6 Hz**.

That would provide enough total cycle time for a **~46.7 ms full-screen transfer**, making one complete uploaded frame per slower refresh feasible on bandwidth grounds. It trades temporal smoothness for the possibility of better visual continuity: a coherent full-screen image rather than partial-screen updates spread over refreshes.

This is not an automatic tearing fix. The estimated long interval would only increase from ~17.5 to **~34.9 ms**, still shorter than a full transfer, so writing across the scan cycle would need correct scheduling. The divider also slows visible scanning; it does not exclusively extend blanking. Re-measure the scanline sequence and retune the threshold after changing it.

## References

- [ST7796S datasheet](https://files.waveshare.com/wiki/common/ST7796S_Datasheet.pdf): SPI timing p. 54; pixel format p. 190; GSCAN p. 196; frame-rate divider pp. 213–214; porch limits p. 217.
- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf): SPI clock division and 4–16-bit word formats, section 4.4.
- [TFT_eSPI GSCAN investigation](https://github.com/Bodmer/TFT_eSPI/issues/731): firsthand SPI read-protocol observations.
- [Raspberry Pi PIO SPI example](https://github.com/raspberrypi/pico-examples/tree/master/pio/spi).
