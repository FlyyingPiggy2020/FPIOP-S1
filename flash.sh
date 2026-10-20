#!/bin/sh
# Flash build/output/demo.elf to HPM5301EVKLite via J-Link (JTAG), then reset and run.
# Set JLINK_EXE to your own JLinkExe path, e.g. JLINK_EXE=/Applications/SEGGER/JLink/JLinkExe ./flash.sh
JLINK_EXE="${JLINK_EXE:-/Users/lxf/sdk/jlink/JLinkExe}"

if [ ! -x "$JLINK_EXE" ]; then
    echo "JLinkExe not found at: $JLINK_EXE" >&2
    echo "Set JLINK_EXE to your JLinkExe path and try again." >&2
    exit 1
fi

cd "$(dirname "$0")" && "$JLINK_EXE" -NoGui 1 -Device HPM5301xEGx -If JTAG -JTAGConf -1,-1 -Speed 4000 -CommandFile flash.jlink </dev/null
