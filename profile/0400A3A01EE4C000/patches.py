# Pokopia's patches for nxnative's rewriter (dm/tools/patch_nso.py reads the patches.py of every profile in DM_GAMES_DIR; the formats are its own).
# OBS: observation gates, which change no instruction: the rewriter moves the instruction into a stub that first calls the host.

# Link Play's "Interact everywhere" (hle_coop_interact.c, the launcher's switch DM_COOP_INTERACT, windows-native docs/coop-interact.md). Gate number 27 is
# OBS_COOP_INTERACT (dm.h, from the co-op patch); the site's id is in its upper bits (COOP_SITE_MASK = 1). The callback does nothing while the switch is off
OBS_COOP_INTERACT, COOP_SITE_MASK = 27, 1
OBS = {
    '76a489f3bd3642fa5e18bd83d32c98ba': [   # 2.0.1 main
        (0xb1030, OBS_COOP_INTERACT | COOP_SITE_MASK << 8, 'f2c0002d', 'Interact everywhere: the permissions a story area gives online players'),
    ],
}
