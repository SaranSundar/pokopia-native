#include "nxload/module.hpp"

#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>   // the Mac build (the swap-in harness, docs/m4-swapin.md)
#else
#include "nxw/sha256.h"                  // Linux (docs/LINUX-PLAN.md)
#endif

namespace nxload {

#ifdef _WIN32
std::array<uint8_t, 32> sha256(std::span<const uint8_t> data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::array<uint8_t, 32> out{};
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0));
    for (size_t p = 0; ok && p < data.size();) {
        const ULONG n = ULONG(std::min<size_t>(data.size() - p, 1u << 30));
        ok = BCRYPT_SUCCESS(BCryptHashData(h, const_cast<PUCHAR>(data.data() + p), n, 0));
        p += n;
    }
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(h, out.data(), ULONG(out.size()), 0));
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) fail("SHA-256: BCrypt failed");
    return out;
}
#elif !defined(__APPLE__)
std::array<uint8_t, 32> sha256(std::span<const uint8_t> data) {
    std::array<uint8_t, 32> out{};
    nxw_sha256 c;
    nxw_sha256_init(&c);
    nxw_sha256_update(&c, data.data(), data.size());
    nxw_sha256_final(&c, out.data());
    return out;
}
#else
std::array<uint8_t, 32> sha256(std::span<const uint8_t> data) {
    std::array<uint8_t, 32> out{};
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    for (size_t p = 0; p < data.size();) {
        const CC_LONG n = CC_LONG(std::min<size_t>(data.size() - p, 1u << 30));
        CC_SHA256_Update(&c, data.data() + p, n);
        p += n;
    }
    CC_SHA256_Final(out.data(), &c);
    return out;
}
#endif

}  // namespace nxload
