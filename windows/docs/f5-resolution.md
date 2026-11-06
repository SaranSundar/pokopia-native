# F-5: higher resolution (the game's render targets drawn larger)

Status, 2026-11-05: done for 2.0.1 and 1.0.5, docked and handheld, at 1.5x, 2x and 3x; committed on the `gfx` branch. Since the in-game menu
(docs/in-game-menu.md) the launcher and the menu choose it by picture size, "Default: your monitor" first (`NXRT_VK_RESOLUTION`), and it can change
while the game runs.

Pokopia draws at 1920x1080, docked and handheld alike (in handheld too its window crop and its screen-sized targets are 1920x1080: measured
2026-11-06, which corrects the "1280x720 handheld" this note first said), and the runtime used to scale only the finished picture to the window. With **Resolution 1.5x / 2x /
3x** (the launcher's Display tab, `NXRT_VK_SCALE=1.5|2|3`) the game's own screen-sized render targets are made that much larger and every draw into them
is scaled, so the 3D scene, its lighting and post effects and the HUD are rendered at 2880x1620 / 3840x2160 / 5760x3240. At 1x (the default) nothing
changes.

| | |
|---|---|
| Changed | `vk/vk_tex.c/.h` (which images are drawn larger, their size, readbacks at the game's size, taking one back to 1x), `vk/vk_replay.c/.h` (render areas, viewports, scissors, clears, copies and blits in each image's own pixels; the support buffer's render scales per draw), `vk/vk_shader.c/.h` (each texture's render-scale entry), `vk/vk_present.c/.h`, `vk/shaders/present.frag` (a picture larger than the window averaged down), `vk/vk_shot.c` (screenshots at the picture's own size), `scripts/play.ps1` (Resolution) |
| Contract | unchanged (`gfx_backend.h`, `nvn_obj.h`, `dmsh_abi.h`); the shader cache is unchanged (no new reflection field) |

## 1. How the shaders take it

Our SPIR-V comes from Ryujinx's shader translator (MIT), which already writes resolution-scale hooks into every shader that needs them, reading the
"support buffer" (`support_buffer`, binding 0 of a stage's set; Ryujinx's `SupportBuffer` layout, 1392 bytes) that `vk_replay.c` binds:

- **The fragment position.** A fragment shader divides `gl_FragCoord.xy` by `RenderScale[0]` (byte 192), so the game's own pixel arithmetic (deferred
  lighting, screen-space effects, dithering) keeps working in the game's pixels.
- **Texel fetches and sizes.** `texelFetch` / image loads on 2D textures go through a `TexelFetchScale(coord, index[, axis])` helper, `textureSize`
  through `TextureSizeUnscale(size, index)`. Both read `RenderScale[1 + index]`, `index` being the texture's rank by binding among the shader's texture
  descriptors (images after them); a vertex shader adds `FragmentRenderScaleCount` (byte 176) instead of 1. A negative scale in a fragment shader keeps the
  fragment's place inside the scaled texel (`coord * |s| + fragCoord mod |s|`).

Checked on all 4,162 shaders of the cache: the reflection's `translator` entries (the translator's own descriptors, already in every `vk/*.json`) give
exactly that order, so the runtime numbers them itself (`vks_bind.scale_idx`) and the cache needs no new field. `reads_scale` marks a module that reads the
render scales at all (an access chain into the support block's member 5).

Per draw and stage the replay fills: entry 0 = the pass's scale (fragment stage), entry 1 + i = the scale of texture i when its image is drawn larger,
negative in a fragment shader whose pass has the same scale. Draws that need none keep the fixed buffer (all 1.0); the others get theirs from the
submission's arena, and the same scales share one buffer within a submission.

## 2. Which images are drawn larger

Decided once, when an image is made (`scale_wanted`): GPU content (what the game renders; never textures read from guest memory), 2D or 2D arrays, not
block-compressed, attachment-capable, at least 64 x 64, no larger than the docked screen (1920 x 1080) and not square, not linear, not filled from guest
memory before its first use, within the device's limits. That takes the G-buffer, depth, the HDR scene, the half / quarter-size and 0.4-size targets, the
bloom chain and the final picture; it leaves the shadow maps (2048x2048, 2048x4096), the 80x45 and 2x1 targets, LUTs and atlases as they are. The log
names every scaled target and why another one is not, when a pass mixes them.

The last rule came from the first 2x runs: the whole 3D picture was darker than at 1x. A per-target comparison of one frame at 1x and 2x (`DM_CTL
texdump`, the clock fixed) found a 256x64 RGBA8 texture black at 2x: a lookup table the game writes into guest memory once and the lighting / tone
mapping read, which the runtime gives its first content from there (`init_fill`). Scaled, it started cleared. It is data, not a picture: never scaled now.

An image goes back to the game's size (`vkt_unscale`: a new image, the content blitted down, the old one retired behind the GPU, never scaled again this
run) when a compute shader binds it for stores (a dispatch covers the game's size) or when a pass attaches it beside one at the game's size.

## 3. Coordinates

Every NVN coordinate stays in the game's pixels; it is mapped into an image's own pixels where Vulkan takes it (`vkt_sx` / `vkt_sy`: both edges of a
rectangle rounded alike, so neighbouring rectangles still meet):

- the render area (the attachments' own size), viewports and scissors of draws, the rectangles of clears (render-pass clears and the masked clear draws),
  `ClearTexture` regions;
- texture copies: both rectangles mapped; a copy whose two sides differ in pixels becomes a (filtered) blit; resolves take the smaller size;
- buffer → scaled image: through a transfer image at the game's size, blitted up; scaled image → buffer: blitted down into one, copied out;
- CPU reads (`ReadTexels`, depth-compare twins): at the game's size, the nearest pixel of each; screenshots: the picture's own size.

## 4. Presentation

The present source reports its own size: the sharp upscaler compares the window with it, and a picture larger than the window is averaged over each
window pixel's footprint (present mode 3, 4 x 4 bilinear taps), not just sampled bilinearly, which would alias.

## 5. Tests

- **`nxrt.vk_scale`** (new, run with `NXRT_VK_SCALE=2`, validation on): which targets are scaled (a 128x64 one, not a square or a 32x16 one), the
  coordinate mapping, a clear's scissor in the image's own pixels, `ReadTexels` at the game's size, a buffer copied in (blitted up) and out (blitted
  down), a copy into a target at the game's size, a target taken back to 1x keeping its content and never scaled again, the render-scale entries of a
  shader's textures. 18 checks. `nxrt.vk_replay` (60), `vk_quads`, `vk_device` unchanged and passing.
- **1x unchanged.** The animated title (`DM_FIXED_TIME`, `TZ=UTC`, screenshots at presents 2400 / 3000 / 3600): main's build against itself
  31.3–39.7 dB, this build at 1x against main's 30.6–35.3 dB; the differences of both pairs are the same animated parts only (the bokeh, the picture's
  light rays, sparkles, the blinking prompt), every still part identical. At 1x no image is scaled and every new path is skipped.
- **2.0.1 field** (a copy of the play save, the clock fixed; `_local/gfx/route.ps1`: into the field, the camera turned, a few steps, the bag, the menu):
  no target taken back to 1x, no RED lines other than the run's usual ones. Against 1x, one frame target by target: the HDR scene 38.7 dB, the G-buffer
  32–34 dB, AO 28–32 dB (all finer at 2x, the same on average); the final picture box-filtered to 1920x1080: 34.0 dB. Edges, text, the HUD and the
  title's logo are visibly sharper.

| Resolution | Picture | GPU ms / frame | Presents / s | Image memory |
|---|---|---|---|---|
| 1x | 1920x1080 | 2.42–2.44 | 57.8 | 794 MB |
| 1.5x | 2880x1620 | 3.32–3.54 | 59.9 | 1,082 MB |
| 2x | 3840x2160 | 5.06–5.54 | 57.0 | 1,512 MB |
| 3x | 5760x3240 | 10.52–10.92 | 59.8 | 2,788 MB |

RTX 4070 Ti, the field, losttest's route without screenshots (the routes with 4K screenshots ran at 50 presents/s above 1x: writing such a PNG
stalls a frame); GPU time from timestamps around each submission. The frame rate is the replay's CPU time, not the GPU's.

- **1.0.5** (the phone's 1.0.5 save, copied; the same route): 1x and 2x alike in the field, the bag and the menu with its map; GPU 2.08 → 4.93 ms,
  59.7 → 59.0 presents/s.
- **Handheld** (`DM_OPMODE=0`) at 2x: the field, an NPC's speech bubble, the bag and the menu right; GPU 3.88 ms.
- **The title and a new game's first screens** (language choice) at 2x: right (the scripted new game stops at the language prompt, whose cursor
  starts on "No": the intro itself is not covered).
- **Validation on** (`NXRT_VK_VALIDATION=1`) at 2x, the title into the field: 0 errors (12 warnings, the usual "an output with no attachment").
