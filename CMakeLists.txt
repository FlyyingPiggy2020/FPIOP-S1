# IO Probe firmware for HPM5301EVKLite: I2C / SPI / GPIO control + CMSIS-DAP (SWD) + target UART

cmake_minimum_required(VERSION 3.13)

set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)

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
    third_party/cherrydap/Source/SW_DP.c
    third_party/cherrydap/Source/DAP_vendor.c
    src/protocol.c
    src/pins.c
    src/cmd_sys.c
    src/cmd_gpio.c
    src/cmd_i2c.c
    src/cmd_spi.c
)
generate_ide_projects()
