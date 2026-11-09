# Wide view (experimental): Pokopia on screens wider than 16:9

Off by default. On a 21:9 (or wider) window or monitor, the game's 3D view fills the whole width instead of being shown with black bars, and the game's 2D
(HUD, menus, text, the map) stays in the middle 16:9. Game 2.0.1 and 1.0.5. Checked on Windows; Linux shares the Vulkan renderer but is not checked yet.

**Turning it on:**
- The launcher's Display tab: "Wide view: On (experimental)".
- Or in the game: F1, Graphics, "Wide view". Both save `"wide_view": true` in display.json. It takes effect at the next start.
- Or `NXRT_VK_WIDE=1` (tests, and from outside the launcher).

**Resolution:**
- The picture is stretched across the window's width.
- For a sharp picture, draw at the window's width: a 3440x1440 screen wants 3440x1935 (`NXRT_VK_RESOLUTION=3440x1935`), or 3840x2160 from the list.
- The Default resolution counts by height (2560x1440 on a 3440x1440 monitor), so it is a little soft horizontally. A Resolution list for wide screens is the next step.

## How it works

1. **Camera (Hor+).**
   - Every camera of the game builds its projection in one function: 2.0.1 `main+0x1f12114`, 1.0.5 `main+0x1e225d4`. It reads the camera's aspect from `+0x14c`.
   - With the wide view on, that function runs in the interpreter with a site hook at its entry, where x0 is the camera (`host/rt.c`, beside Interact everywhere's hook).
   - A camera whose aspect is 16:9 gets the window's aspect, scaled by the stretch (2.3889 at 3440x1440). Other aspects (square, portrait) are left alone.
   - The words at the entry and at its `ldr s8, [x19, #0x14c]` are checked first. In any other build the hook stays off and the log says so.
   - Nothing else of the game changes: its pixel space stays 1920x1080, so every screen-space effect (lighting, shadows, depth reads) stays consistent.
2. **Present.** A window wider than the picture is filled (`vk_present.c`), so the narrower picture the wide camera made is stretched back to the right proportions.
3. **The 2D is squeezed into the middle 16:9** (`vk_replay.c wide_squeeze`).
   - Draws into a texture the game presents are its 2D, drawn after its 3D composite.
   - Their viewport and scissor are squeezed toward the middle by f = 16:9 / the window's aspect, which the stretch undoes.
   - Full-screen draws without vertex inputs keep the whole width: the 3D composite, fades.
4. **Bars for full-screen menus.**
   - Some menus (the camera menu, the album) cover the whole screen and draw no 3D behind them. Their frame has no full-width draw, so the sides of the texture still hold older pictures.
   - The game draws its composite then only into a corner, for the blurred background.
   - Such a frame is shown with black bars at the sides (`vkr_wide_bars`, `VKP_BARS`).
5. **Photos** (photo mode, the album).
   - The game keeps a photo by copying its whole 16:9 picture (`CopyTextureToBuffer`). That picture holds the wider view squeezed into 16:9.
   - A copy of a whole 16:9 picture of a render target drawn larger, out to the CPU or into a texture at the game's size, takes the picture's middle 16:9 part, stretched (`wide_unsqueeze`).
   - The result is what a 16:9 camera would have seen: the area inside the viewfinder, at the right proportions.
   - Copies between targets drawn larger (history, refraction) keep the whole view, as do copies of part of a picture.

## Limits (option B)

- **The HUD and menus stay in the middle 16:9.** They are not anchored to the screen's edges.
- **World-anchored 2D drifts toward the middle.** Speech bubbles, "i" and "This way!" markers over objects near the sides are placed by the game for 16:9, and are cut at the edge of the middle 16:9.
- **Movies are not checked.** MV01 and MV02 play later in the story (not reached from the new-game opening). If they are drawn as a 2D quad they are pillarboxed like the menus.
- **The Resolution list and the launcher are unchanged:** 16:9 sizes, Default by height. The F1 note "the picture is stretched to fill your window" stays true.
- **32:9** works the same way (f = 0.5), with the HUD in the middle half. Not checked.
- **Android** (about 20:9) shares the renderer but has no option for it. **Mac / iPhone** (Metal) are not covered.

**Planned follow-up (option C):** widen the game's own UI to fill 21:9 properly, so the HUD sits at the screen's edges and markers stay over their objects.
- The game lays out its 2D in a 1920x1080 space (its 1920 / 1080 constants, about 50 places in 2.0.1).
- The work is to find its UI canvas size and give it the window's width, or to tell world-anchored 2D from the HUD by shader, vertex data or constants.
- 3–5+ days, uncertain; not started.

## Tests and checks

- **`nxrt.vk_wide`** (run with `NXRT_VK_SCALE=2`): a 16:9 target drawn at 256x144 whose middle half is green and its sides red.
  - With the squeeze at 0.5, a copy of the whole picture to a buffer and one into a texture at the game's size are all green (the middle part, stretched).
  - A copy of part of it, or with the squeeze off, is unchanged.
  - A presented texture with no full-screen draw gets bars.
- **`scripts/test_play.ps1`:** display.json's `wide_view` becomes `NXRT_VK_WIDE=1` with `-Ask`.
- **In the game** (3440x1440 window, drawn at 3440x1935, RTX 4070 Ti, 60 fps), 2026-11-07:
  - 2.0.1: the village, the bag, the menu and map, photo mode and the album; an interior (the cave of the wasteland); a story cutscene (Tangrowth's overview of the wasteland); the new-game opening and character creation.
  - 1.0.5: the field, the menu, the camera menu (bars), photo mode and the album photo.
