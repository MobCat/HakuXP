/*
 * HakuXP native core: the DVD drive's security sector, built and signed
 * here (xbox_ss.h).
 */
#include "native/kernel/xbox_ss.h"
#include "native/kernel/xc_crypto.h"
#include "native/kernel/xc_emukey.h"

#include <string.h>

#define W NC_EMUKEY_WORDS        /* 66: the key blob's words */
#define H NC_EMUKEY_HALF_WORDS   /* 33: p's and q's */
#define KEY_BYTES (NC_EMUKEY_BITS / 8)

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* r (an + bn words) = a * b */
static void bn_mul(uint32_t *r, const uint32_t *a, unsigned an, const uint32_t *b, unsigned bn)
{
    memset(r, 0, (an + bn) * sizeof(uint32_t));
    for (unsigned i = 0; i < an; i++) {
        uint64_t carry = 0;
        for (unsigned j = 0; j < bn; j++) {
            const uint64_t t = (uint64_t)a[i] * b[j] + r[i + j] + carry;
            r[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        r[i + bn] = (uint32_t)carry;
    }
}

/* s = m^d mod n, with the CRT values: two 1024-bit exponentiations instead of
 * one 2048-bit one (nc_modexp reduces bit by bit, so this is ~4x faster). */
static void rsa_private(uint32_t s[W], const uint32_t m[W])
{
    uint32_t mp[H], mq[H], m1[H], m2[H], m2p[H], diff[H], h[H], t[2 * H];
    nc_bn_mod(mp, m, W, nc_emukey_p, H);
    nc_bn_mod(mq, m, W, nc_emukey_q, H);
    nc_modexp(m1, mp, nc_emukey_dp, nc_emukey_p, H);
    nc_modexp(m2, mq, nc_emukey_dq, nc_emukey_q, H);
    /* diff = (m1 - m2) mod p */
    nc_bn_mod(m2p, m2, H, nc_emukey_p, H);
    int64_t borrow = 0;
    for (unsigned i = 0; i < H; i++) {
        const int64_t v = (int64_t)m1[i] - m2p[i] - borrow;
        diff[i] = (uint32_t)v;
        borrow = v < 0;
    }
    if (borrow) {  /* m1 < m2p: add p back */
        uint64_t carry = 0;
        for (unsigned i = 0; i < H; i++) {
            const uint64_t v = (uint64_t)diff[i] + nc_emukey_p[i] + carry;
            diff[i] = (uint32_t)v;
            carry = v >> 32;
        }
    }
    /* h = qinv * diff mod p; s = m2 + h * q */
    bn_mul(t, nc_emukey_qinv, H, diff, H);
    nc_bn_mod(h, t, 2 * H, nc_emukey_p, H);
    bn_mul(t, h, H, nc_emukey_q, H);
    uint64_t carry = 0;
    for (unsigned i = 0; i < W; i++) {
        const uint64_t v = (uint64_t)(i < 2 * H ? t[i] : 0) + (i < H ? m2[i] : 0) + carry;
        s[i] = (uint32_t)v;
        carry = v >> 32;
    }
}

/* A PKCS #1 v1.5 type 1 block for a SHA-1 digest (00 01 FF.. 00 DigestInfo
 * digest), signed: XcVerifyPKCS1Signature undoes it with the public key and
 * finds the digest at its end (native/kernel/xc.c). */
static void sign_sha1(uint8_t sig[KEY_BYTES], const uint8_t digest[20])
{
    static const uint8_t k_sha1_info[15] = {
        0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14,
    };
    uint8_t be[KEY_BYTES];
    memset(be, 0xFF, sizeof(be));
    be[0] = 0x00;
    be[1] = 0x01;
    be[KEY_BYTES - 36] = 0x00;
    memcpy(be + KEY_BYTES - 35, k_sha1_info, sizeof(k_sha1_info));
    memcpy(be + KEY_BYTES - 20, digest, 20);
    uint32_t m[W] = {0}, s[W];
    for (unsigned i = 0; i < KEY_BYTES; i++) {  /* the Xbox's bignums are little-endian */
        m[i / 4] |= (uint32_t)be[KEY_BYTES - 1 - i] << (8 * (i % 4));
    }
    rsa_private(s, m);
    for (unsigned i = 0; i < KEY_BYTES; i++) {
        sig[i] = (uint8_t)(s[i / 4] >> (8 * (i % 4)));
    }
}

void nc_ss_build(uint8_t ss[NC_SS_LEN], uint32_t time)
{
    memset(ss, 0, NC_SS_LEN);
    ss[768] = 1;   /* challenge table version, as on a disc */
    ss[769] = 23;  /* its entry count; the entries stay zero (no drive to answer them) */
    wr32(ss + NC_SS_CERT_TIME, time);
    uint8_t len[4];
    wr32(len, NC_SS_HASHED);
    NcSha1 sha;
    nc_sha1_init(&sha);
    nc_sha1_update(&sha, len, sizeof(len));
    nc_sha1_update(&sha, ss, NC_SS_HASHED);
    nc_sha1_final(&sha, ss + NC_SS_HASH_A);
    sign_sha1(ss + NC_SS_SIG_A, ss + NC_SS_HASH_A);
}

void nc_emukey_blob(uint8_t blob[NC_EMUKEY_BLOB_LEN])
{
    memset(blob, 0, NC_EMUKEY_BLOB_LEN);
    wr32(blob + 0x00, 0x31415352u);          /* "RSA1" */
    wr32(blob + 0x04, W * 4);                /* modulus bytes, padding included */
    wr32(blob + 0x08, NC_EMUKEY_BITS);
    wr32(blob + 0x0C, KEY_BYTES - 1);        /* the largest message */
    wr32(blob + 0x10, NC_EMUKEY_EXPONENT);
    for (unsigned i = 0; i < W; i++) {
        wr32(blob + 0x14 + 4 * i, nc_emukey_modulus[i]);
    }
}
