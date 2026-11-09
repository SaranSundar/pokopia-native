# Anti-aliasing: smoother edges on the 3D picture

On by default (SMAA). Smooths the stair-stepped edges of the 3D world and characters. The HUD, menus and text are drawn after it, so they stay sharp.
Windows (and Linux, which shares the Vulkan renderer); the Mac and iPhone use their own Metal renderer and do not have it.

**The setting:**
- The launcher's Display tab, "Anti-aliasing": Off, FXAA or SMAA.
- Or in the game: F1, Graphics, "Anti-aliasing". It changes at once.
- Both save `"antialiasing": "off" | "fxaa" | "smaa"` in display.json. From outside the launcher (tests): `NXRT_VK_AA=off|fxaa|smaa`.
- SMAA keeps more detail; FXAA smooths a little more and is a little softer.

**Even smoother:** a Resolution above the screen's (F1, Graphics) draws more pixels and averages them down to the window. That smooths everything,
at a much higher GPU cost. The two can be combined.

## What the game does itself

Pokopia's last full-screen pass (shader `20cbfdc0bf490ec8`, the tone-mapped picture into the texture it presents) runs FXAA 3.11's cheap console
variant: five reads, a direction from them, four more reads along it.
- The game switches it on (constant `c5[6].w` = 1.2), with thresholds 0.05 and sharpness 8 (`c5[6]`).
- Our replay runs that shader as the Switch 2 does. Checked on 2026-11-08 by a pass dump and the composite's input and output (`texdump`).
- That FXAA softens edges a little but leaves stair steps along long, shallow edges, on the Switch 2 too.
- The game has no other AA: no multisampled targets, no history (TAA) pass.

## How it works

1. **The points.** `vk_replay.c aa_whole` finds a picture drawn whole: one unblended draw (fades blend) that reads a texture and overwrites the
   first level of its target, all of it or, for a composite, a rectangle from its corner 0,0. Two kinds:
   - **The composite** into a texture the game presents, a full-screen draw without vertex inputs. It is the point Wide view already knows
     (`wide_squeeze`). The pass runs right after it, before the HUD.
     - In handheld mode Pokopia draws its 3D at a lower resolution: 1600x900 in the corner of the 1920x1080 texture, limited by its scissor. It then
       copies that part and stretches it over the whole texture before the HUD.
     - The pass runs on that 1600x900 rectangle, before the stretch. Its images stay at the texture's full size, so a changing resolution makes
       nothing new, and every read stays inside the rectangle.
   - **A picture of its own:** a menu's 3D drawn apart into an 8-bit target and blended in by the menu's 2D. The character editor draws the
     character at 720x720 (HDR, then a premultiplied sRGB picture). It is kept until the next present. The pass runs once, just before a draw into a
     presented texture reads it; another draw into it first drops it.
2. **The pass.** `vk_aa.c` copies the picture and runs over it:
   - **SMAA** in three passes (`shaders/aa_edge.frag`, `aa_weight.frag`, `aa_blend.frag`):
     - luma edges with local contrast adaptation;
     - for each edge, its two ends searched along it (up to 32 pixels), the crossing edges there giving its shape (L, Z, U), and the area on each side
       of the line through the shape. SMAA reads those areas from a precomputed texture; here they are computed in the shader by the same rules (its
       orthogonal patterns; no diagonal or corner patterns);
     - each pixel mixed with its neighbours by those areas.
   - **FXAA** 3.11 quality in one pass (`shaders/aa_fxaa.frag`): the edge's direction from second differences, both ends searched (12 steps), the pixel
     moved across the edge by how near it is to the nearer end, or by a sub-pixel blend.
3. **Colour.** Edges and luma are found on the stored values (gamma-encoded, as perceived). The mixing reads the picture in its own format, which is
   linear light for the game's sRGB target. Mixing in gamma made edges darker than their true coverage.
4. **Untouched pixels.** Pixels on no edge are not written, so everything away from the edges stays exactly as the game drew it.
5. **Resolution.** At a higher Resolution (F-5) it runs at the picture's real size. With Wide view it covers the whole width.

## Cost (RTX 4070 Ti)

Measured with `NXRT_VK_AA_TIME=1`, which logs the passes' own GPU time every 300 frames. The field of the play save, switched live from the
menu in one run (2026-11-08). SMAA's weights pass searches two pixels a step, by gathers.

| Picture | SMAA | FXAA | the whole frame's GPU time |
|---|---|---|---|
| 1920x1080, docked, 60 fps | 0.15–0.22 ms (copy 0.03–0.05, edges 0.04–0.06, weights 0.07, blending 0.02) | 0.18 ms (copy 0.08, FXAA 0.10) | 2.9 ms with it off, 3.05–3.2 ms with either |
| The character editor (the scene at 1920x1080; the character's 720x720 picture is not in the timing) | 0.11–0.25 ms | 0.13 ms | 2.6–3.1 ms |
| Handheld, 30 fps, the field (the 3D at 1600x900) | 0.32–0.36 ms (copy 0.07, edges 0.08–0.09, weights 0.13–0.16, blending 0.04) | 0.19 ms (copy 0.07, FXAA 0.13) | 7.6–7.9 ms, on and off alike (the GPU runs slower at half the frame rate) |
| 3840x2160 (Resolution 4K), docked, 60 fps, the field | 0.33–0.37 ms (copy 0.07–0.08, edges 0.08–0.09, weights 0.13–0.15, blending 0.06) | 0.25 ms (copy 0.08, FXAA 0.17) | 5.0–5.5 ms, the same within its spread with it off |

The weights pass costs most where long straight edges make long searches: the new game's Pokédex scene took 0.29 ms (weights 0.18).

## Tests

`nxrt.vk_aa` (`src/nxrt/vk/tests/t_vk_aa.c`) draws white on black as a game does without AA, on RGBA8 and sRGB targets, and compares it with
the true coverage (16 x 16 samples a pixel). The shapes are a disc (every slope) and a long shallow edge (1 in 7.7). It checks five things, with
validation on and no errors:
- SMAA and FXAA must bring the edge closer to that coverage. On the disc they reach about 68% and 62% of the distance without AA; on the
  shallow edge, 12% and 39%;
- every pixel two or more away from the edge is left exactly as it was;
- alpha stays opaque;
- a run on a rectangle from 0,0 (handheld's lower resolution) changes only that rectangle;
- Off does nothing.
