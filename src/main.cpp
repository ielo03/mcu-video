#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico/multicore.h"

#include <atomic>
#include <cstdio>

#include "st7796.hpp"
#include "config.hpp"

uint8_t a_buff[BUFF_SIZE];
uint8_t b_buff[BUFF_SIZE];

std::atomic<bool> a_buff_written{true};
std::atomic<bool> b_buff_written{true};

uint32_t a_fill_us = 0;
uint32_t b_fill_us = 0;

const uint8_t raset[4][4] = {
    {0x00, 0x00, 0x00, 0x77}, // Top: rows 0–119
    {0x00, 0x78, 0x00, 0xEF}, // Top middle: rows 120–239
    {0x00, 0xF0, 0x01, 0x67}, // Bottom middle: rows 240–359
    {0x01, 0x68, 0x01, 0xDF}, // Bottom: rows 360–479
};

void init_gpio() {
    spi_init(LCD_SPI, SPI_MHZ * 1000 * 1000);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

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

    init_display();

    uint8_t div_num = 0;
    while (true) {
        // Full-screen window for ROTATION 0: columns 0..319, rows 0..479.
        send_command(ST7796Command::CASET);
        send_data({0x00, 0x00, 0x01, 0x3F});

        send_command(ST7796Command::RASET);
        send_data(raset[div_num]);

        send_command(ST7796Command::RAMWR);
        gpio_put(PIN_DC, 1);
        gpio_put(PIN_CS, 0);

        if (!a_buff_written) {
            const uint32_t fill_us = a_fill_us;
            const uint32_t send_start = time_us_32();
            spi_write_blocking(LCD_SPI, a_buff, BUFF_SIZE);
            const uint32_t send_us = time_us_32() - send_start;
            a_buff_written = true;
            gpio_put(PIN_CS, 1);
            std::printf("buffer A strip %u: fill=%lu us send=%lu us bytes=%u\n",
                        static_cast<unsigned>(div_num),
                        static_cast<unsigned long>(fill_us),
                        static_cast<unsigned long>(send_us),
                        static_cast<unsigned>(BUFF_SIZE));
            ++div_num;
            if (div_num == 4) div_num = 0;
        } else if (!b_buff_written) {
            const uint32_t fill_us = b_fill_us;
            const uint32_t send_start = time_us_32();
            spi_write_blocking(LCD_SPI, b_buff, BUFF_SIZE);
            const uint32_t send_us = time_us_32() - send_start;
            b_buff_written = true;
            gpio_put(PIN_CS, 1);
            std::printf("buffer B strip %u: fill=%lu us send=%lu us bytes=%u\n",
                        static_cast<unsigned>(div_num),
                        static_cast<unsigned long>(fill_us),
                        static_cast<unsigned long>(send_us),
                        static_cast<unsigned>(BUFF_SIZE));
            ++div_num;
            if (div_num == 4) div_num = 0;
        }
    }

    gpio_put(PIN_CS, 1);

    while (true) {
        tight_loop_contents();
    }
}
