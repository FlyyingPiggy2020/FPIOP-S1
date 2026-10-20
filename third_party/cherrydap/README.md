# CherryDAP (vendored subset)

Source: https://github.com/cherry-embedded/CherryDAP @ 43d2b4cca77c7e78dd4f5665447d65a65fc6ed0e
License: Apache-2.0 (see LICENSE; the DAP sources also carry ARM's Apache-2.0 headers).

Copied unchanged:

| File | Upstream path |
|---|---|
| Include/DAP.h | DAP/Include/DAP.h |
| Source/DAP.c | DAP/Source/DAP.c (CMSIS-DAP command processor) |
| Source/SW_DP.c | DAP/Source/SW_DP.c (generic GPIO bit-bang SWD) |
| Source/DAP_vendor.c | DAP/Source/DAP_vendor.c (empty vendor-command template) |

The board-specific `DAP_config.h` (pins, timing, SWD-only) lives in `src/dap/` and is ours.
Not taken: JTAG/cJTAG, SPI-accelerated SWD, SWO, HSLink settings, CherryDAP's own dap_main.c (USB glue is in src/usb_link.c).
To update: download the new commit, re-copy these four files, and update the hash above.
