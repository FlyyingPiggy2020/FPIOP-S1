/*
 * GPIO bit-bang SWD backend: the unmodified CMSIS-DAP reference implementation,
 * compiled with its entry points renamed so sw_dp.c can dispatch between backends.
 */
#define SWJ_Sequence IO_SWJ_Sequence
#define SWD_Sequence IO_SWD_Sequence
#define SWD_Transfer IO_SWD_Transfer

#include "../../third_party/cherrydap/Source/SW_DP.c"
