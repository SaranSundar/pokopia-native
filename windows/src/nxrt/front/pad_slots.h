// from nxnative dm/host/pad_slots.h (MIT, © seedds). Windows: this note and pad_slot_first (ours) added; the owner is the SDL joystick instance id's address in the front end's table.
// Player slots of the physical controllers (front_sdl.c; pure, so tests/test_input.c checks it without controllers): each controller holds one slot, slot p is player p + 1
// (hle_hid.c dm_player_npad gives its npad). A connecting controller takes, in this order: a free slot whose last holder had its name (the same controller reconnected gets its
// player back; two controllers of one model may trade places), player 1 when it is free, the lowest slot no controller has held yet (so a slot stays kept for a controller
// that went away while others are in use), then the lowest free slot. With every slot held it gets none (-1). A disconnected controller frees its slot and leaves its name on
// it. The owner is any pointer that stays the same while the controller is connected; a holder that never disconnects (the keyboard with DM_KBD_PLAYER=own) takes slot 0
// first and keeps it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dm_input.h"

typedef struct { const void *owner[DM_PLAYERS]; char last[DM_PLAYERS][64]; } pad_slots;

static int pad_slot_of(const pad_slots *s, const void *owner) {
    for (int i = 0; owner && i < DM_PLAYERS; i++) if (s->owner[i] == owner) return i;
    return -1;
}
static int pad_slot_take(pad_slots *s, const void *owner, const char *name) {
    int p = pad_slot_of(s, owner); if (p >= 0 || !owner) return p;
    if (!name) name = "";
    for (int i = 0; p < 0 && *name && i < DM_PLAYERS; i++) if (!s->owner[i] && !strcmp(s->last[i], name)) p = i;
    if (p < 0 && !s->owner[0]) p = 0;
    for (int i = 0; p < 0 && i < DM_PLAYERS; i++) if (!s->owner[i] && !s->last[i][0]) p = i;
    for (int i = 0; p < 0 && i < DM_PLAYERS; i++) if (!s->owner[i]) p = i;
    if (p < 0) return -1;
    s->owner[p] = owner; snprintf(s->last[p], sizeof s->last[p], "%s", name);
    return p;
}
static int pad_slot_free(pad_slots *s, const void *owner) {
    int p = pad_slot_of(s, owner); if (p >= 0) s->owner[p] = NULL;
    return p;
}
static uint32_t pad_slot_mask(const pad_slots *s) {   // bit p: slot p is held
    uint32_t m = 0; for (int i = 0; i < DM_PLAYERS; i++) if (s->owner[i]) m |= 1u << i;
    return m;
}
// Our own (MIT): a holder made player 1 (the in-game menu's choice, or the controller remembered for it). Whoever had slot 0 takes the holder's old slot (the
// two swap, with the names they leave); a holder of slot 0 that never lets go (the keyboard with DM_KBD_PLAYER=own) keeps it. The holder's slot: 0, or -1
// when it holds none or slot 0 stays where it is
static int pad_slot_first(pad_slots *s, const void *owner, const void *keep) {
    int p = pad_slot_of(s, owner); if (p <= 0) return p;
    if (keep && s->owner[0] == keep) return -1;
    const void *o = s->owner[0]; s->owner[0] = owner; s->owner[p] = o;
    char t[64]; memcpy(t, s->last[0], sizeof t); memcpy(s->last[0], s->last[p], sizeof t); memcpy(s->last[p], t, sizeof t);
    return 0;
}
