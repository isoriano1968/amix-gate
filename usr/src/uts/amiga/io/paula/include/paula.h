/*
 * paula.h -- /dev/paula: Paula's four audio channels for one program,
 * register by register, replayed by CIA-A timer B (AMIX 68040/68060 port,
 * 2026-10-27).  Shared by the kernel driver (src/paula.c) and its users.
 *
 * A tracker drives Paula as on AmigaOS: per replayer tick it sets sample
 * pointer, length, period and volume and switches channel DMA; Paula plays
 * the samples from chip RAM by itself.  The program queues ticks ahead
 * (PAULA_PUSH); CIA-A timer B, running at the song's tempo, applies one
 * queued tick per interrupt.  Sample data lives in a chip-RAM area of the
 * driver's (PAULA_SETUP, PAULA_LOAD); pointers are offsets into it.
 */
#ifndef _PAULA_H
#define _PAULA_H

#define PAULAIOC	('P' << 8)
#define PAULA_SETUP	(PAULAIOC | 1)	/* unsigned long: sample area bytes (0: none) */
#define PAULA_LOAD	(PAULAIOC | 2)	/* struct paula_load */
#define PAULA_PUSH	(PAULAIOC | 3)	/* struct paula_tick; EAGAIN when the queue is full */
#define PAULA_STATUS	(PAULAIOC | 4)	/* struct paula_status */
#define PAULA_RESET	(PAULAIOC | 5)	/* drop the queue, stop the channels */

#define PAULA_MAXAREA	(1536L * 1024)	/* sample area limit */
#define PAULA_MAXOPS	48		/* operations per tick */
#define PAULA_RING	64		/* queued ticks */
#define PAULA_NULLOFF	0xffffffffUL	/* PO_LC: a silent word */

/* operations; ch is 0..3 */
#define PO_LC		1	/* off: sample start, offset in the area (or PAULA_NULLOFF) */
#define PO_LEN		2	/* val: length in words (0 = 65536) */
#define PO_PER		3	/* val: period */
#define PO_VOL		4	/* val: volume 0..64 */
#define PO_DMA		5	/* val: DMACON bits 0..3, bit 15 set = on, clear = off */
#define PO_LED		6	/* val: 1 = "LED" low-pass filter on */
#define PO_TEMPO	7	/* val: BPM 32..255 (timer B rate from the next tick) */

struct paula_op {
	unsigned char	op, ch;
	unsigned short	val;
	unsigned long	off;
};

struct paula_tick {
	unsigned short	nops;
	unsigned short	pad;
	struct paula_op	op[PAULA_MAXOPS];
};

struct paula_load {
	unsigned long	off, len;
	char		*data;
};

struct paula_status {
	unsigned long	played;		/* ticks applied */
	unsigned long	idle;		/* timer ticks that found the queue empty */
	unsigned long	queued;		/* ticks waiting */
	unsigned long	area;		/* sample area bytes */
};

#endif /* _PAULA_H */
