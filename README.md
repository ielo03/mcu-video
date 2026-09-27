# mcu-video

## Goal

This project explores how quickly a Raspberry Pi Pico can update a 320 × 480 SPI display without visible tearing. The current full-screen color test achieves approximately **18.70 full-screen updates per second** after reducing blanking and retuning the scanline trigger, with no visible tearing in the tested pattern.

The performance summary compares the experiments; the sections that follow explain how faster transfers exposed tearing and how changing write direction and synchronization resolved it in the tested pattern.

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
- **Refresh Rate:** Up to 60 Hz; measured ~29.3 Hz in the earlier maximum-porch experiment; not independently remeasured for the latest settings
- **Framebuffer Storage:** On-display GRAM (Supports writing to regions not just the full screen)

## Performance summary

Two RAM buffers are reused alternately. Each holds 76,800 bytes: one quarter of the 320 × 480 screen in RGB565 format (two bytes per pixel). A full-screen update requires four buffer sends, not four allocated buffers. FPS here means uploaded images per second; panel refresh rate means how often the display scans its own image memory. Pixel-send times exclude synchronization waits. Estimated transfer rates below are not measured tear-free update rates; partial-screen updates are labeled separately.

| Step                                                     | Actual SPI clock | Send time per buffer |                 Pixel-send time per update | Update rate / result                                                                                      |
| -------------------------------------------------------- | ---------------: | -------------------: | -----------------------------------------: | --------------------------------------------------------------------------------------------------------- |
| Individual two-byte pixel writes                         |    Nominal 1 MHz |         Not measured | ≥2,457.6 ms, full screen (wire-time bound) | ≤0.407 FPS theoretical; no end-to-end benchmark                                                           |
| Two reusable buffers, blocking 8-bit SPI                 |       15.625 MHz |            46.729 ms |                    186.916 ms, full screen | ~5.35 FPS estimated, excluding overhead                                                                   |
| DMA, requested 20 MHz                                    |       15.625 MHz |           ~46.697 ms |                   ~186.788 ms, full screen | ~5.33 FPS estimated with command/logging overhead; 0.07% shorter sends                                    |
| DMA, requested 40 MHz                                    |        31.25 MHz |            ~23.35 ms |                     ~93.40 ms, full screen | ~10.59 FPS estimated with overhead                                                                        |
| DMA, requested 80 MHz                                    |         62.5 MHz |           ~11.676 ms |                    ~46.704 ms, full screen | ~20.96 FPS estimated with overhead; visible tearing                                                       |
| DMA, requested 160 MHz                                   |         62.5 MHz |            ~11.68 ms |                     ~46.72 ms, full screen | ~20.95 FPS estimated; no clock or throughput gain                                                         |
| Scan-synchronized half-screen, 8-bit SPI                 |         62.5 MHz |           ~11.676 ms |                    ~23.352 ms, half screen | ~29.3 half-screen updates/s; no visible tearing at the tuned start point                                  |
| 16-bit SPI + 16-bit DMA, three-buffer test               |         62.5 MHz |           ~10.755 ms |                 ~32.265 ms, three quarters | ~29.2 partial-screen updates/s (~34.21 ms loop); significant tearing before the direction/start-phase fix |
| Reversed row order + plateau-end start, four buffers |     62.5 MHz |       ~10.755 ms |                 ~43.02 ms, full screen | ~14.55 FPS (~68.75 ms loop); no visible tearing in the full-screen color test                         |
| **Reduced blanking + retuned scanline start, four buffers** | **62.5 MHz** | **~10.755 ms** | **~43.02 ms, full screen** | **~18.70 FPS (~53.481 ms loop); no visible tearing in the full-screen color test** |

The latest full-frame loop averages **53.481 ms (~18.70 FPS)**, including approximately **43.02 ms of pixel transfers**, **9.70 ms waiting for synchronization**, and **0.76 ms of other overhead**. The earlier maximum-porch result spent approximately 24.87 ms waiting and uploaded one frame every two measured 34.17 ms panel cycles. The latest upload timing alone does not independently establish the panel refresh rate.

Supporting measurements:

| Measurement                               |                               Earlier result |                                              Later result |
| ----------------------------------------- | -------------------------------------------: | --------------------------------------------------------: |
| Buffer fill on core 1                     | ~0.81 ms per buffer in the blocking baseline |             ~0.870–0.940 ms in the latest captured full-screen run |
| Pixel transfer throughput at 62.5 MHz SPI |               ~52.62 Mbit/s with 8-bit words |       ~57.13 Mbit/s with 16-bit words; 7.9% shorter sends |
| SPI wire utilization                      |                      ~84.2% with 8-bit words |                                  ~91.4% with 16-bit words |
| Switch to read baud                       |                                   117–124 µs |    Retained benchmark; not remeasured for each later step |
| Restore write baud                        |                                     81–82 µs |    Retained benchmark; not remeasured for each later step |
| Wait return to software write-start point |                                     83–84 µs | Retained benchmark from the synchronization investigation |

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

Increasing the requested SPI clock improved transfer throughput until the hardware reached an actual 62.5 MHz clock. Requesting 160 MHz produced the same clock as requesting 80 MHz, so it provided no further gain. The [performance summary](#performance-summary) collects the measurements for each setting.

Across the tested settings, estimated full-screen throughput rose from about 5.33 to 21 FPS. These estimates include command/logging overhead but not scan synchronization. Faster transfers alone did not establish tear-free output.

## Tearing investigation

### Why faster SPI initially looked worse

I expected to encounter tearing and display-timing issues because SPI writes and the panel's refresh scan run independently. Initially, though, slow SPI transfers masked the tearing. Once we increased the SPI speed, I was confused by how much worse the display looked despite the higher throughput. At a requested 1 MHz, no tear had been visible in the test; faster transfers made the boundary between old and new image data much more apparent.

Slow-motion viewing showed that boundary moving through the screen, with pixels appearing to update out of order even though the final image became correct. This suggested that the panel was scanning GRAM while we were replacing its contents. Completing a DMA transfer does not present a frame atomically, and the controller has no documented second GRAM framebuffer that we can swap into view.

### Establishing a scanline timing reference

The board's normal connector did not appear to expose the controller's tearing-effect (TE) synchronization output, so we investigated the Get Scanline command (`GSCAN`, `0x45`) over SPI. The display is wired separately to match `src/config.hpp`, with MISO on GPIO 16.

We inspected the raw response bytes in hexadecimal and binary. The current experimental decoder treats the capture as one dummy bit, sixteen data bits, and seven trailing bits:

```cpp
scanline = (captured_24_bits >> 7) & 0xFFFF;
```

A separate fixed-register test read pixel format `0x55` correctly 1,000 times with no mismatches, supporting basic readback reliability. An independent [TFT_eSPI investigation](https://github.com/Bodmer/TFT_eSPI/issues/731) also found SPI scanline-read behavior that differed from the datasheet; its extra command clock and trailing dummy-byte sequence remain useful leads.

To extend the non-visible intervals around the panel scan (the vertical front and back porches), we unlocked command set 2 and increased both vertical front and back porches to `0xFF` (255), their documented maximum. Unlocking the extended registers initially left the screen black, so we needed to return the initialization settings to documented defaults before continuing the porch experiments.

With the extended porches, analysis of 32,911 consecutive `GSCAN` samples found this repeating pattern:

```text
0 … 224 → sustained 1s → 257 … 511 → wrap to low values
```

Across 115 sustained runs of `1`, the last sampled value before the plateau was 219–224 and the first afterward was 257–262, with some values skipped by polling. A local timestamped capture (`scanline_timing_log.txt`, not committed to the repository) then established the timing:

| Measurement                                |                Result |
| ------------------------------------------ | --------------------: |
| Cycles analyzed without large capture gaps |                   172 |
| Median cycle period                        | 34.166 ms (~29.27 Hz) |
| Median plateau exit to next plateau entry  |             16.686 ms |
| Typical sustained `1` interval             |             ~17.46 ms |

We have not investigated enough to predict the plateau duration from the controller settings, explain the gap between 224 and 257, or explain why readings now reach 511. The number of repeated `1` samples also depends on polling speed. For now, this is a measured timing reference rather than a complete model of physical scan position or vertical blanking.

### Making polling responsive

Reading `GSCAN` required switching from the faster SPI write baud to a slower read baud. Initially, we switched back to write speed after every read, making repeated polling unnecessarily slow. We optimized this by staying at read speed while polling and restoring write speed only when ready to send pixels.

We measured **117–124 µs** to switch to read baud, **81–82 µs** to restore write baud, and approximately **83–84 µs** from the wait returning to the software write-start point. These measurements let us account for the delay between detecting a scanline and beginning the write sequence, rather than assuming transmission started immediately.

Diagnostics also affected timing. We used buffered capture followed by printing to avoid slowing individual reads, and immediate timestamped printing to observe longer cycles. Buffered batches have gaps between them, while immediate printing paces the reads. Both forms of logging had to be removed from the critical drawing path before evaluating synchronization.

### Finding and verifying the write-start point

Synchronizing updates to a repeatable point in the `GSCAN` cycle made the tear appear at a consistent location instead of drifting. Adjusting the delay before the pixel transfer then moved the boundary predictably through the screen. This gave us direct feedback for finding a working update window.

Simply accepting a reading of `1` was insufficient because it could occur late in the plateau. We instead tested delays relative to the plateau's end, increasing the delay by 1 ms every three seconds. **17–19 ms showed no visible tearing in the tested half-screen region**. Around 20 ms, the boundary reappeared at the bottom and moved upward again. The timestamped logs place that successful range approximately 0.4–2.4 ms into the next plateau.

Combining those observations with the baud-switch and write-start measurements let us calculate an earlier scanline trigger. We swept the threshold upward from 200 and empirically verified **222** as the working return point for the tested half-screen updates. That version of `wait_for_blanking_with_gap(222)` first established the advancing low range, then returned when it reached or crossed the threshold. This replaced the arbitrary delay with a starting point derived from measurements and checked on the display.

The consistent tear location and its predictable movement with delay strongly support a timing conflict between GRAM writes and panel scanning as the cause. The two-buffer test worked at this start point, but adding a third produced visible tearing. Fitting transfers into a measured refresh period was not sufficient; their timing relative to the scan and their write direction also mattered.

A separate transaction issue also surfaced during testing: reading `GSCAN` between pixel buffers interrupted the active `RAMWR` stream. Consecutive buffers must remain in the same pixel-write transaction, with readback afterward. Issuing a new `RAMWR` restarts at the beginning of the configured address window.

## Full-screen tearing fix

We fixed the visible tearing in the full-screen color test by reversing the pixel row-address interpretation and changing when writes begin. The active `MADCTL` setting changed from `0x88` to `0x08`, reversing the vertical write direction while keeping the refresh-direction bits unchanged. This is a row-order flip, rather than a full 180-degree rotation of both image axes.

In the initial full-screen fix, we used the end of the repeated `1` readings as the write-start reference, which we interpret as the beginning of the next scan. `wait_for_blanking_with_gap(257)` observes that plateau and returns on **257 or 258**; if polling misses the window, it retries at the next plateau. All four quarter-screen buffer sends then stream through one uninterrupted `RAMWR` transaction.

The working timing model is that writes begin **behind the current scanline**. The panel reads the old image before those rows are replaced. Writing then finishes **ahead of the next refresh's scanline**, allowing that scan to read the new image. The requirement is to update each row between its old-image and new-image reads, rather than to fit the entire transfer inside blanking or one refresh period. The observed result is **no visible tearing in the tested full-screen pattern**; the detailed mapping from GSCAN values to physical rows remains unverified.

That configuration uploaded **one full frame every two panel refreshes**. Although the update rate is lower than the earlier half-screen experiment, the complete image without a visible tear looks much better to the user.

### Pixel transfer implementation

We changed pixel transfers from **8-bit SPI frames and 8-bit DMA transfers to 16-bit SPI frames and 16-bit DMA transfers** to improve transfer speed. At the same actual 62.5 MHz SPI clock, this reduced the measured time per quarter-screen buffer from **11.676 ms to approximately 10.755 ms**, a **7.9% reduction**. Each transfer now carries one native RGB565 pixel with MSB-first transmission; the total number of pixel bits sent is unchanged. Commands and register reads use 8-bit frames. A shared format flag avoids reconfiguring SPI when its width is already correct. Measured timings and comparisons are collected in the [performance summary](#performance-summary).

The synchronization wait aligns the next write with the chosen phase; removing it can bring tearing back. The earlier half-screen result was a successful configuration, not a hardware limit on tear-free image size. The full-screen result depends on both the new write direction and the new start phase.

### Optimizing one upload every two refreshes

A full 320 × 480 RGB565 image contains 2,457,600 bits. At the current actual SPI clock of 62.5 MHz, transmitting those bits takes **at least 39.322 ms**, even with no gaps or software overhead. Our measured full-frame pixel transfer is approximately **43.02 ms**.

The measured panel refresh period is approximately **34.17 ms even with the vertical front and back porches set to their maximum values**. At this SPI clock and full-frame payload, we therefore cannot upload a new image every panel refresh through timing or software optimization alone. Slowing the screen's internal clock could make each refresh long enough; alternatively, increasing the actual transfer bandwidth or sending less pixel data would change the constraint.

Instead, we kept the screen clock and established a tear-free full-frame update every **two refreshes**, initially taking approximately **68.75 ms (14.55 FPS)** per upload.

### Reducing blanking and retuning the scanline trigger

When I made the blanking area smaller, its reported position in the sequence of scanline values changed. The old plateau-exit trigger around 257 no longer matched the new sequence. I adjusted the synchronization point for that change and continued tweaking the porch settings and scanline trigger until I had just about the smallest blanking period I could achieve while maintaining **no visible tears in the full-screen color test**.

The current source uses porch parameters `0xFF, 0x21, 0x00, 0x04` for command `0xB5` and `wait_for_blanking_with_gap(35)`, which polls for 35 or 36. Pixel transfers still use four quarter-screen sends with 16-bit SPI and DMA at an actual 62.5 MHz.

The latest capture gives these results:

| Measurement | Result |
| --- | ---: |
| Wait-start intervals | 53,510 / 53,408 / 53,524 µs |
| Average full-frame interval | **53,480.7 µs (53.481 ms)** |
| Full-screen upload rate | **18.70 FPS** |
| Pixel send per strip | 10,754–10,756 µs |
| Pixel sends per full frame | ~43.02 ms |
| Synchronization wait | 9,625–9,749 µs; ~9.70 ms average |
| Other loop overhead (by subtraction) | ~0.76 ms |
| Buffer fill on core 1 | 870–940 µs per strip, overlapping transfers |

The frame rate is calculated as `1,000,000 / mean(wait-start interval)`, using the three captured intervals. This is approximately **28.5% faster** than the earlier 14.55 FPS result, with essentially unchanged pixel transfer time. The gain comes from reducing time outside the pixel transfers, primarily the synchronization wait. These measurements describe upload cadence, not the blanking duration itself or an independent measurement of the panel refresh rate.

### Why synchronization still leaves about 10 ms idle

The working diagnosis is that repeating the same blanking interval on every refresh accounts for most of the remaining synchronization wait, despite tuning the current fixed-porch setup close to its tear-free limit. The safe write-start point appears to have only about two scanlines of margin after blanking ends. That narrow margin describes where an upload can start, not how long the previous upload must wait for that point to return.

In this model, the first blanking interval provides the timing margin needed to write behind one scan and finish ahead of the next. Fixed porch settings repeat that allowance during the second refresh as well, even though the writer may no longer need the same amount of blanking at that phase. Shortening both intervals together removes the first interval's needed margin and brings tearing back. The hypothesis is that this repeated allowance explains the vast majority of the remaining idle time, rather than slow software between transfers. It is not yet established that all of the second interval is unnecessary: it may also prevent the next scan from catching unfinished writes.

The measured **9.70 ms wait is not a direct measurement of blanking duration**. Assuming the upload still spans two refreshes, the 53.481 ms upload interval implies a panel period of about 26.740 ms. Two such periods, minus 43.02 ms of transfers and about 0.76 ms of other overhead, leave approximately 9.70 ms waiting for the next safe start. Measuring the scan and blanking phases under these settings is needed to confirm the diagnosis.

It is worth exploring whether controller commands can **adjust blanking live**, giving the first refresh more blanking and the second none, or the minimum the controller permits. If those changes can take effect at the intended boundaries without disturbing the scan, this could preserve the first refresh's timing margin while removing much of the repeated allowance. Sending the extra control commands will probably take substantially less time than the roughly 10 ms currently spent waiting, so command overhead alone is unlikely to erase the potential gain.

This remains an experiment, not a confirmed controller capability or speedup. We need to establish when porch changes take effect, whether alternating them is stable, and how to send the commands while preserving pixel-write position. The existing investigation showed that intervening commands can interrupt the `RAMWR` stream, so the implementation must account for resuming the remaining pixels as well as the command and baud-switch costs. Success should be measured by a shorter complete upload interval with no visible tearing.

### Current source and remaining validation

The current configuration uses `BUFF_NUM = 4` and `wait_for_blanking_with_gap(35)`. Core 0 alternates buffers across updates. Core 1 still selects free buffers A-first, and a loop iteration with neither expected buffer ready sends nothing. Strict producer ordering and counting completed sends remain necessary before treating this as a reliable arbitrary-frame renderer. Moving, detailed imagery should also be tested beyond the solid-color pattern.

Per-buffer fill/send/other timings and optional loop/wait timings remain available. `core0_other` includes synchronization waiting, not just command/logging overhead. Wait-start intervals measure update-loop cadence, currently approximately 53.481 ms per full-screen upload in the supplied capture. Raw serial captures are ignored by default.

## References

- [ST7796S datasheet](https://files.waveshare.com/wiki/common/ST7796S_Datasheet.pdf): SPI timing p. 54; pixel format p. 190; GSCAN p. 196; frame-rate divider pp. 213–214; porch limits p. 217.
- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf): SPI clock division and 4–16-bit word formats, section 4.4.
- [TFT_eSPI GSCAN investigation](https://github.com/Bodmer/TFT_eSPI/issues/731): firsthand SPI read-protocol observations.
- [Raspberry Pi PIO SPI example](https://github.com/raspberrypi/pico-examples/tree/master/pio/spi).
