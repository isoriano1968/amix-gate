/*
 * zzcrypto.h -- public-key crypto for the 060 (AMIX's libcrypto) on the
 * Z3660's core 1 (zzc_arm.c, OpenSSL 3.5's libcrypto for the A9).
 *
 * A process takes one of ZC_SLOTS slots (a SysV semaphore each, on the 060),
 * writes a job into the slot's data, and sets the slot's state to
 * ZC_POSTED; core 1 copies the job out, computes, writes the answer over it
 * and sets ZC_DONE.  The jobs are what costs seconds on the 060 and
 * milliseconds on the A9: X25519, Ed25519, prime-curve point multiplication
 * (ECDH, ECDSA), modular exponentiation (RSA, DH).  Bulk ciphers stay on
 * the 060.
 *
 * The block and the data lie in the MPEG FIFO's megabyte (0x07A00000,
 * uncached on core 0 and on core 1, see bench.h), above the FIFO's 64 KB.
 * Everything is big-endian: 32-bit words, numbers as big-endian bytes.
 */
#ifndef ZZCRYPTO_H
#define ZZCRYPTO_H

#define ZC_BLK_OFF          0x07AFB000UL    /* the block: one page */
#define ZC_DATA_OFF         0x07A80000UL    /* the slots' data */
#define ZC_SLOTS            8
#define ZC_SLOT_SIZE        0x8000UL        /* 32 KB each, to 0x07AC0000 */

#define ZC_MAGIC            0x5A5A4352UL    /* "ZZCR": core 1 takes jobs */
#define ZC_VERSION          1

/* words of the block */
enum {
    ZC_W_MAGIC,         /* core 1: ZC_MAGIC, written on every pass; the 060 clears
                           it when a job got no answer (core 1 hung or replaced) */
    ZC_W_VERSION,       /* core 1 */
    ZC_W_OPS,           /* core 1: 1 << op for each op it does */
    ZC_W_JOBS,          /* core 1: jobs done */
    ZC_W_BEAT,          /* core 1: passes over the slots */
    ZC_W_ERRORS,        /* core 1: jobs refused (bad input) */
    ZC_W_SLOT = 16      /* ZC_SLOTS slots of ZC_S_WORDS words */
};

/* words of a slot */
enum {
    ZC_S_STATE,         /* ZC_FREE ... */
    ZC_S_SEQ,           /* 060: the job's number; core 1 answers only the same number */
    ZC_S_OP,            /* 060: ZC_OP_* */
    ZC_S_LEN,           /* 060: input bytes; core 1: output bytes */
    ZC_S_RESULT,        /* core 1: the job's return value (as the 060's function) */
    ZC_S_PID,           /* 060: who posted it (for zzglstat/ztop) */
    ZC_S_US,            /* core 1: microseconds it took */
    ZC_S_WORDS = 8
};
enum { ZC_FREE, ZC_POSTED, ZC_BUSY, ZC_DONE, ZC_REFUSED };

/*
 * The jobs.  In: what the slot's data holds when posted; out: when done.
 * "n" below is a 32-bit word, "b[k]" k bytes.
 */
enum {
    ZC_OP_X25519 = 1,       /* in: priv b[32], peer b[32]  out: shared b[32]; result 0 for a
                               low-order peer (ossl_x25519) */
    ZC_OP_X25519_PUB,       /* in: priv b[32]  out: pub b[32] */
    ZC_OP_ED25519_PUB,      /* in: priv b[32]  out: pub b[32] */
    ZC_OP_ED25519_SIGN,     /* in: n flags (dom2 | ph << 1 | cs << 2), n ctxlen, n tbslen, pub b[32],
                               priv b[32], ctx b[ctxlen], tbs b[tbslen]  out: sig b[64] */
    ZC_OP_ED25519_VERIFY,   /* in: n flags, n ctxlen, n tbslen, sig b[64], pub b[32], ctx, tbs
                               out: nothing; result 1 = valid */
    ZC_OP_EC_MUL,           /* in: n nid, n flen, n slen, n num, n has_g, b[32] random (core 1's
                               coordinate blinding), [g scalar b[slen]], num x (x b[flen], y b[flen],
                               scalar b[slen])  out: n infinity, x b[flen], y b[flen] (affine) */
    ZC_OP_MOD_EXP,          /* in: n consttime, n len (bytes of m), a b[len], p b[len], m b[len]
                               out: r b[len] */
    ZC_OPS
};

#endif
