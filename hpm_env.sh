# HPMicro SDK environment — usage: source ./hpm_env.sh
export HPM_SDK_BASE=/Users/lxf/sdk/hpm_sdk
export GNURISCV_TOOLCHAIN_PATH=/Users/lxf/sdk/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1
export HPM_SDK_TOOLCHAIN_VARIANT=
# xPack toolchain uses the riscv-none-elf- prefix: pass -DCUSTOM_TARGET_TRIPLET=$HPM_TARGET_TRIPLET to cmake
export HPM_TARGET_TRIPLET=riscv-none-elf
export PATH=$GNURISCV_TOOLCHAIN_PATH/bin:$PATH
source /Users/lxf/sdk/.venv/bin/activate
