#pragma once

#include <cstdint>
#include <cstddef>
#include <initializer_list>

enum class ST7796Command : std::uint8_t {
    NOP        = 0x00,
    SWRESET    = 0x01,  // Software reset
    SLPIN      = 0x10,  // Sleep in
    SLPOUT     = 0x11,  // Sleep out
    INVOFF     = 0x20,  // Display inversion off
    INVON      = 0x21,  // Display inversion on
    DISPOFF    = 0x28,  // Display off
    DISPON     = 0x29,  // Display on

    CASET      = 0x2A,  // Column address set
    RASET      = 0x2B,  // Row address set
    RAMWR      = 0x2C,  // Memory write
    RAMRD      = 0x2E,  // Memory read

    MADCTL     = 0x36,  // Memory access control / rotation
    COLMOD     = 0x3A,  // Interface pixel format

    WRDISBV    = 0x51,  // Write display brightness
    WRCTRLD    = 0x53,  // Write control display
};

void send_command(ST7796Command cmd);
void send_data(const std::uint8_t* data, std::size_t size);
void send_data(std::initializer_list<std::uint8_t> data);

template <std::size_t N>
void send_data(const std::uint8_t (&data)[N]) {
    send_data(data, N);
}

void init_display();
