# USB firmware for the BTT S2DW (RP2040 + LIS2DW12): streams the accelerometer over USB
# serial to a laptop (tools/shaker/laptop_s2dw.py). Build:
#   cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk && cmake --build build
# Flash: hold BOOT, plug in, copy build/s2dw_usb.uf2 to the RPI-RP2 drive.
cmake_minimum_required(VERSION 3.13)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(s2dw_usb C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(s2dw_usb main.c)
target_link_libraries(s2dw_usb pico_stdlib hardware_spi)
target_compile_definitions(s2dw_usb PRIVATE
    USBD_PRODUCT="S2DW Accelerometer"
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0)
pico_enable_stdio_usb(s2dw_usb 1)
pico_enable_stdio_uart(s2dw_usb 0)

# The flash chip of the S2DW: BTT specifies "GENERIC_03H with CLKDIV 4" (works with any chip)
pico_define_boot_stage2(s2dw_boot2 ${PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_generic_03h.S)
target_compile_definitions(s2dw_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(s2dw_usb s2dw_boot2)

pico_add_extra_outputs(s2dw_usb)
