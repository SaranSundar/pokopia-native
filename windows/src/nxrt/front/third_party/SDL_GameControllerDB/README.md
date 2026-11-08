# SDL_GameControllerDB (controller mappings for SDL)

`gamecontrollerdb.txt` and `LICENSE` are copied unchanged from https://github.com/mdqinc/SDL_GameControllerDB at commit
`07af1d50187e94c861c9f9f5d8a55b148f11306b` (2026-11-02), fetched 2026-11-07 with the user's approval:

| file | bytes | SHA-256 |
|---|---|---|
| gamecontrollerdb.txt | 610561 | 4ef169fcee14ef9563e36ebf731dc6950494e553bf8b56b5fc8aa70181f0d25c |
| LICENSE | 884 | 97f35b302b361680ec1e891e95d2d52097bb95abff361434916d99dc1305f127 |

zlib licence (`LICENSE`). The build copies `gamecontrollerdb.txt` beside nxrt.exe, where the front end reads it at start
(`front_sdl.c` mappings_load; a `gamecontrollerdb.txt` in the settings folder is read after it). `.gitattributes` keeps both files byte for byte.
