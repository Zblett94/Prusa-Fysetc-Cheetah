#include "mmu2_serial.h"
#ifdef __AVR__
#include "uart2.h"
#else
// STM32 port: the MMU is connected to USART2 on the "TFT" header of the SKR Mini E3 V3
#include <Arduino.h>
#include "Configuration_var.h"
#include "pins.h"
static HardwareSerial uart2(MMU_SERIAL_RX_PIN, MMU_SERIAL_TX_PIN);
#endif

namespace MMU2 {

void MMU2Serial::begin(uint32_t baud){
#ifdef __AVR__
    uart2_init(baud); // @@TODO we may skip the baud rate setting in case of 8bit FW ... could save some bytes...
#else
    uart2.begin(baud);
#endif
}

void MMU2Serial::close() {
    // @@TODO - probably turn off the UART
}

int MMU2Serial::read() {
#ifdef __AVR__
    return fgetc(uart2io);
#else
    return uart2.read();
#endif
}

void MMU2Serial::flush() {
    // @@TODO - clear the output buffer
}

void MMU2Serial::write(const uint8_t *buffer, size_t size) {
#ifdef __AVR__
    while(size--){
        fputc(*buffer, uart2io);
        ++buffer;
    }
#else
    uart2.write(buffer, size);
#endif
}

MMU2Serial mmu2Serial;

} // namespace MMU2
