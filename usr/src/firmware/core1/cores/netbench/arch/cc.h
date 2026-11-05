/*
 * arch/cc.h -- lwIP's port for core 1: newlib, little-endian Cortex-A9, no
 * output (core 0 owns the UART): diagnostics go nowhere, an assertion faults.
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>         /* ssize_t (Xilinx's etharp.h) */

/* BYTE_ORDER: newlib's, from sys/types.h */
#define LWIP_RAND()                 ((u32_t)rand())
#define LWIP_PLATFORM_DIAG(x)       do { } while (0)
#define LWIP_PLATFORM_ASSERT(x)     do { __builtin_trap(); } while (0)
#define LWIP_NO_STDINT_H            0
#define LWIP_NO_INTTYPES_H          0

#endif
