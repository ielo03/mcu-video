#pragma once

#include <cstdint>
#include <cstddef>
#include <initializer_list>

enum class ST7796Command : std::uint8_t {
    NOP        = 0x00,
    RDDCOLMOD  = 0x0C,  // Read pixel format
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

    GSCAN      = 0x45,  // Get scanline
};

// Call after a batch of reads, before commands, data, or DMA writes.
void reset_write_baud();
// Write-path guard: restores speed if the explicit reset was missed.
void ensure_write_baud();
// Core 0 only; finish any DMA before switching. true = 16-bit pixels.
void ensure_spi_format(bool use_16_bit);

void send_command_no_cs(ST7796Command cmd);
void send_command(ST7796Command cmd);
void send_data(const std::uint8_t* data, std::size_t size);
void send_data(std::initializer_list<std::uint8_t> data);

uint16_t get_scanline();
uint8_t get_pixel_format();
void get_scanline_raw(uint8_t (&rx)[3]);
uint16_t decode_scanline(const uint8_t (&rx)[3]);

void wait_for_blanking_with_gap(uint16_t return_line);

template <std::size_t N>
void send_data(const std::uint8_t (&data)[N]) {
    send_data(data, N);
}

void init_display();
