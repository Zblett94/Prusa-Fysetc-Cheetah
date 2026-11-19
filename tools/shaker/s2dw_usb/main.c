// USB firmware for the BTT S2DW V1.0 (RP2040 + LIS2DW12 on GPIO8 MISO, 9 CS, 10 SCK, 11 MOSI).
// Streams the accelerometer over USB serial, set up as Klipper does: 1600 Hz, +-16 g,
// high performance, low noise, continuous FIFO.
// Commands (one character): i = identify, s = start streaming, x = stop.
// Output while streaming: "x y z" per sample (raw 16 bit, 4.786 mm/s^2 per count),
// "T <microseconds> <samples so far>" after every FIFO read, "O <count>" on FIFO overflow.
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define PIN_MISO 8
#define PIN_CS 9
#define PIN_SCK 10
#define PIN_MOSI 11

#define REG_WHO_AM_I 0x0F
#define REG_CTRL1 0x20
#define REG_CTRL2 0x21
#define REG_CTRL6 0x25
#define REG_OUT_X_L 0x28
#define REG_FIFO_CTRL 0x2E
#define REG_FIFO_SAMPLES 0x2F
#define READ 0x80

static bool use_hw;

// --- SPI mode 3: hardware SPI1, or bit-banged on the same pins -----------------------------
static uint8_t bb_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; --i) {
        gpio_put(PIN_SCK, 0);
        gpio_put(PIN_MOSI, (out >> i) & 1);
        busy_wait_us_32(1);
        gpio_put(PIN_SCK, 1);
        in = (uint8_t)(in << 1) | (uint8_t)gpio_get(PIN_MISO);
        busy_wait_us_32(1);
    }
    return in;
}

static void xfer(uint8_t *buf, size_t n)
{
    gpio_put(PIN_CS, 0);
    if (use_hw) spi_write_read_blocking(spi1, buf, buf, n);
    else for (size_t i = 0; i < n; ++i) buf[i] = bb_byte(buf[i]);
    gpio_put(PIN_CS, 1);
}

static uint8_t rd(uint8_t reg)
{
    uint8_t b[2] = { (uint8_t)(READ | reg), 0 };
    xfer(b, 2);
    return b[1];
}

static void wr(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    xfer(b, 2);
}

static void bus_hw(void)
{
    spi_init(spi1, 4000000);
    spi_set_format(spi1, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    use_hw = true;
}

static void bus_bitbang(void)
{
    spi_deinit(spi1);
    gpio_init(PIN_MISO); gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_init(PIN_SCK); gpio_set_dir(PIN_SCK, GPIO_OUT); gpio_put(PIN_SCK, 1);
    gpio_init(PIN_MOSI); gpio_set_dir(PIN_MOSI, GPIO_OUT);
    use_hw = false;
}

//! find the sensor: hardware SPI first, bit-banged as Klipper's sample config does otherwise
static uint8_t probe(void)
{
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    bus_hw();
    sleep_ms(5);
    uint8_t id = rd(REG_WHO_AM_I);
    if (id != 0x44) {
        bus_bitbang();
        id = rd(REG_WHO_AM_I);
    }
    return id;
}

static bool start(void)
{
    if (rd(REG_WHO_AM_I) != 0x44) return false;
    wr(REG_CTRL2, 0x46);   // soft reset (and I2C off, address increment)
    sleep_ms(10);
    wr(REG_CTRL2, 0x06);   // I2C off, address auto increment
    wr(REG_CTRL6, 0x34);   // bandwidth ODR/2, +-16 g, low noise
    wr(REG_FIFO_CTRL, 0x00);
    wr(REG_FIFO_CTRL, 0xC0); // continuous FIFO
    wr(REG_CTRL1, 0x94);   // 1600 Hz, high performance (14 bit)
    return rd(REG_CTRL1) == 0x94;
}

static void stop(void)
{
    wr(REG_CTRL1, 0x00);
    wr(REG_FIFO_CTRL, 0x00);
}

int main(void)
{
    stdio_init_all();
    const uint8_t id = probe();
    bool streaming = false;
    uint32_t total = 0, overflows = 0, last_read = 0;
    uint8_t buf[1 + 6 * 32];
    for (;;) {
        const int c = getchar_timeout_us(streaming ? 0 : 1000);
        if (c == 'i') {
            printf("S2DW LIS2DW12 id=0x%02x (%s) spi=%s\n", rd(REG_WHO_AM_I),
                   id == 0x44 ? "ok" : "NOT FOUND", use_hw ? "hw" : "bitbang");
        } else if (c == 's') {
            total = overflows = 0;
            streaming = start();
            printf(streaming ? "STARTED\n" : "ERROR sensor not found\n");
        } else if (c == 'x') {
            if (streaming) stop();
            streaming = false;
            printf("STOPPED %lu samples %lu overflows\n", (unsigned long)total, (unsigned long)overflows);
        }
        if (!streaming) continue;
        const uint8_t st = rd(REG_FIFO_SAMPLES);
        if (st & 0x40) printf("O %lu\n", (unsigned long)++overflows);
        const uint32_t n = st & 0x3F;
        // batches of >= 8 samples (5 ms at 1600 Hz; the FIFO holds 32 = 20 ms)
        if (!n || (n < 8 && time_us_32() - last_read < 4000)) continue;
        last_read = time_us_32();
        buf[0] = READ | REG_OUT_X_L;
        for (uint32_t i = 1; i <= 6 * n; ++i) buf[i] = 0;
        xfer(buf, 1 + 6 * n);  // the FIFO read wraps from OUT_Z_H back to OUT_X_L (as in Klipper)
        for (uint32_t k = 0; k < n; ++k) {
            const uint8_t *p = buf + 1 + 6 * k;
            printf("%d %d %d\n", (int16_t)(p[0] | p[1] << 8), (int16_t)(p[2] | p[3] << 8), (int16_t)(p[4] | p[5] << 8));
        }
        total += n;
        printf("T %llu %lu\n", (unsigned long long)time_us_64(), (unsigned long)total);
    }
}
