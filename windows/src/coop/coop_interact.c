// coop_interact: "Interact everywhere" (docs/coop-interact.md), an experimental opt-in for Link Play outside Palette Town. MIT, our own code.
// The game keeps a session's permissions in a 36-bit mask (GameMgr+0x8c; its top bits are the online / host / visitor bits at +0x90) and rebuilds it
// whenever a session starts or the town changes. In a story area (stage type 1: Withered Wasteland and the other story towns) an online player always
// gets the Spectator pattern, whatever the host chose; Palette Town with Spectator Mode Off gets the normal online pattern. One site per supported main
// build swaps the first for the second while the session's own Spectator flag is clear. Each port reaches the site its own way (nxnative: an
// observation gate; Windows: nxrt interprets the function) and calls coop_interact_site with the registers there. Every player must have it on:
// coop_mods_add puts it in the mods fingerprint, so a session refuses a player whose setting differs.
#include "coop.h"
#include <stdlib.h>
#include <string.h>

// Pokopia 2.0.1 main (76a489f3…): the mask builder's story-area online block. At the site (movk x13, #1, lsl #32) x13 holds the pattern's bits 0-31
// (Spectator: bits 23-26 and 31) and the movk adds bit 32 (online); x22 is the session's Spectator flag, 0 in every story area. The normal online
// pattern sets bits 1-28 and 30
static const coop_site g_sites[] = {
    {"76a489f3bd3642fa5e18bd83d32c98ba", 0xb0f60, 0xb1030, 0xf2c0002du, COOP_SITE_MASK, "permission mask: story areas get Palette Town's Spectator-Off pattern"},
};

int coop_interact_sites(const char *main_build, const coop_site **out) {
    int n = 0;
    *out = NULL;
    if (!main_build) return 0;
    for (size_t i = 0; i < sizeof g_sites / sizeof g_sites[0]; i++)
        if (!strncmp(main_build, g_sites[i].build, strlen(g_sites[i].build))) { if (!n) *out = &g_sites[i]; n++; }
    return n;
}

int coop_interact_on(void) {
    const char *e = getenv("DM_COOP_INTERACT");
    return e && !strcmp(e, "1");
}

void coop_interact_site(int id, uint64_t *x) {
    if (id == COOP_SITE_MASK && !(x[22] & 1)) x[13] = 0x5ffffffe;   // the movk that follows adds bit 32
}

uint64_t coop_mods_add(uint64_t h, const char *name) {
    if (!h) h = 0xcbf29ce484222325ull;   // FNV-1a, as nxnative's cheat fingerprint (ldn_glue.c): each name, then 0xFF
    for (const char *p = name; *p; p++) { h ^= (uint8_t)*p; h *= 0x100000001b3ull; }
    h ^= 0xFF; h *= 0x100000001b3ull;
    return h;
}
