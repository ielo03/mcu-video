#include "pico/stdlib.h"
#include "config.hpp"
#include "st7796.hpp"
#include "hardware/spi.h"

#define ROTATION 0

void send_command(ST7796Command cmd) {
   std::uint8_t value = static_cast<std::uint8_t>(cmd);

    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);

    spi_write_blocking(LCD_SPI, &value, 1);

    gpio_put(PIN_CS, 1);
}

void send_data(std::initializer_list<std::uint8_t> data) {
    send_data(data.begin(), data.size());
}

void send_data(const std::uint8_t* data, std::size_t size) {
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    spi_write_blocking(LCD_SPI, data, size);

    gpio_put(PIN_CS, 1);
}

void init_display() {
    // Hardware reset
    gpio_put(PIN_RST, 1);
    sleep_ms(5);

    gpio_put(PIN_RST, 0);
    sleep_ms(10);

    gpio_put(PIN_RST, 1);
    sleep_ms(5);

    send_command(ST7796Command::INVON);

    send_command(static_cast<ST7796Command>(0xC2));
    send_data({0x33});

    send_command(static_cast<ST7796Command>(0xC5));
    send_data({0x00, 0x1E, 0x80});

    send_command(static_cast<ST7796Command>(0xB1));
    send_data({0xB0});

    // Positive gamma
    send_command(static_cast<ST7796Command>(0xE0));
    send_data({0x00, 0x13, 0x18, 0x04, 0x0F, 0x06, 0x3A, 0x56, 0x4D, 0x03, 0x0A, 0x06, 0x30, 0x3E, 0x0F});

    // Negative gamma
    send_command(static_cast<ST7796Command>(0xE1));
    send_data({0x00, 0x13, 0x18, 0x01, 0x11, 0x06, 0x38, 0x34, 0x4D, 0x06, 0x0D, 0x0B, 0x31, 0x37, 0x0F});

    // 16-bit RGB565
    send_command(ST7796Command::COLMOD);
    send_data({0x55});

    // Sleep out
    send_command(ST7796Command::SLPOUT);
    sleep_ms(120);

    // Display on
    send_command(ST7796Command::DISPON);

    send_command(static_cast<ST7796Command>(0xB6));
    send_data({0x00, 0x62});

    // Memory access control / rotation
    send_command(ST7796Command::MADCTL);

    switch (ROTATION) {
        case 0:
            send_data({0x88});
            break;

        case 90:
            send_data({0xE8});
            break;

        case 180:
            send_data({0x48});
            break;

        case 270:
        default:
            send_data({0x28});
            break;
    }

    gpio_put(PIN_BL, 1);
}
