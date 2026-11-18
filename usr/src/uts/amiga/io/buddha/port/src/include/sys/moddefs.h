/* SPDX-License-Identifier: MIT */
/*
 * sys/moddefs.h -- the buddha kit's stand-in for the amigaux module ABI kit's
 * wrapper header.  The kit links the driver into the kernel: there is no
 * module core, so the wrapper that names the module's entries makes nothing.
 */
#define MOD_HDRV_WRAPPER(name, load, unload, halt, desc)
