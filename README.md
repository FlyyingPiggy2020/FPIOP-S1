# HPM5301 IO Probe

把一块 **HPM5301EVKLite** 开发板变成 USB 接口的"IO 探针"：用一根 USB 线，电脑（或 Claude 等 AI）就能
- 收发 **I2C、SPI、GPIO** 信号，测试或模拟待测设备；
- 当 **DAPLink（CMSIS-DAP v2，SWD）调试器**，给 3.3 V 的 ARM Cortex-M 芯片烧录、调试；
- 通过第二个 USB 串口转发**目标板的串口**。

当前版本：固件 0.6.0 · 协议 v1 · 电脑端库 ioprobe 0.4.0。

## 目录

1. [硬件连接](#1-硬件连接)
2. [搭建开发环境（第一次）](#2-搭建开发环境第一次)
3. [编译固件](#3-编译固件)
4. [烧录固件](#4-烧录固件)
5. [安装电脑端工具](#5-安装电脑端工具)
6. [使用 GPIO / I2C / SPI](#6-使用-gpio--i2c--spi)
7. [使用 SWD 调试器（DAPLink）](#7-使用-swd-调试器daplink)
8. [使用目标串口](#8-使用目标串口)
9. [让 AI 直接调用（MCP）](#9-让-ai-直接调用mcp)
10. [测试](#10-测试)
11. [目录结构与文档](#11-目录结构与文档)

---

## 1. 硬件连接

### 1.1 板上接口

| 接口 | 用途 | 注意 |
|---|---|---|
| **J1**（Type-C，USB0） | 接电脑：供电 + 所有 USB 功能 | **必须插 J1**。J1 缺 CC 下拉电阻，要用 **USB-A 转 Type-C 线**；用 C-to-C 线接电脑的 USB-C 口不会上电。在 USB-C 口上加一个"C 转 A"转接头也可以 |
| J2（Type-C） | 只供电，不通信 | 插这里电脑看不到设备 |
| **J5**（20 针 JTAG） | 接 J-Link，烧录 IO Probe 自己的固件 | 只在开发 / 更新固件时用 |
| **J3**（2×20 排针） | 所有对外信号（见下表） | IO 都是 **3.3 V**，不能接 5 V 信号；外部设备必须和板子**共地** |

### 1.2 J3 引脚分配

排针视图：左列奇数脚、右列偶数脚，第 1 脚是 3.3 V。

| 用途 | 芯片脚 | 脚号 | 脚号 | 芯片脚 | 用途 |
|---|---|---|---|---|---|
| 3.3 V 输出 | — | 1 | 2 | — | 5 V |
| **I2C SDA** | PB09 | 3 | 4 | — | 5 V |
| **I2C SCL** | PB08 | 5 | 6 | — | GND |
| 不使用 | ? | 7 | 8 | PB15 | **目标串口 TX** |
| GND | — | 9 | 10 | PB14 | **目标串口 RX** |
| **GPIO 11** | PA31 | 11 | 12 | — | NC |
| **SWD SWCLK** | PB11 | 13 | 14 | — | GND |
| NC | — | 15 | 16 | — | NC |
| 3.3 V 输出 | — | 17 | 18 | — | NC |
| **SPI MOSI** | PA29 | 19 | 20 | — | GND |
| **SPI MISO** | PA28 | 21 | 22 | — | NC |
| **SPI SCLK** | PA27 | 23 | 24 | PA26 | **SPI CS0** |
| GND | — | 25 | 26 | PB10 | **SPI CS1** |
| **GPIO 27** | PB12 | 27 | 28 | PB13 | **SWD SWDIO** |
| **SWD nRESET** | PY00 | 29 | 30 | — | GND |
| **GPIO 31**（板上有上拉） | PY01 | 31 | 32 | PA09 | **GPIO 32** |
| 板载 LED，不开放 | PA10 | 33 | 34 | — | GND |
| NC | — | 35 | 36 | PA00 | 调试日志 TX（保留） |
| 不使用（USB0_PWR，不要驱动） | PA30 | 37 | 38 | PA01 | 调试日志 RX（保留） |
| GND | — | 39 | 40 | — | NC |

GND 脚：6、9、14、20、25、30、34、39。GPIO 一律用 J3 脚号称呼（11 / 27 / 31 / 32）。

### 1.3 各功能接线

**I2C**（接 3.3 V I2C 器件）

| IO Probe J3 | 器件 |
|---|---|
| 3 SDA | SDA |
| 5 SCL | SCL |
| 1 或 17（3.3 V） | VCC（器件可由探针供电，电流不要太大） |
| 6 / 9 GND | GND |

SDA、SCL 都要接 **4.7 kΩ 上拉到 3.3 V**（很多模块板上自带）。芯片内部上拉只够 100 kHz 调试用。

**SPI**（接 3.3 V SPI 器件，片选低电平有效）

| IO Probe J3 | 器件 |
|---|---|
| 19 MOSI | MOSI / SDI / DI |
| 21 MISO | MISO / SDO / DO |
| 23 SCLK | SCK |
| 24 CS0 | CS / NSS（第一个器件） |
| 26 CS1 | 第二个器件的 CS（可选） |
| 20 / 25 GND | GND |

**GPIO**：J3-11、27、31、32，可设为输入 / 推挽输出 / 开漏，带可选上拉 / 下拉。J3-31 板上有上拉，悬空时读到 1。

**SWD**（接 3.3 V ARM Cortex-M 目标板）

| IO Probe J3 | 目标板 |
|---|---|
| 13 SWCLK | SWCLK |
| 28 SWDIO | SWDIO |
| 29 nRESET | NRST（可选，用于硬件复位） |
| 30 或 14 GND | GND |

目标板要有自己的供电（或从 J3-1/17 取 3.3 V）。杜邦线尽量短，长线时把 SWD 时钟降到 8–16 MHz。

**目标串口**（看目标板日志、和目标固件交互）

| IO Probe J3 | 目标板 |
|---|---|
| 8 TX | RX |
| 10 RX | TX |
| 任意 GND | GND |

---

## 2. 搭建开发环境（第一次）

以 macOS（Apple 芯片）为例，环境装在工程外的 `~/sdk` 下，可供多个工程共用：

| 组件 | 位置 | 来源 |
|---|---|---|
| HPM SDK v1.13.0 | `~/sdk/hpm_sdk` | `git clone -b v1.13.0 https://github.com/hpmicro/hpm_sdk.git` |
| RISC-V 工具链 xPack GCC 15.2 | `~/sdk/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1` | [xpack-dev-tools/riscv-none-elf-gcc-xpack](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases)（darwin-arm64） |
| SDK 的 Python 依赖 | `~/sdk/.venv` | `python3 -m venv ~/sdk/.venv && ~/sdk/.venv/bin/pip install -r ~/sdk/hpm_sdk/scripts/requirements.txt` |
| SEGGER J-Link 软件 | `~/sdk/jlink` | SEGGER 官网（需要同意许可协议） |
| OpenOCD 0.12（用 SWD 功能时） | 系统路径 | `brew install openocd` |
| cmake、ninja | 系统路径 | `brew install cmake ninja` |

HPMicro 官方的 macOS 工具链依赖 Intel 版 Homebrew 的库，在 Apple 芯片上跑不起来，所以用 xPack。它的编译器前缀是 `riscv-none-elf-`，编译时要加 `-DCUSTOM_TARGET_TRIPLET`（已写在 `hpm_env.sh` 里）。

工程根目录的 [hpm_env.sh](hpm_env.sh) 设置好了所有环境变量。路径不同时改这个文件里的路径即可。

---

## 3. 编译固件

在工程根目录：

```bash
source ./hpm_env.sh
```

```bash
cmake -GNinja -B build -DBOARD=hpm5301evklite -DCUSTOM_TARGET_TRIPLET=$HPM_TARGET_TRIPLET
```

```bash
ninja -C build
```

- 默认是 Flash XIP + `-O3` 的发布构建（`CMakeLists.txt` 里设好了）。要单步调试时，改用 `-DCMAKE_BUILD_TYPE=debug` 重新配置。
- 产物在 `build/output/`：`demo.elf`（烧录、调试用）、`demo.bin`、`demo.map`。
- 改代码后只需重新运行 `ninja -C build`；新增源文件后要重新运行一次 cmake。

---

## 4. 烧录固件

用 J-Link 接板子的 **J5**（20 针 JTAG），板子由 J1 供电：

```bash
./flash.sh
```

脚本会烧录 `build/output/demo.elf`，然后复位运行；内容没变时会跳过写入。J-Link 软件不在默认路径时：

```bash
JLINK_EXE=/Applications/SEGGER/JLink/JLinkExe ./flash.sh
```

烧录后确认 USB 识别（J1 接电脑）：

```bash
ioreg -p IOUSB -w0 | grep -i probe
```

应该看到 `IO Probe CMSIS-DAP`，同时出现两个串口 `/dev/cu.usbmodem…2`（控制口）和 `…4`（目标串口）。Linux 上是两个 `/dev/ttyACM*`。

---

## 5. 安装电脑端工具

```bash
cd host
python3 -m venv .venv
.venv/bin/pip install -e '.[test,mcp]'
```

装好后有两个命令：`host/.venv/bin/ioprobe`（命令行）和 `host/.venv/bin/ioprobe-mcp`（MCP server）。下面的例子都在 `host/` 目录下运行，并省略了 `.venv/bin/` 前缀。

确认连上了：

```bash
.venv/bin/ioprobe info
```

```
protocol: 1
firmware: 0.6.0
max_payload: 4096
gpio_pins: [11, 27, 31, 32]
```

---

## 6. 使用 GPIO / I2C / SPI

### 6.1 命令行

约定：GPIO 写 J3 脚号（十进制）；I2C 地址和数据字节一律写**十六进制**；加 `--json` 输出机器可读结果，出错时退出码为 1。

**GPIO**

```bash
ioprobe gpio config 27 out --initial 1     # J3-27 设为推挽输出，初始高电平
ioprobe gpio write 27 0                    # 拉低
ioprobe gpio config 32 in --pull up        # J3-32 设为输入，内部上拉
ioprobe gpio read 32                       # 读一个脚（输出状态下也读实际电平）
ioprobe gpio read                          # 读全部 GPIO
```

**I2C**

```bash
ioprobe i2c speed 400000                   # 100000 / 400000 / 1000000
ioprobe i2c scan                           # 扫描 0x08–0x77
ioprobe i2c write 50 00 11 22              # 向 0x50 写 00 11 22
ioprobe i2c read 50 4                      # 从 0x50 读 4 字节
ioprobe i2c wr 68 1 75                     # 写 75 后重复起始读 1 字节（读寄存器 0x75）
```

**SPI**

```bash
ioprobe spi config --mode 0 --freq 10000000      # 模式 0–3，频率 Hz，--lsb 为低位先发
ioprobe spi xfer 9f 00 00 00 --cs 0              # 全双工，CS0 包住整个传输；返回同长度数据
ioprobe spi xfer 03 00 00 00 --cs 0 --keep-cs    # 传输后保持 CS 拉低，下一次接着读
ioprobe spi xfer 00 00 00 00 --cs 0              # 继续读，结束后释放 CS
ioprobe spi cs 1 on                              # 手动拉低 CS1（off 释放）
```

### 6.2 Python

```python
from ioprobe import Probe, ProbeError, Status

with Probe.open() as probe:               # 自动找到控制口
    print(probe.info())

    probe.gpio.config(27, "out", initial=0)
    probe.gpio.write(27, 1)
    print(probe.gpio.read(32))

    probe.i2c.config(400_000)
    print([hex(a) for a in probe.i2c.scan()])
    who_am_i = probe.i2c.read_reg(0x68, 0x75)          # 8 位寄存器地址
    probe.i2c.write_reg(0x68, 0x6B, b"\x00")

    probe.spi.config(mode=0, freq=10_000_000)
    jedec_id = probe.spi.transfer(b"\x9f\x00\x00\x00", cs=0)[1:]

    try:
        probe.i2c.read(0x3C, 1)
    except ProbeError as e:
        if e.status is Status.NACK:
            print("0x3C 没有设备应答")
```

出错时抛 `ProbeError`（`.status` 是 `Status` 枚举，如 `NACK`、`TIMEOUT`、`BUS_BUSY`），等不到回复抛 `ProbeTimeout`。完整的状态码见 [docs/spec.md](docs/spec.md) 第 5 节。

### 6.3 限制

- 单次传输最多 4093 字节（SPI）/ 4095 字节（I2C 读），更大的数据要分几次。
- 每条命令往返约 0.09 ms；大块数据约 9 MiB/s。

---

## 7. 使用 SWD 调试器（DAPLink）

IO Probe 同时是一个 **CMSIS-DAP v2** 调试器，OpenOCD、pyOCD、probe-rs、Keil 都能直接识别，不用装驱动。SWD 由 SPI2 产生，按 1.3 节接好 SWCLK / SWDIO / nRESET / GND。

以 STM32G0 为例（其他芯片换成对应的 `target/*.cfg`）：

**检测目标**

```bash
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -c "transport select swd" -c "adapter speed 16000" -f target/stm32g0x.cfg -c "init" -c "exit"
```

能看到 `SWD DPIDR 0x0bc11477` 和 `Cortex-M0+ processor detected` 就说明连上了。

**烧录**

```bash
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -c "transport select swd" -c "adapter speed 32000" -f target/stm32g0x.cfg -c "program firmware.elf verify reset exit"
```

`.bin` 文件要带地址：`program firmware.bin 0x08000000 verify reset exit`。

**读出 Flash（备份）**

```bash
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -c "transport select swd" -c "adapter speed 32000" -f target/stm32g0x.cfg -c "init" -c "halt" -c "dump_image backup.bin 0x08000000 0x40000" -c "resume" -c "exit"
```

**GDB 调试**：启动 OpenOCD（去掉最后的 `exit` 类命令），另开终端 `arm-none-eabi-gdb firmware.elf`，然后 `target extended-remote :3333`。

**时钟建议**：`adapter speed` 用 8000–40000（kHz）。STM32G0B1 实测读 Flash：16 MHz 约 494 KiB/s，32 MHz 约 765 KiB/s，40 MHz 约 841 KiB/s；256 KB 烧录加校验约 6.7 s。固件会自动避开不稳定的 23.5–25.5 MHz。

**连不上时**：
- 报 `cannot read IDR`：检查 SWDIO / SWCLK 是否接反、是否共地、目标是否上电。
- `halt` 超时、外设寄存器全读 0：目标被按在复位里，检查 NRST 接线和目标供电。
- 不稳定：把 `adapter speed` 降到 8000，缩短杜邦线。

---

## 8. 使用目标串口

第二个 USB 串口直接转发 J3-8（TX）/ J3-10（RX）。波特率、数据位、校验、停止位跟着电脑端的设置走（建议 1 Mbaud 以内）。

找到这个串口：

```bash
.venv/bin/python -c "from ioprobe import find_target_uart; print(find_target_uart())"
```

然后用任何串口工具打开，例如：

```bash
screen /dev/cu.usbmodemXXXX4 115200
```

或者在 Python 里：

```python
import serial
from ioprobe import find_target_uart

with serial.Serial(find_target_uart(), 115200, timeout=1) as uart:
    uart.write(b"hello\r\n")
    print(uart.read(64))
```

---

## 9. 让 AI 直接调用（MCP）

`host/ioprobe_mcp` 把 GPIO / I2C / SPI 包装成 13 个 MCP 工具：`probe_info`、`probe_reset`、`gpio_config`、`gpio_write`、`gpio_read`、`i2c_config`、`i2c_scan`、`i2c_write`、`i2c_read`、`i2c_write_read`、`spi_config`、`spi_transfer`、`spi_cs`。

工程根目录的 [.mcp.json](.mcp.json) 已经注册好。在工程目录里启动 Claude Code，第一次会询问是否信任 `ioprobe`，同意后就可以直接说：

> 扫描一下 I2C 总线，读出 0x68 的 0x75 寄存器

每个工具都返回 JSON：成功时 `{"ok": true, ...}`；失败时 `{"ok": false, "error": "NACK", "detail": ...}`，AI 可以据此判断待测设备是否正常。

`.mcp.json` 里的 Python 路径是本机路径；别人使用时设置环境变量 `IOPROBE_PYTHON` 指向自己的 `host/.venv/bin/python` 即可。SWD 和目标串口不走 MCP，AI 可以直接调用 OpenOCD 和串口工具。

---

## 10. 测试

```bash
cd host
.venv/bin/python -m pytest                    # 不需要硬件：用模拟固件测协议、库、命令行、MCP
.venv/bin/python -m pytest --hardware         # 再加上真实板子的测试（需要接 J1）
```

带回环线的测试（先接好线，再设置对应环境变量）：

| 测试 | 跳线 | 环境变量 |
|---|---|---|
| SPI 回环 | J3-19 ↔ J3-21 | `IOPROBE_SPI_LOOP=1` |
| GPIO 回环 | J3-27 ↔ J3-32 | `IOPROBE_GPIO_LOOP=1` |
| 目标串口回环 | J3-8 ↔ J3-10 | `IOPROBE_UART_LOOP=1` |

例如：`IOPROBE_SPI_LOOP=1 .venv/bin/python -m pytest --hardware`。

---

## 11. 目录结构与文档

```
HPM5301EVKLite/
├── CMakeLists.txt          固件构建（默认 flash_xip + -O3）
├── hpm_env.sh              编译环境变量
├── flash.sh / flash.jlink  J-Link 烧录脚本
├── config/usb_config.h     CherryUSB 配置（VID 0x34B7 / PID 0x5301）
├── src/                    固件
│   ├── main.c              主循环：DAP、目标串口、IO Probe 协议
│   ├── usb_link.c          USB 复合设备（DAP + 2 个 CDC）
│   ├── protocol.c / .h     IO Probe 协议帧（协议定义在 protocol.h）
│   ├── cmd_*.c             系统 / GPIO / I2C / SPI 命令
│   ├── pins.c              GPIO 引脚表
│   ├── target_uart.c       目标串口转发
│   ├── dap_link.c          CMSIS-DAP bulk 传输
│   └── dap/                DAP 配置和 SPI SWD 实现
├── third_party/cherrydap/  CMSIS-DAP 核心（Apache-2.0，固定版本）
├── host/                   电脑端：Python 库、命令行、MCP server、测试
├── docs/
│   ├── spec.md             规格书（功能、参数、命令、性能、故障代码）
│   ├── pinout.md           引脚详细说明、实测记录、待核实项
│   └── quality.md          质量履历：待测试项目、已知问题、版本履历
└── .mcp.json               Claude Code 的 MCP 注册
```

已知问题和还没测的项目都记在 [docs/quality.md](docs/quality.md)，其中最需要注意的：
- J1 要用 A-to-C 线（C-to-C 接 USB-C 口不上电）。
- IO 只支持 3.3 V，没有保护。
- J3-7 和 J3-37 不要使用。
