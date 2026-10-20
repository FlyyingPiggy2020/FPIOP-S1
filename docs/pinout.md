# IO Probe 引脚表（HPM5301EVKLite J3）

适用固件 **0.3.0**。来源：
- 《HPM5301EVKLite 用户使用手册》第二章 J3 表
- 板上 J3 丝印表（实物，RevB）
- SDK 文件：`boards/hpm5301evklite/{README_en.rst,pinmux.c,board.h}`
- 芯片复用表：`soc/HPM5300/HPM5301/hpm_iomux.h`

注意事项：
- 所有 IO 都是 **3.3 V**，没有电平转换，也没有保护。
- 外接设备必须和板子**共地**，不要接 5 V 信号。
- USB 线必须插 **J1**（J2 只供电）。J1 缺少 CC 下拉电阻，要用 A-to-C 线，或者在 USB-C 口上加"C 转 A"转接头；C-to-C 线接 USB-C 主机口不会上电。

## 分配原则

- I2C、SPI、串口、SWD 每种通讯接口各一路。
- I2C、SPI、串口用片上硬件外设，位置由芯片决定。
- SWD 是 GPIO 位操作，可以放在任意脚上，所以把它集中到 **J3-27/28/29/30** 这个 2×2 方块（带一个 GND），4 根线并排插。

## 排针视图

左列是奇数脚，右列是偶数脚，第 1 脚是 3.3 V。

| 用途 | 芯片脚 | 脚号 | 脚号 | 芯片脚 | 用途 |
|---|---|---|---|---|---|
| 3.3 V | — | 1 | 2 | — | 5 V |
| **I2C SDA** | PB09 | 3 | 4 | — | 5 V |
| **I2C SCL** | PB08 | 5 | 6 | — | GND |
| 未分配（待核实） | ? | 7 | 8 | PB15 | **串口 TX** |
| GND | — | 9 | 10 | PB14 | **串口 RX** |
| **GPIO** | PA31 | 11 | 12 | — | NC |
| **GPIO** | PB11 | 13 | 14 | — | GND |
| NC | — | 15 | 16 | — | NC |
| 3.3 V | — | 17 | 18 | — | NC |
| **SPI MOSI** | PA29 | 19 | 20 | — | GND |
| **SPI MISO** | PA28 | 21 | 22 | — | NC |
| **SPI SCLK** | PA27 | 23 | 24 | PA26 | **SPI CS0** |
| GND | — | 25 | 26 | PB10 | **SPI CS1** |
| **SWDIO** | PB12 | **27** | **28** | PB13 | **SWCLK** |
| **nRESET** | PY00 | **29** | **30** | — | **GND** |
| **GPIO** | PY01 | 31 | 32 | PA09 | **GPIO** |
| 板载 LED（不开放） | PA10 | 33 | 34 | — | GND |
| NC | — | 35 | 36 | PA00 | 调试串口 TX（保留） |
| 不使用（USB0_PWR） | PA30 | 37 | 38 | PA01 | 调试串口 RX（保留） |
| GND | — | 39 | 40 | — | NC |

## 按接口汇总

| 接口 | J3 引脚 | 外设 | 说明 |
|---|---|---|---|
| I2C 主机 | SDA 3，SCL 5（3.3 V 在 1，GND 在 9） | I2C2 | 外接设备要有上拉（建议 4.7 kΩ 接 3.3 V），芯片内部上拉只够 100 kHz 调试用 |
| SPI 主机 | MOSI 19，MISO 21，SCLK 23，CS0 24，CS1 26（GND 在 20、25） | SPI1 | 片选低电平有效，由 GPIO 软件控制，可跨多次传输保持 |
| 目标串口 | GND 6，TX 8，RX 10 | UART3 | 电脑上是第二个 USB 串口，波特率跟随电脑端设置 |
| DAPLink（SWD） | SWDIO 27，SWCLK 28，nRESET 29，GND 30 | FGPIO 位操作 | CMSIS-DAP v2；`src/dap/DAP_config.h` |
| GPIO | 11，13，31，32 | GPIO0 | 31 板上有上拉，悬空时读到 1 |

**接目标板（3.3 V ARM Cortex-M）**

| IO Probe J3 | 目标板 |
|---|---|
| 27 SWDIO | SWDIO |
| 28 SWCLK | SWCLK |
| 29 nRESET | nRESET（可选） |
| 30 GND | GND |
| 8 TX | 目标串口 RX（可选） |
| 10 RX | 目标串口 TX（可选） |

## J3 完整表

| J3 | 芯片脚 | 手册 / 丝印 | 芯片可复用功能（不含 GPIO） | 0.3.0 分配 |
|---|---|---|---|---|
| 1, 17 | — | 3.3V | — | 电源 |
| 2, 4 | — | 5.0V | — | 电源（**不要接到 IO**） |
| 3 | PB09 | I2C_SDA | I2C2_SDA, SPI2_CS_1, UART2_RXD, GPTMR0_CAPT_1 | **I2C SDA**（I2C2） |
| 5 | PB08 | I2C_SCL | I2C2_SCL, SPI2_CS_2, UART2_TXD, GPTMR0_COMP_1 | **I2C SCL**（I2C2） |
| 7 | PB15? / PA02? | 手册和丝印都写 PB15 | 见"待核实" | 未分配 |
| 8 | PB15 | UART_TXD / TX | UART3_TXD, SPI2_DAT3, GPTMR0_COMP_3 | **串口 TX**（UART3） |
| 10 | PB14 | UART_RXD / RX | UART3_RXD, SPI2_DAT2 | **串口 RX**（UART3） |
| 11 | PA31 | GPIO | SPI1_DAT3, TRGM0_P_07 | **GPIO** |
| 13 | PB11 | GPIO | SPI2_SCLK, UART2_CTS, ACMP | **GPIO** |
| 19 | PA29 | SPI_MOSI | SPI1_MOSI, I2C3_SCL | **SPI MOSI**（SPI1） |
| 21 | PA28 | SPI_MISO | SPI1_MISO, I2C3_SDA | **SPI MISO**（SPI1） |
| 23 | PA27 | SPI_SCLK | SPI1_SCLK | **SPI SCLK**（SPI1） |
| 24 | PA26 | SPI_CS0 | SPI1_CS_0 | **SPI CS0** |
| 26 | PB10 | SPI_CS1 | SPI2_CS_0, UART2_RTS, GPTMR0_COMP_2, ADC | **SPI CS1**（GPIO 软件片选） |
| 27 | PB12 | GPIO | I2C3_SDA, SPI2_MISO, UART3_CTS | **SWDIO** |
| 28 | PB13 | GPIO | I2C3_SCL, SPI2_MOSI, UART3_RTS | **SWCLK** |
| 29 | PY00 | GPIO / USB_ID | UART0_TXD, USB0_ID | **nRESET**（开漏，板上上拉） |
| 31 | PY01 | GPIO / USB_OC | UART0_RXD, USB0_OC | **GPIO**（板上上拉） |
| 32 | PA09 | GPIO / PA9 | I2C2_SDA, SPI3_CS_1, SOC_REF0 | **GPIO**（SDK 里也是 TinyUF2 的引导脚） |
| 33 | PA10 | GPIO / PA10_LED | SPI3_CS_0, GPTMR0_COMP_2 | 不开放（板载 LED） |
| 36 | PA00 | UART_LOG_TX / PA0 | UART0_TXD | 调试打印，**保留** |
| 37 | PA30 | GPIO / USB0_PWR | SPI1_DAT2, SOC_REF0, USB0_PWR | **不使用**（接 J1 的 VBUS 供电控制） |
| 38 | PA01 | UART_LOG_RX / PA1 | UART0_RXD | 调试打印，**保留** |
| 6,9,14,20,25,30,34,39 | — | GND | — | 地 |
| 12,15,16,18,22,35,40 | — | NC | — | 没有连接 |

## 版本变化

| 固件 | GPIO | SWD | 其他 |
|---|---|---|---|
| 0.1.0 | 8, 10, 11, 13, 27, 28, 29, 31, 32, 37 | — | — |
| 0.2.0 | 13, 27, 28, 32, 37 | SWCLK 11，SWDIO 29，nRESET 31 | 8/10 改为目标串口 |
| 0.2.1 | 13, 27, 28, 32 | 同上 | 去掉 37（USB0_PWR） |
| **0.3.0** | **11, 13, 31, 32** | **SWDIO 27，SWCLK 28，nRESET 29，GND 30** | SWD 集中成 2×2 方块；SWDIO/SWCLK 换到更快的 PB 口 |

## 从机模拟（后续）

按"每种接口各一路"，不再单独占用 I2C3 / SPI2 的引脚（PB12/PB13 已给 SWD）。后续的从机模拟改为让现有接口切换模式：
- I2C 从机：I2C2（3/5）由命令切换为从机，芯片支持主/从。
- SPI 从机：SPI1（19/21/23/24）由命令切换为从机。

## SWD 实测：STM32G0B1（固件 0.3.0）

目标板：STM32G0B1（Cortex-M0+，256 KB 双 Bank Flash），3.3 V，按上面的 2×2 方块接 SWDIO / SWCLK / nRESET / GND。工具：OpenOCD 0.12（`interface/cmsis-dap.cfg` + `cmsis_dap_backend usb_bulk` + `target/stm32g0x.cfg`）。

| 测试项 | 结果 |
|---|---|
| 连接 | DPIDR `0x0bc11477`，识别为 Cortex-M0+ r0p1（4 个断点、2 个观察点） |
| 芯片信息 | DBGMCU_IDCODE `0x10006467`（STM32G0B/G0Cx Rev A），Flash 256 KB，读到 96 位 UID |
| 停住 / 继续运行 | 正常；能读 PC、SP、xPSR 和 RAM |
| nRESET 复位 | `reset halt` 停在复位入口 `0x08000358`，和向量表的复位向量一致 |
| Flash 读取 | 16 KB 在 1 / 4 / 8 / 16 MHz 下读回的数据完全一致；速率 84 / 134 / 160 / 164 KiB/s |
| Flash 编程 | 写入 64 KB 随机数据，OpenOCD 校验通过，单独读回的哈希也一致 |
| 整片写回 | 256 KB 擦除 + 编程 + 校验约 9 s，读回和备份逐字节一致；目标板恢复运行原固件 |

说明：
- 8 MHz 以上读取速率不再增加（约 160 KiB/s），瓶颈是 GPIO 位操作和 USB 往返，实际 SWCLK 低于设定值。推荐用 4–8 MHz。
- 第一次测试时目标一直处在复位：IO Probe 没有驱动 nRESET，NRST 线却是低电平。原因是接线错误，重新接线后正常。遇到 `halt` 超时、DHCSR 一直是 `0x02000001`、外设寄存器全读 0 的情况，先查 NRST 接线和目标板供电。

## 实测（引脚悬空）

| J3 | 芯片脚 | 输入：不上下拉 / 上拉 / 下拉 | 输出 0 / 1 读回 | 结论 |
|---|---|---|---|---|
| 11, 13, 32（以及 0.1.0 时的 8, 10, 27, 28） | — | 0 / 1 / 0 | 0 / 1 | 正常 |
| 29, 31 | PY00, PY01 | 1 / 1 / 1 | 0 / 1 | 板上有**外部上拉**（丝印 USB_ID / USB_OC），当输入时默认读到 1 |
| 37 | PA30 | 0 / 0 / 0 | 0.1.0：0 / 1；0.2.0：0 / 0 | 被 USB0_PWR 电路拉低，已去掉 |

GPIO 的 FUNC_CTL 都打开了 LOOP_BACK。所以引脚设成输出时，读到的也是引脚上的真实电平，可以用来发现短路或电平冲突。

## 板上丝印与手册的差异

板上 J3 丝印（RevB）和手册的 J3 表大部分一致，下面几处补充了手册没写的信息：

| J3 | 丝印 | 手册 | 说明 |
|---|---|---|---|
| 7 | PB15 | PB15 | 两处都写 PB15，但 J3-8（丝印 TX）也是 PB15，见"待核实" |
| 8 / 10 | TX / RX | UART_TXD / UART_RXD | 即 UART3（PB15 / PB14），现作目标串口 |
| 29 | USB_ID | GPIO | PY00 原本接 USB OTG 的 ID 检测，所以板上有上拉；现作 nRESET，正好当复位上拉 |
| 31 | USB_OC | GPIO | PY01 原本接 USB 过流检测，所以板上有上拉 |
| 33 | PA10_LED | GPIO | 和板载 LED 共用 |
| 37 | USB0_PWR | GPIO | PA30 接 USB0 的 VBUS 供电控制，解释了它为什么被拉低 |
| 32 / 36 / 38 | PA9 / PA0 / PA1 | PA09 / PA00 / PA01 | 写法不同，同一个脚 |

**J3-37 不要驱动。** USB0_PWR 是 OTG 主机模式下打开 J1 VBUS 输出的控制信号。把它拉高，可能会让板子往 J1 的 VBUS 上输出 5 V，而此时 J1 正接着电脑。固件从 0.2.1 起不再碰它。

## 待核实

- **J3-7**：手册和板上丝印都写 PB15，但 PB15 已经在 J3-8（丝印 TX，SDK 里是 UART3_TXD）。SDK README 写 J3-7 是 GPTMR1.COMP_1，对应 PA02。
  所以有两种可能：J3-7 和 J3-8 在板上是连在一起的（都接 PB15），或者手册和丝印都写错了、实际是 PA02。
  用万用表蜂鸣档量 J3-7 和 J3-8 是否导通：导通就是 PB15（不能另作他用），不导通就按 PA02 处理。确认之前先不分配。
- **PY00（29）当 nRESET**：SDK 的 `init_usb_pins()` 会把它设成 USB0_ID，`DAP_Setup()` 在 USB 初始化之后改回 GPIO。
  在 STM32G0B1 上复位实测正常（见上方 SWD 实测）。
- **SWD 实际时钟**：已在 STM32G0B1 上验证连接、复位、读写和编程（见上方），但没用示波器量实际 SWCLK 频率。
- **I2C 上拉**：板上 3/5 脚有没有焊上拉电阻，原理图摘录里看不出来，需要用万用表量一下（量对 3.3 V 的电阻）。
