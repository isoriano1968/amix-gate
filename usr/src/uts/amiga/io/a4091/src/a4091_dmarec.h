/* SPDX-License-Identifier: MIT */
/*
 * a4091_dmarec.h -- the D42 DMA-coherence seam as the A4091 calls it (040/060
 * driver line only; included by a4091-wr.c under -DA4091_KVA).
 *
 * D42 as agreed with the port author (2026-09-28): ONE protocol and
 * state machine, in the port's src/dma_cache040.s; the per-controller RECORD
 * lives in each controller's own object.  Names, magic and offsets below are
 * the agreed ones (docs/D42-CALL-SHAPE.md).  The service is not written yet, so
 * until it is linked a4091-wr.c builds local stubs (no cache maintenance at all)
 * unless -DA4091_D42 is given.
 *
 * Every field is a longword so the service can reach it with movel and a fixed
 * displacement; the offsets are the contract.  All service fields are
 * contiguous (0x14..0x4c, one kpeek), all driver fields 0x50..0x5c.
 *   D = the driver writes it (the service only reads it)
 *   S = the service writes it (the driver only reads it, for diagnostics)
 *
 * Cache ops, per 16-byte line of the owned range rounded outward:
 *   prepare  cpushl dc then cinvl dc (040: cinvl is a no-op after the push;
 *            060: correct whatever CACR.DPI is)
 *   complete FROM_DEVICE: cinvl dc.  TO_DEVICE: no op.  NO re-push of edges.
 * Edge lines: a FROM_DEVICE range whose pa or pa+len is not on a 16-byte
 * boundary shares a line with unrelated bytes; a CPU write there mid-transfer
 * is lost either way at complete.  The service does not try to repair it; it
 * counts the range in edge_shared, and the acceptance invariant requires
 * edge_shared == 0.  TO_DEVICE ranges are never counted (no op at complete).
 */
#ifndef	A4091_DMAREC_H
#define	A4091_DMAREC_H

#define	DMA_REC_MAGIC		0x444D4152	/* "DMAR": static, proves the address   */
#define	DMA_REC_VERSION		2		/* 2: edge_shared at 0x48, size 0x60    */

#define	DMA_TO_DEVICE		0		/* device reads RAM  (SCSI write)       */
#define	DMA_FROM_DEVICE		1		/* device writes RAM (SCSI read)        */

#define	DMA_EMPTY		0		/* state values (service-owned)         */
#define	DMA_PREPARING		1
#define	DMA_PREPARED		2

struct dma_rec {
	ulong	magic;		/* 0x00 D  DMA_REC_MAGIC                               */
	ulong	version;	/* 0x04 D  DMA_REC_VERSION                             */
	ulong	ctlid;		/* 0x08 D  controller identity: AutoConfig product id   */
	ulong	unit;		/* 0x0c D  controller instance (sd card number)         */
	ulong	off;		/* 0x10 D  nonzero: seam OFF (service counts, no op)    */
	ulong	state;		/* 0x14 S  EMPTY / PREPARING / PREPARED                 */
	ulong	dir;		/* 0x18 S  direction of the owned segment               */
	ulong	pa;		/* 0x1c S  owned segment: physical base                 */
	ulong	len;		/* 0x20 S  owned segment: length in bytes               */
	ulong	seq;		/* 0x24 S  prepares that took ownership                 */
	ulong	prep_to;	/* 0x28 S  prepares, TO_DEVICE                          */
	ulong	prep_from;	/* 0x2c S  prepares, FROM_DEVICE                        */
	ulong	cmpl_to;	/* 0x30 S  completes consumed, TO_DEVICE                */
	ulong	cmpl_from;	/* 0x34 S  completes consumed, FROM_DEVICE              */
	ulong	prep_owned;	/* 0x38 S  prepare while a segment is still owned       */
	ulong	cmpl_noprep;	/* 0x3c S  complete with no PREPARED record             */
	ulong	cmpl_mismatch;	/* 0x40 S  complete (pa,len,dir) != the owned segment   */
	ulong	range_ovf;	/* 0x44 S  pa + len wrapped: refused                    */
	ulong	edge_shared;	/* 0x48 S  FROM_DEVICE range not 16-byte aligned        */
	ulong	off_n;		/* 0x4c S  calls seen while off != 0                    */
	ulong	drv_prep;	/* 0x50 D  prepare calls made by the driver             */
	ulong	drv_cmpl;	/* 0x54 D  complete calls made by the driver            */
	ulong	drv_zero;	/* 0x58 D  zero-length transfers: no call made          */
	ulong	drv_quiesce;	/* 0x5c D  completes that followed a forced quiesce     */
};				/* 0x60 bytes */

/*
 * dma_prepare  -- before the driver arms the device (the DSP write that starts
 *                 SCRIPTS).  Takes ownership of [pa, pa+len) and pushes (then
 *                 invalidates) the CPU's lines over it.  Returns 0 when the
 *                 segment is PREPARED, nonzero when it is not (off, still owned,
 *                 bad range); the driver arms either way -- nothing may panic.
 * dma_complete -- after the device has stopped touching the segment (its
 *                 completion, or a forced quiesce), before the sd callback.
 *                 FROM_DEVICE: invalidate the owned lines; TO_DEVICE: no op.
 *                 Consumes the record (state -> EMPTY).
 * pa is PHYSICAL (sdcom.addr is physical by contract on this line); the service
 * works by physical line, so one op covers every virtual alias of the page.
 */
#ifdef	A4091_D42
extern int	dma_prepare();	/* (struct dma_rec *, ulong pa, ulong len, int dir) */
extern void	dma_complete();	/* (struct dma_rec *, ulong pa, ulong len, int dir) */
#endif

#endif	/* A4091_DMAREC_H */
