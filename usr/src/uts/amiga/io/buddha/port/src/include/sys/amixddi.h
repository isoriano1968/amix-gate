/* SPDX-License-Identifier: MIT */
/*
 * sys/amixddi.h -- the buddha kit's stand-in for the amigaux module core's
 * DDI header: the four calls the module form of ide.c makes.  buddha_kit.c
 * defines them for a kernel without a module core (see there).
 */
extern int	sd_register(), root_defer(), intr_attach(), intr_detach();
