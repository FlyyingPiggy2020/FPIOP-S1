# ioprobe 电脑端库和命令行

通过 USB 驱动 HPM5301 IO Probe 固件，可以操作 I2C、SPI 和 GPIO。
协议定义在 `ioprobe/protocol.py`，必须和固件里的 `src/protocol.h` 保持一致。引脚分配见 `../docs/pinout.md`。

从固件 0.2.0 起，同一根 USB 线上有三个功能（0.4.0 起序列号由芯片 UUID 生成，库按序列号把两个串口归到同一块板）：

| 功能 | 电脑上看到的 | 怎么用 |
|---|---|---|
| IO Probe 控制 | 第一个串口（macOS 上形如 `/dev/cu.usbmodem<位置号>2`） | 本库、命令行、MCP server，`find_probe()` 自动找到 |
| 目标串口 | 第二个串口（`…<位置号>4`），转发 J3-8 TX / J3-10 RX | 任何串口工具；`find_target_uart()` 返回它的路径 |
| DAPLink 调试器 | CMSIS-DAP v2（VID:PID 34B7:5301） | OpenOCD / pyOCD / probe-rs / Keil，SWD 接 J3-27/28/29/30 |

## 安装

```bash
cd host
python3 -m venv .venv
.venv/bin/pip install -e '.[test]'
```

## 命令行

约定：
- I2C 地址和数据字节一律按十六进制写。
- GPIO 引脚写 J3 排针编号（十进制）。
- 加 `--json` 输出机器可读的结果，出错时退出码为 1。

```bash
.venv/bin/ioprobe info
.venv/bin/ioprobe gpio config 11 out --initial 1
.venv/bin/ioprobe gpio read            # 读所有 probe GPIO
.venv/bin/ioprobe i2c speed 400000
.venv/bin/ioprobe i2c scan
.venv/bin/ioprobe i2c wr 50 2 00       # 先写 00，再从 0x50 读 2 字节（重复起始）
.venv/bin/ioprobe spi config --mode 0 --freq 10000000
.venv/bin/ioprobe spi xfer 9f 00 00 00 --cs 0
```

## Python

```python
from ioprobe import Probe, ProbeError

with Probe.open() as probe:          # 自动查找 USB 34B7:5301
    print(probe.i2c.scan())
    chip_id = probe.i2c.read_reg(0x68, 0x75)
    rx = probe.spi.transfer(b"\x9f\x00\x00\x00", cs=0)
```

设备回复的状态码不是 OK 时，会抛出 `ProbeError`，`.status` 是 `Status` 枚举，比如 `Status.NACK`。
等不到回复时，会抛出 `ProbeTimeout`。

## 测试

```bash
.venv/bin/python -m pytest                 # 不需要硬件：用模拟固件测协议、API 和命令行
.venv/bin/python -m pytest --hardware      # 再加上真实板子的测试
IOPROBE_SPI_LOOP=1 .venv/bin/python -m pytest --hardware   # 需要先用线把 J3-19 和 J3-21 连起来
```

## MCP server（让 AI 直接调用）

`ioprobe_mcp` 把 probe 包装成 13 个 MCP 工具：
- 系统：`probe_info`、`probe_reset`
- GPIO：`gpio_config`、`gpio_write`、`gpio_read`
- I2C：`i2c_config`、`i2c_scan`、`i2c_write`、`i2c_read`、`i2c_write_read`
- SPI：`spi_config`、`spi_transfer`、`spi_cs`

每个工具都返回 JSON：
- 成功时带 `ok: true`、结果字段和 `elapsed_ms`（往返耗时）。
- 失败时带 `ok: false`、`error`（比如 `NACK`、`TIMEOUT`、`BAD_ARG`、`NOT_CONNECTED`）和 `detail`。

USB 断开或超时后，下一次调用会自动重新连接。

工程根目录的 `../.mcp.json` 已经注册好了这个 server，在工程目录里启动 Claude Code 就能用（第一次会问你是否信任这个 server）。
Python 路径默认是本机的 `host/.venv/bin/python`。别人使用时可以先设置环境变量 `IOPROBE_PYTHON` 指向自己的解释器，不用改文件。

手动启动 server：`.venv/bin/ioprobe-mcp`，或者 `.venv/bin/python -m ioprobe_mcp`（走 stdio）。

## DAPLink（SWD 调试器）

接线：J3-27 SWDIO、J3-28 SWCLK、J3-29 nRESET、J3-30 GND，四个脚在排针上是一个 2×2 方块。目标板必须是 3.3 V。以 STM32F1 为例，用 OpenOCD 读芯片：

```bash
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -c "transport select swd" -f target/stm32f1x.cfg
```

用 OpenOCD 烧录：

```bash
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -c "transport select swd" -f target/stm32f1x.cfg -c "program firmware.elf verify reset exit"
```

pyOCD 用 `pyocd list` 能看到这个调试器。没接目标板时，OpenOCD 会报 `Error connecting DP: cannot read IDR`，这是正常的。
