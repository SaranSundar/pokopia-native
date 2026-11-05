/* FIPS 180-4's SHA-256 example vectors ("abc", the 448-bit message, one million 'a'), fed whole and in odd pieces */
#include "nxw/sha256.h"

#include <stdio.h>
#include <string.h>

static int check(const char *name, const void *msg, size_t n, size_t piece, const char *want) {
    nxw_sha256 c;
    uint8_t md[32];
    char hex[65];
    nxw_sha256_init(&c);
    for (size_t p = 0; p < n; p += piece) nxw_sha256_update(&c, (const uint8_t *)msg + p, n - p < piece ? n - p : piece);
    nxw_sha256_final(&c, md);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", md[i]);
    if (strcmp(hex, want)) { printf("FAIL %s (pieces of %zu): %s\n", name, piece, hex); return 1; }
    return 0;
}

int main(void) {
    static char million[1000000];
    memset(million, 'a', sizeof million);
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    int bad = 0;
    for (size_t piece = 1; piece <= 129; piece += 7) {
        bad += check("abc", "abc", 3, piece, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        bad += check("448 bits", m2, strlen(m2), piece, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    bad += check("empty", "", 0, 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    bad += check("million a", million, sizeof million, 4096, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    bad += check("million a", million, sizeof million, 997, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    printf(bad ? "%d failures\n" : "sha256: all vectors pass\n", bad);
    return bad != 0;
}
