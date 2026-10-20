# IO Probe firmware for HPM5301EVKLite: I2C / SPI / GPIO control + CMSIS-DAP (SWD) + target UART

cmake_minimum_required(VERSION 3.13)

set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)

# Default to an optimized XIP build: the SDK maps "flash_xip" alone to a debug (-O0) build,
# which halves USB and SWD throughput. Override with -DCMAKE_BUILD_TYPE=debug for debugging.
if(NOT HPM_BUILD_TYPE AND (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE MATCHES "^(debug|release)$"))
    set(HPM_BUILD_TYPE flash_xip)
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(ioprobe)

sdk_inc(config)
sdk_inc(src)
sdk_inc(src/dap)
sdk_inc(third_party/cherrydap/Include)
sdk_app_src(
    src/main.c
    src/usb_link.c
    src/dap_link.c
    src/target_uart.c
    third_party/cherrydap/Source/DAP.c
    src/dap/sw_dp.c
    src/dap/sw_dp_io.c
    src/dap/sw_dp_spi.c
    third_party/cherrydap/Source/DAP_vendor.c
    src/protocol.c
    src/pins.c
    src/cmd_sys.c
    src/cmd_gpio.c
    src/cmd_i2c.c
    src/cmd_spi.c
)
generate_ide_projects()
