/*
 * Small random/urandom character device for AMIX.
 *
 * This is a compatibility source for software that expects /dev/random
 * or /dev/urandom.  It does not try to model modern entropy accounting;
 * both minor 0 and minor 1 are non-blocking PRNG output.  Seed material
 * written to either device is mixed into the generator.
 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/uio.h"
#include "sys/errno.h"
#include "sys/systm.h"
#include "sys/proc.h"
#include "sys/time.h"
#include "sys/user.h"

#define RANDOM_MINOR	0
#define URANDOM_MINOR	1
#define RAND_BUFSIZE	256

extern clock_t lbolt;

static unsigned long rand_s[4];
static int rand_seeded;

/*
 * A macro, not a function: this kernel maps its data cache-inhibited
 * (DTT0), so every call's stack traffic goes to the bus.
 */
#define rand_rotl(x, n)	(((x) << (n)) | ((x) >> (32 - (n))))

static void
rand_mix(v)
unsigned long v;
{
	rand_s[0] ^= v + 0x9e3779b9UL + (rand_s[3] << 6) + (rand_s[3] >> 2);
	rand_s[1] += rand_s[0] ^ rand_rotl(v, 7);
	rand_s[2] ^= rand_s[1] + rand_rotl(v, 13);
	rand_s[3] += rand_s[2] ^ rand_rotl(rand_s[0], 17);
}

static void
rand_stir()
{
	rand_mix((unsigned long)hrestime.tv_sec);
	rand_mix((unsigned long)hrestime.tv_nsec);
	rand_mix((unsigned long)lbolt);
	rand_mix((unsigned long)&rand_s[0]);
	rand_mix((unsigned long)u.u_procp);
	if (u.u_procp)
		rand_mix((unsigned long)u.u_procp->p_pid);
}

static void
rand_seed()
{
	if (rand_seeded)
		return;

	rand_s[0] = 0x243f6a88UL;
	rand_s[1] = 0x85a308d3UL;
	rand_s[2] = 0x13198a2eUL;
	rand_s[3] = 0x03707344UL;
	rand_stir();
	rand_seeded = 1;
}

/*
 * Fill p[0..n-1] with generator output.  The state lives in registers for
 * the whole run and goes back to rand_s once at the end, for the same
 * reason as rand_rotl above.
 */
static void
rand_fill(p, n)
register unsigned long *p;
register int n;
{
	register unsigned long s0, s1, s2, s3, e;

	s0 = rand_s[0];
	s1 = rand_s[1];
	s2 = rand_s[2];
	s3 = rand_s[3];
	while (n-- > 0) {
		e = s0 - rand_rotl(s1, 27);
		s0 = s1 ^ rand_rotl(s2, 17);
		s1 = s2 + s3;
		s2 = s3 + e;
		s3 = e + s0;
		*p++ = s3;
	}
	rand_s[0] = s0;
	rand_s[1] = s1;
	rand_s[2] = s2;
	rand_s[3] = s3;
}

int
randopen(devp, mode, type, cr)
dev_t *devp;
int mode;
int type;
struct cred *cr;
{
	switch (getminor(*devp)) {
	case RANDOM_MINOR:
	case URANDOM_MINOR:
		rand_seed();
		return 0;
	default:
		return ENXIO;
	}
}

int
randclose(dev, mode, type, cr)
dev_t dev;
int mode;
int type;
struct cred *cr;
{
	return (0);
}

int
randread(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
	unsigned long buf[RAND_BUFSIZE / 4];
	int n, error;

	if (getminor(dev) != RANDOM_MINOR && getminor(dev) != URANDOM_MINOR)
		return ENXIO;

	rand_seed();
	rand_stir();
	while (uiop->uio_resid) {
		n = uiop->uio_resid;
		if (n > RAND_BUFSIZE)
			n = RAND_BUFSIZE;

		rand_fill(buf, (n + 3) / 4);
		error = uiomove((caddr_t)buf, n, UIO_READ, uiop);
		if (error)
			return (error);
	}

	return (0);
}

int
randwrite(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
	unsigned char buf[RAND_BUFSIZE];
	int i, n, error;
	unsigned long v;

	if (getminor(dev) != RANDOM_MINOR && getminor(dev) != URANDOM_MINOR)
		return ENXIO;

	rand_seed();
	while (uiop->uio_resid) {
		n = uiop->uio_resid;
		if (n > RAND_BUFSIZE)
			n = RAND_BUFSIZE;

		error = uiomove((caddr_t)buf, n, UIO_WRITE, uiop);
		if (error)
			return error;

		v = 0;
		for (i = 0; i < n; i++) {
			v = (v << 8) ^ buf[i] ^ (unsigned long)i;
			if ((i & 3) == 3) {
				rand_mix(v);
				v = 0;
			}
		}
		if (n & 3)
			rand_mix(v);
		rand_stir();
	}

	return 0;
}
