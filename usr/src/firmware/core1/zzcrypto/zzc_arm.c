/*
 * zzc_arm.c -- core 1's side of zzcrypto (zzcrypto.h): the 060's public-key
 * jobs, done by OpenSSL 3.5's libcrypto built for the A9 (ARMv4/NEON
 * assembler: Montgomery multiplication, P-256).
 *
 * hb_service() looks at the slots; the harness calls it while it waits
 * (arm_main.c's idle loop), the GL server between commands (gpu.c), the
 * player between frames (player.c).  A job runs to its end there: from a
 * few hundred microseconds (X25519) to tens of milliseconds (RSA-4096).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "bench.h"
#include "zzcrypto.h"

/* libcrypto's own (include/crypto/ecx.h, not installed) */
int ossl_x25519(uint8_t out_shared_key[32], const uint8_t private_key[32],
                const uint8_t peer_public_value[32]);
void ossl_x25519_public_from_private(uint8_t out_public_value[32], const uint8_t private_key[32]);
int ossl_ed25519_public_from_private(void *ctx, uint8_t out_public_key[32],
                                     const uint8_t private_key[32], const char *propq);
int ossl_ed25519_sign(uint8_t *out_sig, const uint8_t *tbs, size_t tbs_len,
                      const uint8_t public_key[32], const uint8_t private_key[32],
                      const uint8_t dom2flag, const uint8_t phflag, const uint8_t csflag,
                      const uint8_t *context, size_t context_len, void *libctx, const char *propq);
int ossl_ed25519_verify(const uint8_t *tbs, size_t tbs_len, const uint8_t signature[64],
                        const uint8_t public_key[32], const uint8_t dom2flag, const uint8_t phflag,
                        const uint8_t csflag, const uint8_t *context, size_t context_len,
                        void *libctx, const char *propq);

extern volatile uint8_t *pl_board;
extern uint64_t pl_ticks(void);
extern uint64_t pl_tick_hz;

static volatile uint32_t *zblk;
static uint8_t job[ZC_SLOT_SIZE];
static uint32_t jobs, errors, beat;

static uint32_t be32(uint32_t v) { return __builtin_bswap32(v); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/*
 * Random numbers: no entropy on core 1.  EC_MUL brings 32 bytes from the
 * 060's /dev/urandom; libcrypto's (blinding) draws come from SHA-256 over
 * them and a counter.  Installed as the legacy RAND_METHOD, which
 * RAND_bytes_ex and RAND_priv_bytes_ex still honour.
 */
static uint8_t rseed[32];
static uint32_t rctr;

static int rbytes(unsigned char *buf, int num)
{
    uint8_t in[36], out[32];

    while (num > 0) {
        int n = num < 32 ? num : 32;

        memcpy(in, rseed, 32);
        wr32(in + 32, rctr++);
        SHA256(in, sizeof(in), out);
        memcpy(buf, out, (size_t)n);
        buf += n;
        num -= n;
    }
    return 1;
}
static int rstatus(void) { return 1; }
static int rseed_add(const void *b, int n, double e) { (void)b; (void)n; (void)e; return 1; }
static const RAND_METHOD rmeth = { NULL, rbytes, NULL, rseed_add, rbytes, rstatus };

/* curves by name, built once */
static struct { int nid; EC_GROUP *g; } groups[8];

static EC_GROUP *group(int nid)
{
    int i;

    for (i = 0; i < 8 && groups[i].g; i++)
        if (groups[i].nid == nid)
            return groups[i].g;
    if (i == 8)
        return NULL;
    groups[i].g = EC_GROUP_new_by_curve_name(nid);
    groups[i].nid = nid;
    return groups[i].g;
}

static BN_CTX *bctx;

/* the jobs: the input in job[0..len), the output over it; *out = its bytes.
   Returns the result word, or -1: refused */
static int ec_mul(uint32_t len, uint32_t *out)
{
    const uint32_t nid = rd32(job), flen = rd32(job + 4), slen = rd32(job + 8),
                   num = rd32(job + 12), has_g = rd32(job + 16);
    const uint8_t *p = job + 52;
    EC_GROUP *g;
    EC_POINT *r = NULL, *pts[4] = { NULL, NULL, NULL, NULL };
    BIGNUM *gs = NULL, *ss[4] = { NULL, NULL, NULL, NULL }, *x, *y;
    int ret = -1;
    uint32_t i;

    if (len < 52 || flen == 0 || flen > 80 || slen == 0 || slen > 96 || num > 4 || (!num && !has_g) ||
        len != 52 + (has_g ? slen : 0) + num * (2 * flen + slen) || !(g = group((int)nid)))
        return -1;
    memcpy(rseed, job + 20, 32);
    rctr = 0;
    BN_CTX_start(bctx);
    x = BN_CTX_get(bctx);
    y = BN_CTX_get(bctx);
    if (!y || !(r = EC_POINT_new(g)))
        goto done;
    if (has_g) {
        gs = BN_bin2bn(p, (int)slen, NULL);
        p += slen;
    }
    for (i = 0; i < num; i++) {
        pts[i] = EC_POINT_new(g);
        if (!pts[i] || !BN_bin2bn(p, (int)flen, x) || !BN_bin2bn(p + flen, (int)flen, y) ||
            !EC_POINT_set_affine_coordinates(g, pts[i], x, y, bctx))
            goto done;
        ss[i] = BN_bin2bn(p + 2 * flen, (int)slen, NULL);
        p += 2 * flen + slen;
    }
    if (!EC_POINTs_mul(g, r, gs, num, (const EC_POINT **)pts, (const BIGNUM **)ss, bctx)) {
        ret = 0;
        goto done;
    }
    memset(job, 0, 4 + 2 * flen);
    if (EC_POINT_is_at_infinity(g, r))
        wr32(job, 1);
    else if (!EC_POINT_get_affine_coordinates(g, r, x, y, bctx) ||
             BN_bn2binpad(x, job + 4, (int)flen) < 0 || BN_bn2binpad(y, job + 4 + flen, (int)flen) < 0)
        goto done;
    *out = 4 + 2 * flen;
    ret = 1;
done:
    BN_clear_free(gs);
    for (i = 0; i < 4; i++) {
        BN_clear_free(ss[i]);
        EC_POINT_free(pts[i]);
    }
    EC_POINT_free(r);
    BN_CTX_end(bctx);
    return ret;
}

static int mod_exp(uint32_t len, uint32_t *out)
{
    const uint32_t ct = rd32(job), n = rd32(job + 4);
    BIGNUM *a, *e, *m, *r;
    int ret = -1;

    if (n == 0 || n > 1024 || len != 8 + 3 * n)
        return -1;
    BN_CTX_start(bctx);
    a = BN_CTX_get(bctx);
    e = BN_CTX_get(bctx);
    m = BN_CTX_get(bctx);
    r = BN_CTX_get(bctx);
    if (r && BN_bin2bn(job + 8, (int)n, a) && BN_bin2bn(job + 8 + n, (int)n, e) &&
        BN_bin2bn(job + 8 + 2 * n, (int)n, m) && BN_is_odd(m)) {
        if (ct)
            BN_set_flags(e, BN_FLG_CONSTTIME);
        ret = ct ? BN_mod_exp_mont_consttime(r, a, e, m, bctx, NULL) : BN_mod_exp_mont(r, a, e, m, bctx, NULL);
        if (ret == 1 && BN_bn2binpad(r, job, (int)n) < 0)
            ret = -1;
        *out = n;
    }
    BN_clear(a);
    BN_clear(e);
    BN_CTX_end(bctx);
    return ret;
}

static int ed25519(uint32_t op, uint32_t len, uint32_t *out)
{
    const uint32_t fl = rd32(job), cl = rd32(job + 4), tl = rd32(job + 8);
    uint8_t sig[64];
    int ret;

    if (op == ZC_OP_ED25519_SIGN) {
        const uint8_t *pub = job + 12, *priv = job + 44, *ctx = job + 76;

        if (cl > 255 || len != 76 + cl + tl)
            return -1;
        ret = ossl_ed25519_sign(sig, ctx + cl, tl, pub, priv, fl & 1, fl >> 1 & 1, fl >> 2 & 1,
                                ctx, cl, NULL, NULL);
        memcpy(job, sig, 64);
        *out = 64;
        return ret;
    }
    {
        const uint8_t *s = job + 12, *pub = job + 76, *ctx = job + 108;

        if (cl > 255 || len != 108 + cl + tl)
            return -1;
        *out = 0;
        return ossl_ed25519_verify(ctx + cl, tl, s, pub, fl & 1, fl >> 1 & 1, fl >> 2 & 1, ctx, cl,
                                   NULL, NULL);
    }
}

static int run(uint32_t op, uint32_t len, uint32_t *out)
{
    uint8_t k[32];

    switch (op) {
    case ZC_OP_X25519:
        if (len != 64)
            return -1;
        memcpy(k, job, 32);
        *out = 32;
        return ossl_x25519(job, k, job + 32);
    case ZC_OP_X25519_PUB:
        if (len != 32)
            return -1;
        memcpy(k, job, 32);
        ossl_x25519_public_from_private(job, k);
        *out = 32;
        return 1;
    case ZC_OP_ED25519_PUB:
        if (len != 32)
            return -1;
        memcpy(k, job, 32);
        *out = 32;
        return ossl_ed25519_public_from_private(NULL, job, k, NULL);
    case ZC_OP_ED25519_SIGN:
    case ZC_OP_ED25519_VERIFY:
        return ed25519(op, len, out);
    case ZC_OP_EC_MUL:
        return ec_mul(len, out);
    case ZC_OP_MOD_EXP:
        return mod_exp(len, out);
    }
    return -1;
}

static void start(void)
{
    zblk = (volatile uint32_t *)(pl_board + ZC_BLK_OFF);
    memset((void *)zblk, 0, 4096);
    RAND_set_rand_method(&rmeth);
    bctx = BN_CTX_new();
    zblk[ZC_W_VERSION] = be32(ZC_VERSION);
    zblk[ZC_W_OPS] = be32(((1u << ZC_OPS) - 1) & ~1u);
}

void hb_service(void)
{
    int i;

    if (!zblk)
        start();
    zblk[ZC_W_MAGIC] = be32(ZC_MAGIC);
    zblk[ZC_W_BEAT] = be32(++beat);
    for (i = 0; i < ZC_SLOTS; i++) {
        volatile uint32_t *s = zblk + ZC_W_SLOT + i * ZC_S_WORDS;
        volatile uint8_t *d = pl_board + ZC_DATA_OFF + i * ZC_SLOT_SIZE;
        uint32_t seq, op, len, out = 0;
        uint64_t t0;
        int ret;

        if (be32(s[ZC_S_STATE]) != ZC_POSTED)
            continue;
        seq = be32(s[ZC_S_SEQ]);
        op = be32(s[ZC_S_OP]);
        len = be32(s[ZC_S_LEN]);
        s[ZC_S_STATE] = be32(ZC_BUSY);
        t0 = pl_ticks();
        ret = -1;
        if (len <= ZC_SLOT_SIZE) {
            memcpy(job, (const void *)d, len);
            ret = run(op, len, &out);
        }
        if (be32(s[ZC_S_SEQ]) != seq) { /* the 060 gave up on it (0): free for the next */
            memset(job, 0, len < ZC_SLOT_SIZE ? len : ZC_SLOT_SIZE);
            if (s[ZC_S_SEQ] == 0)
                s[ZC_S_STATE] = be32(ZC_FREE);
            continue;
        }
        if (ret < 0) {
            s[ZC_S_STATE] = be32(ZC_REFUSED);
            zblk[ZC_W_ERRORS] = be32(++errors);
            continue;
        }
        memcpy((void *)d, job, out);
        memset(job, 0, len > out ? len : out);  /* keys */
        s[ZC_S_LEN] = be32(out);
        s[ZC_S_RESULT] = be32((uint32_t)ret);
        s[ZC_S_US] = be32((uint32_t)((pl_ticks() - t0) * 1000000ULL / pl_tick_hz));
        __asm__ volatile("dmb" ::: "memory");
        s[ZC_S_STATE] = be32(ZC_DONE);
        zblk[ZC_W_JOBS] = be32(++jobs);
    }
}

/*
 * What libcrypto names and newlib on bare metal has not: never called for
 * these jobs (no directories, files or users on core 1)
 */
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>

int posix_memalign(void **p, size_t align, size_t n);
int posix_memalign(void **p, size_t align, size_t n)
{
    *p = memalign(align, n);
    return *p ? 0 : ENOMEM;
}

void *opendir(const char *name);
void *opendir(const char *name) { (void)name; errno = ENOENT; return NULL; }
void *readdir(void *d);
void *readdir(void *d) { (void)d; return NULL; }
int closedir(void *d);
int closedir(void *d) { (void)d; return 0; }
gid_t getgid(void);
gid_t getgid(void) { return 0; }
gid_t getegid(void);
gid_t getegid(void) { return 0; }
int _stat(const char *name, struct stat *st);
int _stat(const char *name, struct stat *st) { (void)name; (void)st; errno = ENOENT; return -1; }
