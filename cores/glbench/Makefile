# glbench: osbench (Mesa 18.3 OSMesa + softpipe) on the Z3660's core 1, in
# h264bench's harness (arm_main.c, lscript.ld, the BSP).
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
SIZE    := $(CROSS_COMPILE)size
OBJCOPY := $(CROSS_COMPILE)objcopy
HB      := $(abspath ../h264bench)
Z3660   ?= $(abspath ../../../Z3660)
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1
V       := $(abspath ../..)
G       := $(abspath ../..)
CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
CFLAGS  := $(CPU) -O2 -Wall -ffunction-sections -fdata-sections -fno-short-enums \
           -I$(BSP)/include -I$(HB) -I$(V)/ffmpeg-4.4.8 -I$(V)/build-arm \
           -I$(G)/bench -I$(G)/src/mesa-18.3.6/include
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec \
           -Wl,--gc-sections,-build-id=none,-Map=build/glbench.map,--no-enum-size-warning -L$(BSP)/lib
LIBS    := $(G)/build/arm/libOSMesa.a \
           -Wl,--start-group,-lxil,-lstdc++,-lgcc,-lm,-lc,--end-group

all: build/z3660ARM_Core_glbench.bin

build/z3660ARM_Core_glbench.bin: build/glbench.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	ls -l $@

build/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p build
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@

build/arm_main.o: $(HB)/arm_main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/osbench.o: $(G)/bench/osbench.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/armgl_compat.o: $(G)/compat/arm/armgl_compat.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(G)/compat/arm -c $< -o $@

build/glbench.elf: build/arm_main.o build/glbench.o build/osbench.o build/armgl_compat.o build/xil-crt0.o $(G)/build/arm/libOSMesa.a
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@

clean:
	rm -rf build
