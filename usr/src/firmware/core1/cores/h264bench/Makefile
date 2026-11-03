# h264bench for the Z3660's core 1: FFmpeg's H.264 decoder, timed.
#   make Z3660=<Z3660 checkout> [BSP_CRT0_DIR=<dir with xil-crt0.o>]
# Needs arm-none-eabi-gcc on PATH and ../../build-arm (configure-arm.sh).
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
SIZE    := $(CROSS_COMPILE)size
OBJCOPY := $(CROSS_COMPILE)objcopy

Z3660   ?= $(abspath ../../../../../../../upstream/Z3660-beta21)
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1
# xil-crt0.o is not in the Z3660 repo: assembled from the BSP's xil-crt0.S
BSP_CRT0_DIR ?= build
V       := $(abspath ../..)
FF      := $(V)/build-arm

CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
CFLAGS  := $(CPU) -O2 -Wall -ffunction-sections -fdata-sections \
           -I$(BSP)/include -I$(V)/ffmpeg-4.4.8 -I$(FF) -I$(V)/zzcrypto
LDFLAGS := $(CPU) -T lscript.ld -specs=Xilinx.spec -Wl,--gc-sections,-build-id=none,-Map=build/h264bench.map \
           -L$(BSP)/lib
LIBS    := $(BSP_CRT0_DIR)/xil-crt0.o $(FF)/libavcodec/libavcodec.a $(FF)/libavutil/libavutil.a \
           -Wl,--start-group,-lxil,-lgcc,-lm,-lc,--end-group

# h264bench.elf goes into a BOOT image; z3660ARM_Core_h264bench.bin is the same
# program for core 0's loader or a reload, sent from AMIX (armbench -load/-reload);
# core 1 programs are named z3660ARM_Core_<name>
all: build/h264bench.elf build/z3660ARM_Core_h264bench.bin

build/z3660ARM_Core_h264bench.bin: build/h264bench.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j .ARM.exidx -j .init_array -j .fini_array $< $@

build/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p build
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@

build/%.o: %.c bench.h zzav.h player.h yuvrow.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/h264bench.elf: build/arm_main.o build/decode.o build/player.o $(BSP_CRT0_DIR)/xil-crt0.o lscript.ld
	$(CC) $(LDFLAGS) build/arm_main.o build/decode.o build/player.o $(LIBS) -o $@
	$(SIZE) $@

clean:
	rm -rf build
