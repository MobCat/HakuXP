# XAPI's security-sector check (Midtown Madness 3)

Why Midtown Madness 3 (USA, `4D53002A`, `MS04204A`) returns to the
dashboard at boot, and what a fix has to satisfy. Found 2026-11-15 from a
device capture (`summary.bat`) and the title's XBE, read from the owner's
XISO without writing to it, disassembled with MinGW `objdump`. Addresses
are this XBE's (XDK 5233, statically linked XAPI).

## What the title does

`0x86FEE`, called from `0x8721F` early in startup; a negative result makes
the caller leave for the dashboard (HalReturnToFirmware, routine 2).

1. Gate. `[ebp-8]` = certificate `+0x1D8` (security flags; 0 if the
   certificate is shorter) & 1. Media = certificate `+0x9C` (allowed media)
   & 0xFFFFFF, forced to 2 when the flag is set. Media other than 2
   (DVD-X2): return 0, no check at all.
2. Opens `\Device\CdRom0`; MODE SENSE(10) page `0x3E`, 28 bytes, up to 5
   tries: byte 11 (PartitionArea) must be 1 (else `0xC0000014`), bytes 10
   (CDFValid) and 12 (Authentication) nonzero (else `0xC0000001`). The
   emulator's answer passes (`scsi_pass_through`, `native/kernel/io.c`).
3. If the flag is clear: return success. Otherwise, up to 5 tries, READ DVD
   STRUCTURE `ad 00 ff 02 fd ff fe 00 06 64 00 c0` (address `0xFF02FDFF`,
   layer `0xFE`, format 0, 0x664 bytes: a 4-byte header and the 1632 bytes
   of the security sector a stock drive returns). A failed IOCTL returns
   its status (fatal for the caller). Otherwise the buffer goes to the
   checker, `0x86F37`; success ends the loop.

The checker, with offsets into the security sector (the buffer minus its
4-byte header; layout per xboxdevwiki "Xbox Game Disc"):

| Check | Fails with |
| --- | --- |
| u32 at 1191 (the first Unknown1's "XBE certificate timestamp") equals certificate `+0x04` (TimeDate) | `0xC0000156` |
| SHA-1 (`XcSHAInit/Update/Final`) of the u32 `0x4CB` and then bytes 0..1226 equals the 20 bytes at 1227 (hash A) | `0xC0000190` |
| `XcVerifyPKCS1Signature(sector + 1247 (signature A), XePublicKeyData, hash)` | `0xC0000190` |

## Which titles

Only those with security flag bit 0 set. Midtown Madness 3: allowed media
`0x002`, flags `0x3`. RalliSport Challenge 2 (World), which boots: media
`0x202`, flags `0x2`, so step 1 returns at once. The capture line `SCSI ad
answered with zeros x5 (1636 bytes, in, cdb ad 00 ff 02 fd ff fe ...)`
marks an affected title; batch runs can count them.

## What the emulator has today

- MODE SENSE page `0x3E`: answered as an authenticated Xbox partition.
- Every other pass-through command, this READ DVD STRUCTURE included:
  "good" status and a zero-filled buffer, so the checker fails on the
  timestamp.
- `XcSHA*`: implemented. `XcVerifyPKCS1Signature`: a real RSA check
  (`native/kernel/xc.c`). `XePublicKeyData`: an empty data export, so no
  signature verifies against it ("bad public key").
- xemu's ATAPI code (`hw/ide/atapi.c`, master) has no Xbox handling: format
  0 on layer `0xFE` is an error there. Failing the IOCTL doesn't help this
  check (the caller treats it as fatal).

The real security sector isn't in an XISO, and even a real one is signed
with Microsoft's key, which the project doesn't ship.

## Fixes considered

1. **The drive answers with a consistent security sector, signed with an
   emulator key.** On this READ DVD STRUCTURE, build the 0x664 bytes: the
   title's certificate timestamp at 1191, hash A at 1227 as above, and
   signature A at 1247 made with an RSA key pair of the project's own; the
   kernel exports that key's public half as `XePublicKeyData`. The title's
   code runs unchanged and every check is real. No Microsoft data. Other
   signatures checked against `XePublicKeyData` (Microsoft-signed content)
   fail as they do now. Costs: an RSA-2048 private-key operation per read
   (the modexp in `xc_crypto.c`), the key's layout in `XePublicKeyData`,
   the rest of the sector's fields (zeros, or the challenge table format)
   for titles that read more of it.
2. **Clear security flag bit 0 in the loaded certificate.** The title then
   skips the read (and, for media other than 2, the whole check). A few
   lines in the XBE loader, but the title sees a certificate that differs
   from its disc, as with a media patch.

## Chosen: option 1 (2026-11-15)

- `tools/native/gen_emukey.py` generated HakuXP's RSA-2048 key (e 65537)
  into `native/kernel/xc_emukey.h`: modulus, private exponent and the CRT
  values.
- The kernel exports the public half as `XePublicKeyData`, a 284-byte
  "RSA1" blob in the big data-export area (`K_DATA_BIG + 0x100`, past
  `XboxAlternateSignatureKeys`; a 0x40 slot is too small).
- `native/kernel/xbox_ss.c` builds the sector: challenge table version 1
  and count 23 (entries zero), the certificate's TimeDate at 1191, hash A
  at 1227, signature A at 1247 (a PKCS #1 v1.5 type 1 block with SHA-1's
  DigestInfo, signed by CRT). Other fields are zero.
- `scsi_pass_through` (`native/kernel/io.c`) answers this READ DVD
  STRUCTURE with the 4-byte header and the sector for the running XBE's
  certificate (read from the header at 0x10000), built once per
  certificate and logged as `scsi ad: security sector for certificate time
  ..., signed with HakuXP's key in N ms`.
- Host test `tests/hle/test_ss.c` replays XAPI's three checks the way
  `native/kernel/xc.c` verifies, for MM3's TimeDate (`3EC15106`) and
  others, checks that a changed byte fails, and that the CRT signature equals
  m^d mod n. Signing takes about 240 ms at `-O2` on the PC (the bit-serial
  reduction in `xc_crypto.c`); a faster reduction would cut it.

Not done: kernel-level DVD authentication (the challenge table), PFI and
the second hash and signature, which nothing seen so far reads. A title
that does would show it in its capture.
