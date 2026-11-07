# M5 track B: the Vulkan backend's foundations

Status, 2026-11-03: **done, in the working tree for review (nothing committed).** `nxrt --gfx vulkan` opens an SDL3 window on a Vulkan
1.3 device, keeps the NVN memory pools in device-local buffers through write-protect dirty tracking, uploads textures through s2tex
(ASTC through BC7, with a cache), persists the pipeline cache while it runs, and shows every game present at the game's pace. With
Pokopia 2.0.1's M3 `game.dll` the title loop runs at **59.9 presents/s in the window, none dropped, 0 validation errors**. Drawing the
game's frames (the replay of draws, dispatches and texture copies) is M5-C; until then the window shows a test pattern per present.

| | |
|---|---|
| Code | `src/nxrt/vk/` (about 3,100 lines of C and C++, 1,200 of tests), built into `nxrt_core` and listed in track A's dispatcher (`gfx/gfx_ops.h`) as `vulkan` |
| Tests | 5 new (`nxrt.vk_device`, `vk_dirty`, `vk_tex`, `vk_astc`, `vk_pcache`); the whole suite is 33 tests, all pass |
| Outside `vk/` | `src/nxrt/CMakeLists.txt` (one `add_subdirectory(vk)`), `src/nxrt/host/hle_svc.c` (the heap's fault hook, §3.5: needed by any write protection in the heap, track A's tracker included) |
| Contract | unchanged (`gfx_backend.h`, `nvn_obj.h`, `dmsh_abi.h`) |

## 1. Dependencies

| What | Version | Where | Verification |
|---|---|---|---|
| Vulkan SDK | 1.4.363.0 (headers, `glslangValidator`, validation layer) | `C:\VulkanSDK\1.4.363.0` (installed) | – |
| SDL3 (zlib) | **3.4.18** (2026-10-02; 3.4.20 came out today, too new) | `<work>\toolchains\SDL3-3.4.18` (CMake `NXRT_SDL3_DIR`) | `SDL3-devel-3.4.18-VC.zip`, 16,902,234 bytes, SHA-256 `78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd`, equal to the `digest` GitHub's release API publishes for the asset |
| astc-encoder (Apache-2.0) | **5.7.0** (2026-07-31; 5.7.1 is from today) | `src/nxrt/vk/third_party/astcenc/`: the library sources (22 `.cpp`, 13 headers) and `LICENSE.txt` | tag `5.7.0` = commit `baff485b0ff36d2f95d28961605106502c653966`, which the release API names as the release's target and `refs/tags/5.7.0` points at; files taken from a clone at that commit (git checks every object's hash). Set digest (SHA-256 of the sorted per-file SHA-256 list): `f0ad06ff…63e670d` |
| bc7enc_rdo (MIT or Unlicense) | commit **`b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`** (2026-07-31; the repository has no releases or tags) | `src/nxrt/vk/third_party/bc7enc/`: `bc7enc.cpp/.h`, `bc7decomp.cpp/.h`, `LICENSE` | fetched by that commit hash; blob ids checked against its tree. Set digest `1136f7b5…e6069b68`. Not vendored: `bc7e.ispc` (Apache-2.0) and the bundled `ispc.exe` (never run) |

SDL3.dll is copied next to the executables at configure time and **delay-loaded**, and `vulkan-1.dll` is opened at run time
(`vk_fn.c`): `nxrt.exe` still starts on a PC without either under `--gfx null`. Without the SDK or SDL3, CMake builds the null backend only.

## 2. Window, device, presentation

**Window (`vk_window.c`).** An SDL3 window on a thread of its own that pumps its events: resizable, high-DPI (the swapchain follows the
drawable size in pixels), Alt+Enter / F11 for borderless fullscreen, a close handler (ends the run). A covered window keeps presenting; a
minimized one drops presents (logged once each way).

**Device (`vk_dev.c`).** Vulkan 1.3, one universal queue (graphics + compute + transfer + sparse binding), validation and debug names in
debug builds (`NXRT_VK_VALIDATION=1` turns it on in release; messages go to the log and are counted). `NXRT_VK_DEVICE` picks a GPU by index
or name; otherwise the first discrete one. On this PC: RTX 4070 Ti, driver 616.92, Vulkan 1.4.351.

- **Required** (the device is refused without them): dynamic rendering, synchronization2, timeline semaphores, descriptor indexing
  (runtime arrays, partially bound, non-uniform sampled-image indexing), BC textures, maintenance4, `VK_KHR_swapchain`.
- **Enabled when present**, every feature of these the device has (minus the robustness ones that cost speed; `robustBufferAccess` stays on
  NVIDIA, as Ryujinx does): custom border colours, transform feedback, depth clip control (NVN's -1..1 depth), 8-bit indices, conditional
  rendering, provoking vertex, robustness2's null descriptor, extended dynamic state 2 and 3, `VK_NV_viewport_swizzle` (NVN's viewport
  swizzles; NVIDIA only), memory budget, present id / wait, swapchain maintenance, line rasterization, vertex attribute divisor, list
  restart, stencil export, fragment shader interlock, advanced blend, attachment feedback loops, colour write enable, depth clamp 0..1, the
  graphics pipeline library, push descriptors, 4444 formats; all of Vulkan 1.0–1.3's optional features (geometry, tessellation, multiViewport,
  dual-source blend, logic op, depth clamp, anisotropy, 64-bit and 16-bit types, buffer device address, mirror-clamp-to-edge, min/max
  filtering, …); sparse buffers.
- **What the translated corpus declares** (track A's notes, `docs/m5-record-shaders.md` §7: 3,967 modules): `DrawParameters`, subgroup
  ballot / vote / shuffle, `StorageImageRead/WriteWithoutFormat`, `ImageGatherExtended`, `Geometry`, texel buffers. Checked at start
  (`shader corpus needs: … all present`) and by `nxrt.vk_device`. On this GPU all are present (subgroup operations 0x7ff in every stage).
- **NVIDIA-specific** on this PC: no `VK_EXT_shader_stencil_export`; `VK_NV_viewport_swizzle` and `VK_NV_geometry_shader_passthrough` are
  there (other vendors will not have them); the sparse block is 64 KB; ASTC is not sampled (as on every desktop GPU).

**Submissions (`vk_submit.c`).** One NVN submit = one recorded submission: an upload command buffer (with a transfer → everything barrier at
its end) and the main one (the replay's). A timeline semaphore numbers them; `gfx_sub_commit` returns the value. Committed submissions are
**batched**: held until a present, a wait, 4 of them, or 2 ms old, then handed over in one `vkQueueSubmit2` (at the title 33,000 NVN submits
became 9,000 queue submits: 11 → 3 a frame). A completion thread applies the CPU-visible effects in order **after** the GPU finishes (event
writes, counter records in NVN's 614.4 MHz ticks, write-backs), as the contract's ordering rule asks. Until the replay exists, `gfx_exec`
walks the records and keeps those effects: fences get the submission's value at the commit, events and counters follow completion, and
buffer copies, inline uniform updates and fills run on the GPU against the pool mirrors and land in guest memory at completion
(`NXRT_VK_CPU_EFFECTS=1` does them on the CPU, as the null backend does).

**Presentation (`vk_present.c`).** A swapchain on the window, FIFO by default, mailbox or immediate on request (`NXRT_VK_PRESENT`), recreated
on resize, B8G8R8A8 UNORM. `gfx_present` and `gfx_present_ext` enqueue a present with a ticket (a queue of 4, nxnative's model: the game's
acquire waits for its texture's last ticket); a present thread shows each one after the frame's GPU work, **scaled to the window with the
aspect kept** (letterboxed, any window size), sRGB content through a UNORM view, GL's lower-left origin flipped. The game's present period
is kept on a fixed cadence (a producer running ahead is held to it on any display rate; FIFO does the rest). Until M5-C draws, a present
shows the presented texture if the texture path holds content for it, else the test pattern (colour bars, a 16:9 grid, a bar that moves
one step per game present). `NXRT_VK_SHOT=<n>` writes present n as the window shows it, as a BMP next to the log.

## 3. GPU memory

### 3.1 Design

- **Every NVN memory pool gets a device-local `VkBuffer` of its own size**: buffer offset = guest address − pool base, so a GPU address
  resolves to a buffer and an offset (`gfx_addr`, `vkm_addr`). The buffer is **sparse** and backed in 2 MB granules the first time
  something is copied into them, so only what is used takes video memory (without sparse binding: fully backed, logged). **No host memory is
  imported** (research/01 lesson 10); this works the same on any driver and under Wine.
- **Dirty tracking is the recording layer's** (`gfx/nvn_dirty.c`, nxnative's tracker ported by track A; I had ported it too and dropped mine
  to keep one): pages are write-protected with `VirtualProtect`, a guest write faults into the runtime's vectored handler, which stamps the
  page and unprotects it. The mirror keeps, per page, the stamp its copy was taken at; a page is copied again when its generation is newer.
  Order per run, nxnative's: take the stamp, protect, then read. Host writes go through `dm_guest_write_prepare` as before. A run Windows
  refuses because part of it is not committed (a texture's storage spanning a 1 MB heap chunk the game has not touched) is protected region by
  region: only the uncommitted pages count as always written (#27; the whole run used to).
- **Uploads** go through a **staging ring** (64 MB, host-visible), into the submission's upload command buffer, batched as one
  `vkCmdCopyBuffer` with up to 256 regions per pool; runs of up to 4 MB. A ring that fills waits for the oldest submission; a submission
  that alone needs more takes host blocks of 32 MB (or its allocation's size), released with it, and fills each before the next (#27: it was a
  buffer per allocation, about 96 a frame).
- **Per-frame budget** (`NXRT_VK_UPLOAD_MB`, 64 MB): background uploads (the whole-pool sync below, ASTC high-quality replacements) stop at
  it; uploads a submission needs never do.
- **Hot pages**: a page written in 3 frames in a row stops being protected (no fault per write) and is copied once per submission (the GPU reads
  NVN memory when the submission runs, and the game does not rewrite what a submission reads before it is done); every 240 frames hot pages cool
  down (protected again at their next copy). They were copied at every use: #27's 4 MB hot range, bound by a dozen draws and dispatches a frame,
  was 50-94 MB of uploads a frame. `NXRT_VK_HOT_EVERY_USE=1` brings that back.
- **Uncommitted pages** (the heap commits lazily; most of Pokopia's 3.4 GB pool is never touched) are neither read nor protected: a use reads
  them as zeros and keeps asking, the background sync steps over the region.
- **GPU writes into pools** (copies, fills, uniform updates now; storage writes, transform feedback, query results with the replay) are
  copied back to guest memory when the submission completes; their pages are marked in flight meanwhile so a sync cannot overwrite the newer
  device copy with old guest bytes. A page the write covers whole, which the CPU has not written since its copy and no other GPU write is on its
  way to, then holds the same bytes on both sides: it is protected again and counts as copied instead of going back up (#27: the light pass's
  10 MB every two frames went GPU → guest memory → staging → GPU unchanged, and a buffer written back every frame turned hot).
  `NXRT_VK_WB_RECOPY=1` copies every written-back page up again, as before.
- **`NXRT_VK_UPLOAD_TOP=1`** adds to each memory line the ranges copied most since the last one, how often, and why their pages were behind
  (never copied, hot, written back by the GPU, written by the CPU).
- **Two policies**: `vkm_use(addr, len)` brings a range up to date for the submission being recorded (the replay's, per binding) and
  `vkm_sync_all()` copies every written page of every pool, round robin, within the budget. Until the replay asks per use, the backend runs
  `vkm_sync_all` once a frame (`NXRT_VK_SYNC=all`, the default now), which measures the worst case; `NXRT_VK_SYNC=use` is M5-C's setting.

### 3.2 Measured at the title (Pokopia 2.0.1, RTX 4070 Ti, `--gfx vulkan`, frames 1,500–3,900)

| | per frame |
|---|---|
| Pools | 5, 4,988 MB in all; the game has committed about 2.5 GB of them, mirrored (2,546 MB backed) |
| Uploaded | 210–260 KB in about 3 runs (copy and scan: **0.38–0.43 ms CPU**) |
| Write faults | 45–52 (protection changes in all, faults and re-protection: 0.1–0.4 ms) |
| Uncommitted pool stepped over | about 540 MB (no reads, no faults) |
| Loading (frames 600–1,200) | 3.3–4.1 MB a frame (1.7–2.2 ms), within the budget; one staging overflow in the run |
| NVN layer's own measure, "submit+present a frame" | 0.85 ms with the whole-pool sync (validation on); 0.46 ms with `NXRT_VK_SYNC=use`; 0.01–0.06 ms on the null backend |
| One write fault, measured alone | 1.8 µs (protect + write + fault + unprotect, `nxrt.vk_dirty`) |

So the title's steady state writes about a quarter of a megabyte a frame into its pools and costs well under a millisecond to mirror in full.
With the replay the cost falls to what draws actually bind, and video memory to what they use (instead of every committed page).

### 3.3 The heap's fault hook (`host/hle_svc.c`)

The heap commits 1 MB chunks lazily, and its fault hook ran first and answered *every* fault in the open heap by re-opening the whole chunk
read-write. Write protection anywhere in the heap (track A's tracker and this mirror) was silently undone, and the tracker never saw the
write: stale textures, missed uploads. Now a committed chunk is never re-opened wholesale; a write fault on a read-only page goes on to the
tracker's hook; any other fault opens that one page and runs the access again (a first version returned "not mine" when a racing thread had
already opened the page, which crashed the null backend too; caught by the baseline run and fixed).

## 4. Textures

### 4.1 Formats (`vk_fmt.c`)

All 156 NVN formats with a counterpart are mapped (entry by entry in NVN order), each with its upload conversion and the component mapping
its views start from. On this device: **107 exact, 18 converted on upload** (the 3-component formats, expanded to 4), **31 device
fallbacks**: the 28 ASTC formats → BC7 through the transcoder and the 3 signed 10:10:10:2 formats → RGBA16 (on devices that lack them, also
D24S8 → D32S8, D24 → D32F and S8 → a depth-stencil format, research/01 lesson 8). `*_UI2F` / `*_I2F` are the integer format plus a flag for the shader translator (Vulkan's
scaled formats are vertex-only). **Fallbacks apply to every view** (research/01 lesson 8): every view goes through
`vkf_view_format(image format, view format)`, so an image created with a fallback can only be viewed in it (an sRGB view of an ASTC image
takes BC7 sRGB). Not yet confirmed on real data (flagged in the table, as in nxnative's): the bit order of the packed 16-bit formats and the
depth / stencil layout of D24S8 storage.

### 4.2 Uploads (`vk_tex.c`)

Storage addressing is `nvn_tex.c`'s (level offsets, per-level block heights, layer stride: what the recording layer computes), the byte order
s2tex's in Switch 2 order, through track A's `gfx/nvn_texio.c`; each level and layer is deswizzled into the staging ring and copied with one
`vkCmdCopyBufferToImage` batch; storage that is not mapped any more reads as zeros instead of crashing. **Which textures read their storage:**
packaged layouts, textures the CPU wrote (`gfx_tex_cpu_written`), and **every block-compressed texture in a CPU-writable pool, IMAGE flag or
not** — a BC or ASTC texture cannot be a render target or a storage image, so its storage is the only place its content can come from
(Pokopia's icons are BC1 / BC7 flagged IMAGE; treating IMAGE as GPU-owned is what left them stale). Such textures are uploaded again when
their pages are written (stamp, protect, read). Everything else is a GPU texture (render target, depth, storage image when IMAGE and the
format allows), cleared at creation.

**Sharing** is keyed by storage (pool, offset), shape (target, size, levels, samples, layout), format and usage class (from storage / GPU,
storage-image use): a new texture object over the same storage with the same description gets the same image, and sees the current storage.
A texture over storage that overlaps another description of the same pool evicts it unless it was used in the last 16 frames (nxnative's
take-over rule); finalizing a pool evicts its images. Images rest in one layout between submissions (shader-read for sampled, general for
render targets and storage), which the present thread and the replay rely on. Samplers are cached by their NVN parameters (custom border
colours, min/max reduction, mirror-clamp-to-edge); texture and sampler registrations are kept for the replay.

### 4.3 ASTC → BC7 (`vk_astc.cpp`)

ASTC blocks are deswizzled, decoded by astc-encoder and encoded to BC7 by bc7enc. **First use: a fast pass** (mode 6 only, on at most 4
threads) uploaded at once; the key (a 128-bit hash of the blocks, the shape and the encoder version) and the pass go into a persistent cache
(`%LOCALAPPDATA%\pokopia-native\vulkan\astc-bc7`, or `NXRT_VK_CACHE_DIR`), written atomically. **A high-quality pass** (modes 1 and 6, 64
partitions, uber level 1) runs on one worker thread at the lowest priority, is cached, and replaces the fast data in the same image at a
later submission within the upload budget (same format and size: views and descriptors stay valid). A cached high-quality result is used
directly on the next start (research/01 lesson 18). Speed here: decode 102 Mtexel/s, fast pass 58 Mtexel/s (a 1024×1024 texture in about 28
ms all told), high quality 2.9 Mtexel/s in the background (0.37 s for 1024×1024), a cache hit 5 ms. The corpus of 591 textures has no ASTC,
so it may be rare in Pokopia.

## 5. Pipeline cache (`vk_pcache.c`)

One `VkPipelineCache` for the device, loaded from `pipelines-<vendor>-<device>.bin` when its header matches this device and driver
(`pipelineCacheUUID`). A saver thread writes it **while the process runs**, 2 s after new pipelines stop arriving, through a temporary file
and an atomic replace (research/01 lesson 16: a cache written only at exit is never written when the process is terminated). In the game
runs, killed by a timeout, the file was there; in the test, 48 pipelines took 123 ms cold and 8 ms from the reloaded cache, all 48 reported
as cache hits. The replay calls `vkpc_note_created()` for each pipeline it makes.

## 6. Tests and results

| Test | What | Result |
|---|---|---|
| `nxrt.vk_device` | the device's required features and the shader corpus's; a window and a FIFO swapchain; 120 presents at the game's 60 Hz period on a 160 Hz display; the window at 1920×1080, 2560×1440, 3840×2160, 3440×1440, 1280×1024 and 900×1600, the picture captured from the swapchain and checked inside and outside the letterbox; a CPU-written texture presented and its colours read back from the window; mailbox; validation on | 28 checks pass; 60.5 presents/s; mailbox about 3,000 per s; **0 validation errors** |
| `nxrt.vk_dirty` | the tracker alone: one fault for two writes to a page, only that page newer, host pre-writes stamp, fault cost; the mirror: first sync copies every page, unwritten pages are not copied again, 3 written pages → 3 faults → 3 pages in 2 runs, the device copy equals guest memory after each step; hot pages stop faulting and cool down; `vkm_use` copies only its range; a GPU copy, a fill and an inline update reach guest memory at completion | 22 checks pass; 1.8 µs a fault |
| `nxrt.vk_tex` | 11 shapes and formats through s2tex and back from the GPU against the CPU decode (mip chains, arrays, a cube, 3D, non-power-of-two, BGRA sRGB, RGB8 and RGB32F expanded, BC1, BC5 arrays, BC7 flagged IMAGE); the icon rule over time (rewritten storage uploaded again, a new object over the same storage shares the image and shows the new content, GPU textures are render targets / storage images, finalizing evicts); every NVN format created, uploaded and viewed; fallbacks on views; **the real corpus** | 31 checks pass; 156 of 156 formats; **corpus: 591 of 591 textures read back from the GPU equal to the CPU decode and equal to the Mac host's decode** (245.8 MB of storage, 301 MB/s one texture at a time) |
| `nxrt.vk_astc` | images (gradient, bands at 250×130, sharp edges with alpha, noise) compressed by astc-encoder at 4×4 to 12×12, LDR and sRGB; transcoded, decoded with bc7enc's decoder, compared with astc-encoder's decode; the worker's pass from the cache; the cache across a restart; speed | 193 checks pass. PSNR fast / high: gradient 50.8–53.9 / 50.7–55.6 dB, bands 34.8–36.9 / 35.5–37.6 dB, edges 47.1–57.2 / 48.7–57.2 dB, noise (incompressible, a sanity bound) 16.4–27.6 / 18.9–33.9 dB. Tolerances: 45, 33, 45, 15 dB; the high pass never below the fast one by more than 0.25 dB |
| `nxrt.vk_pcache` | pipelines created, the file on disk while the process runs, no temporary file left, the header is this device's, a new start loads it and the pipelines come from it | 11 checks pass |
| the game | `nxrt.exe <ExeFS> --dll _local\nxlift\m3\game.dll --romfs … --save <copy of run4> --gfx vulkan`, 75 s, killed by timeout | title loop at **59.9 presents/s** in the window (p50 16.6 ms), 0 dropped, 0 validation errors at frame 3,000, the same pace as the null backend on the same build; the pipeline cache on disk after the kill; `_local/nxrt-run/m5vk-final` |

Every test skips (77) without a GPU or display; the corpus part skips without `_local/s2tex/corpus`. The full suite: 33 tests pass
(`nxrt.svc` has a timing check that fails now and then on the 18:12 build from before this work too: a 2 ms sleep returning in under 1.9 ms).

## 7. What replay (M5-C) needs

- **`gfx_exec`** for draws, dispatches, `COPY_B2T` / `T2B` / `T2T`, `CLEAR_TEX`, `DRAW_TEX`, render targets and state, recorded into
  `vks_cb()`: textures through `vkt_resolve` + `vkt_view` (fallbacks already applied) + `vkt_sampler`, registrations through
  `vkt_reg_get` / `vkt_samp_get` (capacity `vkt_pool_n`); **every buffer range through `vkm_use`** (vertex, index, uniform, storage, indirect,
  texel buffers), then switch the default to `NXRT_VK_SYNC=use`; **every GPU write into a pool through `vkm_gpu_wrote`** (storage writes,
  transform feedback, texture-to-buffer copies, query results).
- **Image layouts**: leave each image in `im->rest` at the end of a submission (the present thread samples it there); a GPU write into a
  storage-class (block-compressed) texture must also reach its storage (the recording layer's `nvn_tex_mirror_*`) or it is overwritten by the
  next upload from storage.
- **Pipelines** from track A's SPIR-V and reflection (`src/nxshader`), made with `g_vk.pcache` and announced with `vkpc_note_created`; depth
  mode -1..1 through `VK_EXT_depth_clip_control` (`gfx_set_depth_zero`), the translator's upper-left origin and no Y flip (track A's notes),
  viewport swizzles through `VK_NV_viewport_swizzle` where present.
- **Counters**: `vks_post_counter` writes the time and 0 for everything but timestamps; occlusion queries and the other counters are the
  replay's. Conditional rendering is enabled on this device.
- **Resolution independence at the renderer**: present already scales any picture to any window; render scale (rendering above the game's
  1920×1080) needs the replay's render targets.
- **Performance, later**: write straight into device-local memory where the GPU exposes it to the CPU (ReBAR) instead of staging; a written-page
  bitmap in the shared tracker (a scan of generations is O(pages) now); a background thread for the whole-pool sync.

**Found in track A's tracker, not changed (`src/nxrt/gfx` is theirs):** when `VirtualProtect` fails (uncommitted pages: error 487 at the
first run, before §3.1's step-over), `nvn_dirty.c` logs once but still marks the pages protected, so later writes to them are never seen;
leaving them unprotected and stamped would be safe. The mirror no longer asks it to protect uncommitted pages.

## 8. Reproducing

```
scripts\build.cmd                    (or the same settings in a folder of its own; this work used build\m5vk-Release, -j 4, low priority)
ctest --test-dir build\m5vk-Release  (with _local\s2tex\corpus present for the corpus part)
build\m5vk-Release\bin\nxrt.exe <ExeFS> --dll _local\nxlift\m3\game.dll --romfs <RomFS.img> --save <a copy of _local\saves\run4\save> --temp <dir> --log <dir> --gfx vulkan
    NXRT_VK_VALIDATION=1  NXRT_VK_PRESENT=fifo|mailbox|immediate  NXRT_VK_WINDOW=1920x1080  NXRT_VK_SYNC=all|use  NXRT_VK_UPLOAD_MB=64
    NXRT_VK_CACHE_DIR=<dir>  NXRT_VK_DEVICE=<index or name>  NXRT_VK_SHOT=<present number>  NXRT_VK_CPU_EFFECTS=1  NXRT_VK_HEADLESS=1
```
