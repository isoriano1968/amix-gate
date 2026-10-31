/*
 * paulatest.c -- first light for /dev/paula (src/paula.c): plays a short
 * scale on channels 0 and 1 from a looped 32-byte waveform, through the
 * driver's tick queue, and checks the tick rate and the exclusion with
 * /dev/audio.
 *
 *   paulatest            needs /dev/paula (mknod /dev/paula c 53 0)
 *
 * Expected: about 50 ticks a second at 125 BPM (CIA-A timer B), "idle"
 * small (the queue is kept full), /dev/audio refused while open.
 * Built with the native cc on AMIX: cc -o paulatest paulatest.c
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <poll.h>
#include "paula.h"

static long now_ms()
{
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *)0);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static int fd;

static void op(t, o, c, v, off)
struct paula_tick *t;
int o, c, v;
unsigned long off;
{
	struct paula_op *p = &t->op[t->nops++];

	p->op = o;
	p->ch = c;
	p->val = v;
	p->off = off;
}

static void push(t)
struct paula_tick *t;
{
	while (ioctl(fd, PAULA_PUSH, (char *)t) < 0) {
		if (errno != EAGAIN) {
			perror("PAULA_PUSH");
			exit(1);
		}
		poll((struct pollfd *)0, 0, 10);	/* the queue is full */
	}
}

int main()
{
	static short notes[] = { 428, 381, 339, 320, 285, 254, 226, 214 };	/* C-2 .. C-3 */
	struct paula_tick t;
	struct paula_load ld;
	struct paula_status st;
	char wave[64];
	long t0, t1;
	int i, n, a;
	unsigned long played0;

	fd = open("/dev/paula", O_RDWR);
	if (fd < 0) {
		perror("/dev/paula");
		return 1;
	}
	a = open("/dev/audio", O_WRONLY);
	printf("/dev/audio while /dev/paula is open: %s\n",
	    a < 0 ? (errno == EBUSY ? "refused, EBUSY (right)" : "refused, other error") : "OPENED (wrong)");
	if (a >= 0)
		close(a);

	/* a soft square in the first 32 bytes, a saw in the next 32 */
	for (i = 0; i < 32; i++) {
		wave[i] = i < 16 ? 90 : -90;
		wave[32 + i] = (char)(i * 8 - 128);
	}
	if (ioctl(fd, PAULA_SETUP, (char *)1024) < 0) {
		perror("PAULA_SETUP");
		return 1;
	}
	ld.off = 0;
	ld.len = sizeof wave;
	ld.data = wave;
	if (ioctl(fd, PAULA_LOAD, (char *)&ld) < 0) {
		perror("PAULA_LOAD");
		return 1;
	}

	ioctl(fd, PAULA_STATUS, (char *)&st);
	played0 = st.played;
	t0 = now_ms();
	for (n = 0; n < 8 * 25; n++) {		/* 8 notes of 25 ticks, 4 s at 125 BPM */
		t.nops = 0;
		if (n == 0)
			op(&t, PO_TEMPO, 0, 125, 0L);
		if (n % 25 == 0) {
			int k = n / 25;
			/* ProTracker's order: off, set, on, then the loop part */
			op(&t, PO_DMA, 0, 0x0003, 0L);
			op(&t, PO_LEN, 0, 16, 0L);
			op(&t, PO_LC, 0, 0, 0L);
			op(&t, PO_PER, 0, notes[k], 0L);
			op(&t, PO_VOL, 0, 48, 0L);
			op(&t, PO_LEN, 1, 16, 0L);
			op(&t, PO_LC, 1, 0, 32L);
			op(&t, PO_PER, 1, notes[k] * 2, 0L);
			op(&t, PO_VOL, 1, 32, 0L);
			op(&t, PO_DMA, 0, 0x8003, 0L);
			op(&t, PO_LC, 0, 0, 0L);
			op(&t, PO_LEN, 0, 16, 0L);
			op(&t, PO_LC, 1, 0, 32L);
			op(&t, PO_LEN, 1, 16, 0L);
		}
		push(&t);
	}
	/* let the queue play out */
	do {
		poll((struct pollfd *)0, 0, 50);
		ioctl(fd, PAULA_STATUS, (char *)&st);
	} while (st.queued > 0 && now_ms() - t0 < 10000);
	t1 = now_ms();
	printf("played %lu ticks in %ld ms: %.1f per second (expect 50.0); idle %lu, area %lu\n",
	    st.played - played0, t1 - t0, (st.played - played0) * 1000.0 / (t1 - t0), st.idle, st.area);
	close(fd);
	return 0;
}
