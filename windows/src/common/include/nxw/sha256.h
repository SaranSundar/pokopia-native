/* SHA-256 (FIPS 180-4) without a system library, for Linux (Windows uses BCrypt, the Mac CommonCrypto). C and C++. */
#ifndef NXW_SHA256_H
#define NXW_SHA256_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nxw_sha256 {
    uint32_t h[8];
    uint64_t len;        /* bytes hashed so far */
    uint8_t buf[64];
    size_t nbuf;
} nxw_sha256;

void nxw_sha256_init(nxw_sha256 *c);
void nxw_sha256_update(nxw_sha256 *c, const void *data, size_t n);
void nxw_sha256_final(nxw_sha256 *c, uint8_t out[32]);

#ifdef __cplusplus
}
#endif
#endif
