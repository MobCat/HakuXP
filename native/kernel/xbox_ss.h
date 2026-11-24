/*
 * HakuXP native core: the DVD drive's security sector, built and signed here.
 *
 * XAPI reads it (READ DVD STRUCTURE format 0, address 0xFF02FDFF, layer 0xFE)
 * in titles whose certificate sets security flag bit 0, and checks the
 * certificate's timestamp in it, a SHA-1 over it and an RSA signature against
 * XePublicKeyData (docs/research/2026-11-15-security-sector-check.md). An
 * XISO has no security sector, and a real one is signed with Microsoft's key,
 * so the emulated drive builds one and signs it with HakuXP's own key
 * (xc_emukey.h), whose public half the kernel exports as XePublicKeyData.
 * Pure: host memory only, tested on the host (tests/hle/test_ss.c).
 */
#ifndef NATIVE_KERNEL_XBOX_SS_H
#define NATIVE_KERNEL_XBOX_SS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The part of the security sector a stock Xbox drive returns. */
#define NC_SS_LEN 1632
/* Offsets XAPI checks (into the sector, past READ DVD STRUCTURE's header). */
#define NC_SS_CERT_TIME 1191   /* u32: the XBE certificate's TimeDate */
#define NC_SS_HASH_A    1227   /* SHA-1 of the u32 0x4CB, then bytes 0..1226 */
#define NC_SS_SIG_A     1247   /* RSA-2048 signature of hash A, little-endian */
#define NC_SS_HASHED    0x4CB

/* The security sector for a title whose certificate has TimeDate time. */
void nc_ss_build(uint8_t ss[NC_SS_LEN], uint32_t time);

/* XePublicKeyData: HakuXP's public key as an "RSA1" blob (magic, key length
 * in bytes, bit length, largest message, public exponent, the modulus). */
#define NC_EMUKEY_BLOB_LEN (0x14 + 66 * 4)
void nc_emukey_blob(uint8_t blob[NC_EMUKEY_BLOB_LEN]);

#ifdef __cplusplus
}
#endif

#endif
