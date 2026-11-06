# Would DLSS / FSR 2 have their inputs? (research only)

Status, 2026-11-05: done (research only). From pass dumps of Pokopia 2.0.1 at 1x in the field (`NXRT_VK_PASSDUMP` / DM_CTL `passdump`: the passes, their
targets, what each draw reads and the first constants of its buffers), one frame and then two consecutive frames with the clock fixed.

DLSS Super Resolution, FSR 2/3 and XeSS build a picture from several frames. Each needs, every frame: the scene colour before post-processing, depth,
**motion vectors** for every pixel, and a **jittered projection** (the camera shifted by a sub-pixel offset that changes every frame), plus the jitter
values. A game that already does temporal anti-aliasing (TAA) computes all of these, and a port can borrow them. A game without TAA has none.

## What the frame does

| | Pass (targets) | Reads |
|---|---|---|
| shadows | 3 x depth 2048x2048 D16 | |
| G-buffer | RGBA8_SRGB, RGBA16, RGBA8, R11G11B10F (+ the RGBA8 again) + D24S8, 1920x1080 | material textures |
| small passes | 80x45 colour + depth; depth at 960x540 and 480x270 | albedo, depth |
| AO | 768x432 R32F, 768x432 R8, 1920x432 R8, 1920x1080 RGBA8 | depth, RGBA16 |
| lighting | R11G11B10F 1920x1080 (the HDR scene) + depth | the G-buffer, depth, AO, shadows |
| transparent / effects | the HDR scene + depth; 960x540 colour + depth | the HDR scene, depth, 960x540 |
| bloom | one R11G11B10F texture, 960x540 down to 60x33 and back | the HDR scene |
| exposure | compute: a histogram of the HDR scene, a 2x1 exposure | |
| tone map | R11G11B10F 1920x1080 | the HDR scene, the bloom, another 960x540 target, the exposure, a 32x32x32 colour LUT |
| anti-aliasing | RGBA8_SRGB 1920x1080 (the final picture) | **only the tone-mapped picture**, with the screen's texel size from its constants |
| HUD | the final picture | UI atlases |

## Findings

- **No motion-vector target.** None of the G-buffer's targets is a two-channel float velocity format (RG16F is the usual one); the RGBA16 target is read
  by the AO and lighting passes as surface data. No pass writes a screen-sized target that only the anti-aliasing or a temporal pass reads.
- **No history.** The anti-aliasing pass reads one texture, the tone-mapped picture of the same frame: a spatial filter (FXAA-like, fed the 1/1920,
  1/1080 texel size), not TAA. The only input of any pass not written in the same frame is a 960x540 target the tone mapping reads, and it is written
  in neither of two consecutive frames: not a per-frame history.
- **No jitter.** The G-buffer's view-projection matrix (the first constant buffer of its 1,080 vertex-shader draws) has a first row of exactly
  (1.358, 0, 0, 0), and the matrix is the same (to the six digits the dump prints) in two consecutive frames with the camera still: a jitter would add a
  sub-pixel share of the w row that changes every frame.

So the game computes neither motion vectors nor a jittered camera: **DLSS, FSR 2/3 and XeSS would not have their inputs.** Making them would mean
patching the game's own shaders and camera (a jitter in its projection, a velocity output in every G-buffer shader, the previous frame's matrices for
skinned and moving objects), a large game-specific change, or guessing vectors from the pictures, which looks worse than today's upscalers.

What remains possible without the game's help: spatial upscalers (our Sharp upscaler, FSR 1 / NIS) on the finished picture, and the resolution scaling
of F-5 (the game's own targets drawn larger), which is the real increase in detail. Since the game is CPU-bound on Windows (about 2.5 ms of GPU per
frame at 1x, 5.5 ms at 2x on an RTX 4070 Ti, at the same frame rate), an upscaler would not raise the frame rate either.
