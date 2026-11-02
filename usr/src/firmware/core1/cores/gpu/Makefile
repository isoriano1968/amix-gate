# gpu: zzgl's server (Mesa 18.3 OSMesa + softpipe) on the Z3660's core 1, in
# h264bench's harness (arm_main.c, lscript.ld, the BSP).
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
SIZE    := $(CROSS_COMPILE)size
OBJCOPY := $(CROSS_COMPILE)objcopy
HB      := $(abspath ../h264bench)
Z3660   ?= $(abspath ../../../../../../../upstream/Z3660-beta21)
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1
V       := $(abspath ../..)
G       := $(abspath ../..)
CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
CFLAGS  := $(CPU) -O2 -Wall -ffunction-sections -fdata-sections -fno-short-enums \
           -I$(BSP)/include -I$(HB) -I$(V)/ffmpeg-4.4.8 -I$(V)/build-arm \
           -I$(G)/zzgl -I$(G)/src/mesa-18.3.6/include
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec \
           -Wl,--gc-sections,-build-id=none,-Map=build/gpu.map,--no-enum-size-warning,--defsym=_HEAP_SIZE=0x07500000 -L$(BSP)/lib
LIBS    := $(G)/build/arm/libOSMesa.a \
           -Wl,--start-group,-lxil,-lstdc++,-lgcc,-lm,-lc,--end-group

all: build/z3660ARM_Core_gpu.bin

build/z3660ARM_Core_gpu.bin: build/gpu.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	ls -l $@

build/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p build
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@

# the heap from 0x30000000 too (arm_main.c): about 245 MiB with the 117 MiB
# above the image (to just below RELOAD_TRAMP)
build/arm_main.o: $(HB)/arm_main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -DHB_HEAP_LOW -c $< -o $@

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/zzgl_dec.o: $(G)/zzgl/zzgl_dec.c $(G)/zzgl/zzgl.h $(G)/zzgl/gen/zzgl_gen_dec.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(G)/zzgl/gen/zzgl_gen_dec.h: $(G)/zzgl/zzgl.h $(G)/zzgl/gen.py
	@mkdir -p $(G)/zzgl/gen
	python3 -I $(G)/zzgl/gen.py $(G)/zzgl/gen

build/spook.o: $(G)/zzgl/spook.c $(G)/zzgl/spook_font.h
	@mkdir -p build
	$(CC) $(CFLAGS) -O3 -c $< -o $@

build/armgl_compat.o: $(G)/compat/arm/armgl_compat.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(G)/compat/arm -c $< -o $@

build/gpu.elf: build/arm_main.o build/gpu.o build/zzgl_dec.o build/spook.o build/armgl_compat.o build/xil-crt0.o $(G)/build/arm/libOSMesa.a
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@

clean:
	rm -rf build
