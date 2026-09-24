#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico/multicore.h"
#include "hardware/dma.h"

#include <atomic>
#include <cstdio>

#include "st7796.hpp"
#include "config.hpp"

int spi_dma_channel;
dma_channel_config spi_dma_config;

uint8_t a_buff[BUFF_SIZE];
uint8_t b_buff[BUFF_SIZE];

std::atomic<bool> a_buff_written{true};
std::atomic<bool> b_buff_written{true};

uint32_t a_fill_us = 0;
uint32_t b_fill_us = 0;

uint32_t buff_not_ready_count = 0;

void init_gpio() {
    spi_init(LCD_SPI, SPI_MHZ * 1000 * 1000);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);

    gpio_init(PIN_BL);
    gpio_set_dir(PIN_BL, GPIO_OUT);

    gpio_put(PIN_BL, 1);
}

void init_dma() {
    spi_dma_channel = dma_claim_unused_channel(true);
    spi_dma_config = dma_channel_get_default_config(spi_dma_channel);

    channel_config_set_transfer_data_size(&spi_dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&spi_dma_config, true);
    channel_config_set_write_increment(&spi_dma_config, false);
    channel_config_set_dreq(&spi_dma_config, spi_get_dreq(LCD_SPI, true));
}

void send_pixels_dma(const uint8_t* buffer, size_t bytes) {
    ensure_write_baud();
    dma_channel_configure(
        spi_dma_channel,
        &spi_dma_config,
        &spi_get_hw(LCD_SPI)->dr,
        buffer,
        bytes,
        true
    );

    dma_channel_wait_for_finish_blocking(spi_dma_channel);

    // DMA has finished feeding SPI; wait for the last bits to leave.
    while (spi_is_busy(LCD_SPI)) {
        tight_loop_contents();
    }

    // Discard received bytes and clear receive overrun, as the
    // SDK's write-only SPI function does.
    while (spi_is_readable(LCD_SPI)) {
        (void)spi_get_hw(LCD_SPI)->dr;
    }
    spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
}

void core1_main() {
    uint8_t colors[8][2] = {{0xF8, 0x00}, {0x07, 0xE0}, {0x00, 0x1F}, {0xFF, 0xFF}, {0x00, 0x00}, {0xFF, 0xE0}, {0x07, 0xFF}, {0xF8, 0x1F}};

    uint8_t color = 0;
    uint8_t div_num = 0;
    while (true) {
        if (a_buff_written) {
            const uint32_t fill_start = time_us_32();
            for (int i = 0; i < BUFF_SIZE / 2; ++i) {
                a_buff[i * 2] = colors[color][0];
                a_buff[(i * 2) + 1] = colors[color][1];
            }
            a_fill_us = time_us_32() - fill_start;
            a_buff_written = false;
            ++div_num;
            if (div_num == 4) {
                div_num = 0;
                ++color;
                if (color == 8) color = 0;
            }
        } else if (b_buff_written) {
            const uint32_t fill_start = time_us_32();
            for (int i = 0; i < BUFF_SIZE / 2; ++i) {
                b_buff[i * 2] = colors[color][0];
                b_buff[(i * 2) + 1] = colors[color][1];
            }
            b_fill_us = time_us_32() - fill_start;
            b_buff_written = false;
            ++div_num;
            if (div_num == 4) {
                div_num = 0;
                ++color;
                if (color == 8) color = 0;
            }
        }
    }
}

int main() {
    stdio_init_all();

    multicore_launch_core1(core1_main);

    init_gpio();

    init_dma();

    init_display();

    // Measure all steady-state core 0 time outside the pixel-send call.
    // Each interval includes the previous log and the next command setup.
    uint64_t other_start = time_us_64();
    uint8_t div_num = 0;

    send_command(ST7796Command::CASET);
    send_data({0x00, 0x00, 0x01, 0x3F});

    send_command(ST7796Command::RASET);
    send_data({0x00, 0x00, 0x01, 0xDF});

    send_command(ST7796Command::RAMWR);
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    uint64_t previous_wait_start = 0;
    bool have_previous_wait = false;
    while (true) {
        const uint64_t wait_start = time_us_64();
        const bool frame_interval_valid = have_previous_wait;
        const uint64_t frame_interval_us = frame_interval_valid
            ? wait_start - previous_wait_start : 0;
        previous_wait_start = wait_start;
        have_previous_wait = true;
        wait_for_blanking_with_gap(222); // read-baud swap 117-124us
        const uint64_t after_wait_us = time_us_64();
        const uint64_t wait_us = after_wait_us - wait_start;
        reset_write_baud(); // write-baud swap 81-82us

        send_command(ST7796Command::RAMWR);
        gpio_put(PIN_DC, 1);
        gpio_put(PIN_CS, 0);

        for (int o = 0; o < 4; o++) {
            if (!a_buff_written) {
                const uint32_t fill_us = a_fill_us;
                const uint64_t send_start = time_us_64();
                const uint64_t other_us = send_start - other_start;
                send_pixels_dma(a_buff, BUFF_SIZE);
                const uint64_t send_end = time_us_64();
                const uint64_t send_us = send_end - send_start;
                other_start = send_end;
                a_buff_written = true;
//                 std::printf("buffer A strip %u: fill=%lu us core0_send=%llu us core0_other=%llu us bytes=%u\n",
//                             static_cast<unsigned>(div_num),
//                             static_cast<unsigned long>(fill_us),
//                             static_cast<unsigned long long>(send_us),
//                             static_cast<unsigned long long>(other_us),
//                             static_cast<unsigned>(BUFF_SIZE));
                ++div_num;
                if (div_num == 4) div_num = 0;
            } else if (!b_buff_written) {
                const uint32_t fill_us = b_fill_us;
                const uint64_t send_start = time_us_64();
                const uint64_t other_us = send_start - other_start;
                send_pixels_dma(b_buff, BUFF_SIZE);
                const uint64_t send_end = time_us_64();
                const uint64_t send_us = send_end - send_start;
                other_start = send_end;
                b_buff_written = true;
//                 std::printf("buffer B strip %u: fill=%lu us core0_send=%llu us core0_other=%llu us bytes=%u\n",
//                             static_cast<unsigned>(div_num),
//                             static_cast<unsigned long>(fill_us),
//                             static_cast<unsigned long long>(send_us),
//                             static_cast<unsigned long long>(other_us),
//                             static_cast<unsigned>(BUFF_SIZE));
                ++div_num;
                if (div_num == 4) div_num = 0;
            } else {
                std::printf("ERROR: buff not ready - %u\n", buff_not_ready_count);
                ++buff_not_ready_count;
            }
        }
        // Optional loop/wait timing; enable alongside the per-buffer logs.
        // if (frame_interval_valid) {
        //     std::printf("wait-start interval: %llu us; wait_for_blanking: %llu us\n",
        //                 static_cast<unsigned long long>(frame_interval_us),
        //                 static_cast<unsigned long long>(wait_us));
        // }
    }

}
