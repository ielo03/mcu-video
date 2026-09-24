#include "pico/stdlib.h"
#include "config.hpp"
#include "st7796.hpp"
#include "hardware/spi.h"
#include <cstdio>

#define ROTATION 0

uint8_t read_baud_set = 0;

// SPI access and this state are owned by core 0. Finish DMA before switching.
void reset_write_baud() {
    if (!read_baud_set) return;
    while (spi_is_busy(LCD_SPI)) tight_loop_contents();
    spi_set_baudrate(LCD_SPI, SPI_MHZ * 1000 * 1000);
    read_baud_set = 0;
}

void ensure_write_baud() {
    if (read_baud_set) {
        reset_write_baud();
    }
}

// Read commands must also be sent at read speed, without the write guard.
static void send_command_at_current_baud(ST7796Command cmd) {
    std::uint8_t value = static_cast<std::uint8_t>(cmd);

    gpio_put(PIN_DC, 0);
    spi_write_blocking(LCD_SPI, &value, 1);
}

void send_command_no_cs(ST7796Command cmd) {
    ensure_write_baud();
    send_command_at_current_baud(cmd);
}

void send_command(ST7796Command cmd) {
    ensure_write_baud();
    gpio_put(PIN_CS, 0);

    send_command_at_current_baud(cmd);

    gpio_put(PIN_CS, 1);
}

void send_data(std::initializer_list<std::uint8_t> data) {
    send_data(data.begin(), data.size());
}

void send_data(const std::uint8_t* data, std::size_t size) {
    ensure_write_baud();
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    spi_write_blocking(LCD_SPI, data, size);

    gpio_put(PIN_CS, 1);
}

static void read_register(ST7796Command command, uint8_t* rx, size_t size) {
    // Caller must finish any DMA transfer before reading the display.
    while (spi_is_busy(LCD_SPI)) {
        tight_loop_contents();
    }
    gpio_put(PIN_CS, 1);
    if (!read_baud_set) {
        spi_set_baudrate(LCD_SPI, SPI_READ_MHZ * 1000 * 1000);
        read_baud_set = 1;
    }
    gpio_put(PIN_CS, 0);

    send_command_at_current_baud(command);

    gpio_put(PIN_DC, 1);

    spi_read_blocking(LCD_SPI, 0, rx, size);

    gpio_put(PIN_CS, 1);
    // Keep read speed for subsequent reads; caller resets before writing.
}

uint8_t get_pixel_format() {
    // Datasheet p. 91: 0x0C returns eight bits, with no dummy clock.
    uint8_t format;
    read_register(ST7796Command::RDDCOLMOD, &format, 1);
    return format;
}

void get_scanline_raw(uint8_t (&rx)[3]) {
    read_register(ST7796Command::GSCAN, rx, sizeof(rx));
}

uint16_t decode_scanline(const uint8_t (&rx)[3]) {
    // Experimental SPI layout: 1 dummy bit, 16 data bits, 7 trailing bits.
    const uint32_t captured =
        (static_cast<uint32_t>(rx[0]) << 16) |
        (static_cast<uint32_t>(rx[1]) << 8) |
        static_cast<uint32_t>(rx[2]);
    return static_cast<uint16_t>((captured >> 7) & 0xFFFFu);
}

uint16_t get_scanline() {
    uint8_t rx[3];
    get_scanline_raw(rx);
    return decode_scanline(rx);
}

void wait_for_blanking_with_gap(uint16_t return_line) {
    uint16_t line = get_scanline();
    if (line < 2 || line > 199) {
        std::printf("ERROR: first scanline=%u; expected 2-199\n",
                    static_cast<unsigned>(line));
    }
    // Establish the low advancing range before accepting the threshold.
    for (;;) {
        while (line < 2 || line > 199) line = get_scanline();
        while (line >= 2 && line <= 224) {
            if (line >= return_line) return;
            line = get_scanline();
        }
        // If the plateau was reached before observing the threshold, retry
        // next cycle rather than accepting a high-range reading like 257.
    }
}

void init_display() {
    // Hardware reset
    gpio_put(PIN_RST, 1);
    sleep_ms(5);

    gpio_put(PIN_RST, 0);
    sleep_ms(10);

    gpio_put(PIN_RST, 1);
    sleep_ms(5);

    // Exit sleep before extended-register configuration.
    send_command(ST7796Command::SLPOUT);
    sleep_ms(120);

    // Enable Command Set 2.
    send_command(static_cast<ST7796Command>(0xF0));
    send_data({0xC3});

    send_command(static_cast<ST7796Command>(0xF0));
    send_data({0x96});

    // Leave power control 3 and gamma at their reset settings.

    // Documented VCOM default; C5 takes one parameter.
    send_command(static_cast<ST7796Command>(0xC5));
    send_data({0x1C});

    send_command(static_cast<ST7796Command>(0xB1));
    send_data({0xA0, 0x10});

    // Maximum vertical front/back porches for the synchronization experiment.
    send_command(static_cast<ST7796Command>(0xB5));
    send_data({0xFF, 0xFF, 0x00, 0x04});
    //          VFP   VBP   reserved HBP

    // Documented display-function defaults, including 480 gate lines.
    send_command(static_cast<ST7796Command>(0xB6));
    send_data({0x80, 0x02, 0x3B});

    // Disable Command Set 2 again.
    send_command(static_cast<ST7796Command>(0xF0));
    send_data({0x3C});

    send_command(static_cast<ST7796Command>(0xF0));
    send_data({0x69});

    // 16-bit RGB565
    send_command(ST7796Command::COLMOD);
    send_data({0x55});

    send_command(ST7796Command::INVON);

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

    // Display on
    send_command(ST7796Command::DISPON);

    gpio_put(PIN_BL, 1);
}
