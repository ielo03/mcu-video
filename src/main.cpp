#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "st7796.hpp"
#include "config.hpp"

int main() {
    stdio_init_all();

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

    init_display();

    uint8_t red[2] = {0xF8, 0x00};
    uint8_t green[2] = {0x07, 0xE0};
    uint8_t blue[2] = {0x00, 0x1F};
    uint8_t white[2] = {0xFF, 0xFF};
    uint8_t black[2] = {0x00, 0x00};
    uint8_t yellow[2] = {0xFF, 0xE0};
    uint8_t cyan[2] = {0x07, 0xFF};
    uint8_t magenta[2] = {0xF8, 0x1F};

    // Full-screen window for ROTATION 0: columns 0..319, rows 0..479.
    send_command(ST7796Command::CASET);
    send_data({0x00, 0x00, 0x01, 0x3F});

    send_command(ST7796Command::RASET);
    send_data({0x00, 0x00, 0x01, 0xDF});

    send_command(ST7796Command::RAMWR);
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    uint8_t count = 0;
    while (true) {
        ++count;
        if (count == 8) count = 0;
        uint8_t *color;
        switch (count) {
            case 0:
                color = red;
                break;
            case 1:
                color = green;
                break;
            case 2:
                color = blue;
                break;
            case 3:
                color = white;
                break;
            case 4:
                color = black;
                break;
            case 5:
                color = yellow;
                break;
            case 6:
                color = cyan;
                break;
            case 7:
                color = magenta;
                break;
            default:
                color = red;
                break;
        }
        for (int i = 0; i < 320 * 480; ++i) {
            spi_write_blocking(LCD_SPI, color, 2);
        }
    }

    gpio_put(PIN_CS, 1);

    while (true) {
        tight_loop_contents();
    }
}
