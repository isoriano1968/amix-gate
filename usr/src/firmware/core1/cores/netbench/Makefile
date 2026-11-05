# netbench: core 1's own network (lwIP 2.1.3 over core 0's rings, c1netif.c)
# on the Z3660's core 1, in h264bench's harness (arm_main.c, lscript.ld, the
# BSP); needs the firmware with C1LD_FEAT_NET.  lwIP is the release in the
# firmware's BSP tree, untouched; our port is lwipopts.h and arch/cc.h.
CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
SIZE    := $(CROSS_COMPILE)size
OBJCOPY := $(CROSS_COMPILE)objcopy
HB      := $(abspath ../h264bench)
Z3660   ?= $(abspath ../../../../../../../upstream/Z3660-beta21)
BSP     := $(Z3660)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_1/standalone_ps7_cortexa9_1/bsp/ps7_cortexa9_1
LWIP    ?= $(abspath ../../../../../../../upstream/Z3660-fork10)/z3660-firmware/Z-TURN/vitis_ide/design_1_wrapper/ps7_cortexa9_0/standalone_domain/bsp/ps7_cortexa9_0/libsrc/lwip213_v1_1/src/lwip-2.1.3/src
V       := $(abspath ../..)
CPU     := -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard
CFLAGS  := $(CPU) -O2 -Wall -ffunction-sections -fdata-sections \
           -I. -I$(BSP)/include -I$(HB) -I$(V)/ffmpeg-4.4.8 -I$(V)/build-arm -I$(V)/zzcrypto -I$(LWIP)/include
LDFLAGS := $(CPU) -T $(HB)/lscript.ld -specs=$(HB)/Xilinx.spec \
           -Wl,--gc-sections,-build-id=none,-Map=build/netbench.map,--no-enum-size-warning -L$(BSP)/lib
LIBS    := -Wl,--start-group,-lxil,-lgcc,-lm,-lc,--end-group

LWIP_SRCS := $(addprefix $(LWIP)/core/,init.c def.c dns.c inet_chksum.c ip.c mem.c memp.c netif.c pbuf.c \
             raw.c stats.c sys.c tcp.c tcp_in.c tcp_out.c timeouts.c udp.c) \
             $(addprefix $(LWIP)/core/ipv4/,dhcp.c etharp.c icmp.c ip4.c ip4_addr.c ip4_frag.c) \
             $(LWIP)/netif/ethernet.c
LWIP_OBJS := $(patsubst $(LWIP)/%.c,build/lwip/%.o,$(LWIP_SRCS))

all: build/z3660ARM_Core_netbench.bin

build/z3660ARM_Core_netbench.bin: build/netbench.elf
	$(OBJCOPY) -O binary -j .benchid -j .text -j .init -j .fini -j .rodata -j .data \
	    -j .mmu_tbl -j '.ARM.extab*' -j .ARM.exidx -j .init_array -j .fini_array $< $@
	ls -l $@

build/xil-crt0.o: $(BSP)/libsrc/standalone_v9_0/src/xil-crt0.S
	@mkdir -p build
	$(CC) $(CPU) -I$(BSP)/include -c $< -o $@

build/arm_main.o: $(HB)/arm_main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/lwip/%.o: $(LWIP)/%.c lwipopts.h arch/cc.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.c lwipopts.h c1netif.h $(HB)/core1_net.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/netbench.elf: build/arm_main.o build/netbench.o build/c1netif.o build/xil-crt0.o $(LWIP_OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@

clean:
	rm -rf build
