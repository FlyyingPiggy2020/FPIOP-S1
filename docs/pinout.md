# IO Probe 引脚表（HPM5301EVKLite J3）

来源：
- 《HPM5301EVKLite 用户使用手册》第二章 J3 表
- SDK 文件：`boards/hpm5301evklite/{README_en.rst,pinmux.c,board.h}`
- 芯片复用表：`soc/HPM5300/HPM5301/hpm_iomux.h`

注意事项：
- 所有 IO 都是 **3.3 V**，没有电平转换，也没有保护。
- 外接设备必须和板子**共地**，不要接 5 V 信号。

## J3 完整表

| J3 | 芯片脚 | 手册标注 | 芯片可复用功能（不含 GPIO） | IO probe 分配 |
|---|---|---|---|---|
| 1 | — | 3.3V | — | 电源 |
| 2, 4 | — | 5.0V | — | 电源（**不要接到 IO**） |
| 3 | PB09 | I2C_SDA | I2C2_SDA, SPI2_CS_1, UART2_RXD, GPTMR0_CAPT_1 | **I2C-A SDA**（I2C2） |
| 5 | PB08 | I2C_SCL | I2C2_SCL, SPI2_CS_2, UART2_TXD, GPTMR0_COMP_1 | **I2C-A SCL**（I2C2） |
| 7 | PB15? / PA02? | GPIO | 见下方"待核实" | GPIO（待核实） |
| 8 | PB15 | UART_TXD | UART3_TXD, SPI2_DAT3, GPTMR0_COMP_3 | **目标串口 TX**（UART3，接目标板 RX） |
| 10 | PB14 | UART_RXD | UART3_RXD, SPI2_DAT2 | **目标串口 RX**（UART3，接目标板 TX） |
| 11 | PA31 | GPIO | SPI1_DAT3, TRGM0_P_07 | **SWCLK**（DAPLink） |
| 13 | PB11 | GPIO | **SPI2_SCLK**, UART2_CTS, ACMP | **GPIO** / SPI-B SCLK |
| 19 | PA29 | SPI_MOSI | SPI1_MOSI, I2C3_SCL | **SPI-A MOSI**（SPI1） |
| 21 | PA28 | SPI_MISO | SPI1_MISO, I2C3_SDA | **SPI-A MISO**（SPI1） |
| 23 | PA27 | SPI_SCLK | SPI1_SCLK | **SPI-A SCLK**（SPI1） |
| 24 | PA26 | SPI_CS0 | SPI1_CS_0 | **SPI-A CS0**（SPI1） |
| 26 | PB10 | SPI_CS1 | **SPI2_CS_0**, UART2_RTS, GPTMR0_COMP_2, ADC | SPI-A CS1（用 GPIO 软件片选）/ SPI-B CS0 |
| 27 | PB12 | GPIO | **I2C3_SDA**, **SPI2_MISO**, UART3_CTS | I2C-B SDA 或 SPI-B MISO |
| 28 | PB13 | GPIO | **I2C3_SCL**, **SPI2_MOSI**, UART3_RTS | I2C-B SCL 或 SPI-B MOSI |
| 29 | PY00 | GPIO | UART0_TXD, USB0_ID | **SWDIO**（DAPLink，板上有上拉） |
| 31 | PY01 | GPIO | UART0_RXD, USB0_OC | **nRESET**（DAPLink，开漏，板上有上拉） |
| 32 | PA09 | GPIO | I2C2_SDA, SPI3_CS_1, SOC_REF0 | **GPIO**（SDK 里也是 TinyUF2 的引导脚） |
| 33 | PA10 | GPIO | SPI3_CS_0, GPTMR0_COMP_2 | GPIO（**和板载 LED 共用**） |
| 36 | PA00 | UART_LOG_TX | UART0_TXD | 调试打印，**保留** |
| 37 | PA30 | GPIO | SPI1_DAT2, SOC_REF0 | **不使用**（板上有东西把它拉低，见下方） |
| 38 | PA01 | UART_LOG_RX | UART0_RXD | 调试打印，**保留** |
| 6,9,14,20,25,30,34,39 | — | GND | — | 地 |
| 12,15,16,18,22,35,40 | — | NC | — | 没有连接 |

## 当前分配（固件 0.2.1）

| 接口 | J3 引脚 | 外设 | 说明 |
|---|---|---|---|
| I2C-A（主机） | SDA 3, SCL 5 | I2C2 | 手册标注的 I2C 口。外接设备要有上拉（建议 4.7 kΩ 接 3.3 V），芯片内部上拉只够 100 kHz 调试用 |
| SPI-A（主机） | MOSI 19, MISO 21, SCLK 23, CS0 24 | SPI1 | CS1 用 26（PB10），由 GPIO 软件控制 |
| GPIO | 13, 27, 28, 32 | GPIO | 一共 4 个。33 和 LED 共用，7 待核实，37 不可用 |
| DAPLink（SWD） | SWCLK 11, SWDIO 29, nRESET 31 | FGPIO 位操作 | CMSIS-DAP v2；接目标板还要接 GND |
| 目标串口 | TX 8, RX 10 | UART3 | 电脑上是第二个 USB 串口，波特率跟随电脑端设置 |

0.1.0 里 8、10、11、29、31 是 GPIO；从 0.2.0 起分给了 DAPLink 和目标串口。0.2.1 又去掉了 37。

**接目标板（3.3 V ARM Cortex-M）的接线**

| IO Probe J3 | 目标板 |
|---|---|
| 11 SWCLK | SWCLK |
| 29 SWDIO | SWDIO |
| 31 nRESET | nRESET（可选） |
| 8 TX | 目标串口 RX（可选） |
| 10 RX | 目标串口 TX（可选） |
| 任意 GND（如 9、14） | GND |

## 第二阶段：二选一的总线

J3 的 27/28 只能在下面两种用法里选一种（PB12/PB13 同时连到了 I2C3 和 SPI2）：
- **I2C-B = I2C3**（SDA 27, SCL 28）：做 I2C 从机模拟；或者和 I2C-A 连起来做自测回环。
- **SPI-B = SPI2**（SCLK 13, MISO 27, MOSI 28, CS0 26）：做 SPI 从机模拟。这时 26 不能再给 SPI-A 当 CS1。

固件按命令动态切换这两组的复用功能，默认先当 GPIO。

## 实测（固件 0.1.0，引脚悬空；8、10、11、29、31 现在已不是 GPIO）

| J3 | 芯片脚 | 输入：不上下拉 / 上拉 / 下拉 | 输出 0 / 1 读回 | 结论 |
|---|---|---|---|---|
| 8, 10, 11, 13, 27, 28, 32 | — | 0 / 1 / 0 | 0 / 1 | 正常 |
| 29, 31 | PY00, PY01 | 1 / 1 / 1 | 0 / 1 | 板上有**外部上拉**（USB ID/OC 线），当输入时默认读到 1 |
| 37 | PA30 | 0 / 0 / 0 | 0 / 1 | 板上有**外部下拉**，当输入时默认读到 0 |

GPIO 的 FUNC_CTL 都打开了 LOOP_BACK。所以引脚设成输出时，读到的也是引脚上的真实电平，可以用来发现短路或电平冲突。

## 待核实

- **J3[7]**：手册写的是 PB15，但 PB15 已经在 J3[8]（UART_TXD）。SDK README 写 J3[7] 是 GPTMR1.COMP_1，对应 PA02。
  所以手册多半是笔误，实际应该是 **PA02**。阶段 1 用 GPIO 翻转测量确认之前，先不把这个脚分配出去。
- **PY00 / PY01（29 / 31）**：SDK 的 `init_usb_pins()` 会把它们设成 USB0_ID / USB0_OC。probe 只当 USB 设备用，
  `DAP_Setup()` 在 USB 初始化之后把它们改成 SWDIO / nRESET。PY 口属于电源域，翻转速度可能比 PA/PB 口慢，
  SWD 时钟上不去时改用 13 / 32。
- **J3-37（PA30）已从 GPIO 列表去掉（0.2.1）**：在 0.2.0 上，J3-37 什么都没接，推挽输出 1 时读回 0，寄存器配置正确（输出使能、输出值 1、LOOP_BACK 打开）。
  当输入时，不论上拉还是下拉都读到 0。说明板上有东西把 PA30 强拉到低电平；这个脚的复用功能里有 USB0_PWR，可能接在 USB 供电电路上。
  0.1.0 时有一次读回是 1，原因不明。确认原因要看完整原理图，或者用万用表量 J3-37 对地电阻。
- **I2C 上拉**：板上 3/5 脚有没有焊上拉电阻，原理图摘录里看不出来，需要用万用表量一下（量对 3.3 V 的电阻）。
