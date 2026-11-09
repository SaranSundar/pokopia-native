# The NVIDIA hang after early returns (#12, #21, #22, D-1)

Status, 2026-11-06: `vkspv_ballot_per_lane` (`src/nxrt/vk/vk_shader.c`) rewrites every warp-aggregated counter of the shape below in the 2.0.1 shader
cache: 12 ballots in 11 shaders. Three counters of other shapes are left as translated (below). Only on NVIDIA's GPUs (vendor 0x10de), whose hang
it is: the lane's own bit assumes 32-lane subgroups (nxrt does not pin the size; AMD's are often 64), and Mesa's lavapipe crashed in the rewritten
counters (the Linux session's 1.3.0 bisect).

## What hangs

NVN's compiler aggregates an atomic add over a warp: `ballot(true)`, the ballot's highest lane leads (`findMSB`), and the leader adds the ballot's
bit count once for all of them. Pokopia's compute shaders do this after some lanes have returned. On NVIDIA GPUs such a dispatch can stop the
GPU for good (Windows resets it: "device lost"):

- **#12, #21, #22**: RTX 50 cards, driver 617, shader `d2c50cca6e386abc`. The reporter bisected it by patching the SPIR-V: replacing the ballot with
  the lane's own bit fixed it; capping loops, removing the barrier, masking the shuffle lane or removing only the atomic did not.
- **D-1**: RTX 3050 Laptop, driver 621.14, a 1x1x1 dispatch of shader `2cb5df5cb96b324e`, twice (at 386 s on 1.2.5, at 984 s on 1.2.6). Its counter
  has the same shape, and the add's result is read.

## The rewrite

Each counter's `ballot(true)` becomes `uvec4(1 << lane, 0, 0, 0)`: every lane leads itself and adds its own share. Taken in three shapes:

1. **The result not read** (d2c50cca): the same sum.
2. **The result handed out** (2cb5df5c and 6 more): each lane takes the leader's result through the translator's shuffle helper (NVN's `SHFL.IDX`,
   a function of its own) plus its rank, `bitCount(LtMask & ballot)`. Per lane, the shuffle reads the lane's own value and the rank is 0: every
   lane gets a slot of its own, as many slots as before, in the same range; only which lane takes which within a warp changes.
3. **Entries of a constant size** (7f1a413b, a36bcf86, e3745de3): the leader adds `count << 2` and each lane's rank is scaled by 4 (worked out in
   16-bit halves, as NVN multiplies).

Taken only when the ballot reaches nothing but the leader, the count and an AND with `gl_SubgroupLtMask`; the count nothing but the add (scaled by a
constant) and the result's variables; the leader nothing but the leader test and the shuffle call; the result nothing but that call; the rank
nothing but masks, constant scales, adds and variables. The translator keeps each value in a variable stored once and each GPU register in a
variable stored many times: the rank is followed through the first kind, not into a register (NVN's rank is dead after its add; `560a0262fe47818a`
writes it to a register that a later predicated add reads only when its predicate is false, and that add's compare is masked by the same
predicate). `NXRT_VK_BALLOT_FIX=0` leaves every ballot as translated, `=1` rewrites them on any GPU; unset, NVIDIA only. The log says which applies
and names each shader rewritten.

## The whole cache

`ballot_census.py` (a CPU-only reading aid, not in the repository) counts every subgroup ballot by shape and by whether a return comes before it in
the same function. The shared cache our runs use (4,029 shaders, 2.0.1 and 1.0.5) and 2.0.1's build cache (3,819) give the same shaders:

| Shape after an early return | Ballots | Shaders | Rewritten |
|---|---|---|---|
| counter (bit count → atomic add, the count scaled or not) | 10 (+2 before any return) | 11 | all |
| counter adding a masked sum of the lanes' own amounts (one ballot gathers the amounts by 32 shuffles, one picks the lane that adds) | 4 (2 counters) | 5c587593b1de61cd, 9506b27c35b91e67 | no |
| counter adding the count times a stride from a constant buffer (16-bit halves) | 1 | d41124ad6ffa56b5 | no |
| first-lane broadcast (`subgroupAny(true)`, the lowest lane, a shuffle) | 25 | 09212c09, 2cb5df5c, d2c50cca | no (never seen to hang) |
| a ballot of a varying condition | 3 | 09212c09, 2cb5df5c | no |
| `subgroupAll` | 1 | 2cb5df5c | no |

What is known about the ones left: the first-lane broadcasts sit after the early returns in d2c50cca too, and #12's fix (its counter alone) was
enough on RTX 50. 9506b27c35b91e67 runs in 168 of our run logs and in D-1's; d41124ad6ffa56b5 in D-1's; neither is the shader that hung. Both
shapes could be made per lane as well (each lane adds its own amount, or its own stride), but the check that the sum and the stride are taken
whole is longer than the one above and was left for later.

## Checks

- `t_vk_ballot` (14 checks, RTX 4070 Ti, driver 616.92): small shaders of each shape are rewritten (d2c50cca's, 2cb5df5c's, 4-unit entries) and left
  alone when the add's result is read otherwise or the rank is compared; on the GPU the rewritten counters count every running lane and hand each a
  slot of its own (1..count, none twice), as the originals do with 32-lane subgroups.
- Both caches: the same 11 shaders change (12 ballots). Each rewritten shader passes `spirv-val --target-env vulkan1.2`, and decompiled it differs
  from the translation only in its ballot lines. Each was read by hand: the count only reaches the add, the leader only the leader test and the
  shuffle, the rank only the slot.
- The game, on a copy of the 2.0.1 save, a scripted 150 s route from the title into the field: d2c50cca, 2cb5df5c, 560a0262 and a36bcf86 rewritten and
  dispatched, no device lost; with the validation layer (before the scaled counters were added) no errors. The pictures match the route without the
  rewrite by eye. They do not match pixel for pixel: the route turns the camera by holding the stick for 2.5 s of wall-clock time, and the runs
  without the rewrite hitch for about 200 ms during the turn (they turn less).

## Not verified

We have no RTX 50 or RTX 3050 here: that the rewrite stops D-1's hang is inferred from #12's bisect and the shared shape, not reproduced.

## After 1.3.0: D-1's next hang, and 64-lane GPUs

**D-1 on 1.3.0** (RTX 3050 Laptop, driver 621.14) hung at 160 s in a 16x16-group dispatch of `634565ab77c61463`. That is not a ballot shape (it uses no
subgroup operations): each of its 262,144 threads walks a table of items, 5 texels each, keeping the nearest, for `count` iterations, `count` being
the first word of its constant buffer read as an integer, with nothing else bounding the loop.
- In play (our pass dumps, field and a conversation) it runs every frame, its table holds 40 texels (8 items), and its constants are 256 bytes at a
  new aligned address each frame, read from the GPU's copy. A hang needs that word to be wrong when the GPU runs it (1.0f read as an integer is about
  a billion iterations). Why it was wrong on his PC is not known; his upload staging overflows 7x as often as ours (a slower GPU further behind).
- **The guard** (`vkspv_limit_count`, applied by key): the loop's comparisons use `min(count, the items the
  table holds)`, from the table's own size (the translator lays NVN's texel buffers out in rows of 16,384 texels: `((rows - 1) * 16384 + width + 4) /
  5`). Every sane frame walks the same items; a garbage count walks 8 instead of a billion.
- **NVIDIA only since 1.3.2** (`vkspv_loop_guard_wanted`, as the ballot rewrite): on 1.3.1 AMD's Windows driver crashed making the guarded
  pipeline (santriman's Radeon 890M, amdvlk64.dll, 19 s in, right after the guard's log line; 1.2.x ran the unguarded shader for minutes). The hang
  was only seen on NVIDIA (D-1's RTX 3050 Laptop, D-11's GTX 1650). `NXRT_VK_LOOP_GUARD=1` makes it on any GPU, `=0` turns it off (on 1.3.1 too).
- **The report:** on NVIDIA, a lost device prints the first 16 words (8 in 1.3.1) of each dispatch's first constant buffer as guest memory held them
  when it was recorded, beside the checkpoint that names the dispatch, with word 0 as an integer and words 12 and 13 as unsigned.

**D-11 on 1.3.1** (GTX 1650, driver 621.14): the guard held on `634565ab`, then after 62 minutes the GPU hung in a 30x17x16-group dispatch of
`560a0262fe47818a`, the per-cluster light lists (one of the ballot-rewritten counters). It has the same weakness: two loops, `for (i = lane; i < count;
i += 64)` over the lights of a texel table (one texel a light), with `count` constant buffer words 12 and 13, read and compared as unsigned, and nothing
else bounding them. The report's eight words were sane (a matrix row), but they do not reach words 12 and 13.
- **The same guard** for both loops (`vkspv_limit_count` takes the word and, for an unsigned count, the unsigned minimum: 0xffffffff is no negative
  number), NVIDIA only by default and switched with `634565ab`'s by `NXRT_VK_LOOP_GUARD`.
- Whether the counts were garbage is not known; both shaders read their counts from the game's per-frame constants, so a count read while the game
  rewrites its slot is the lead. The 16-word report shows words 12 and 13 next time.

**64-lane GPUs.** NVN's shaders are written for 32-lane warps: ballots read `.x`, the shuffle helpers clamp to lane 31, votes span 32. AMD's GPUs run
64-lane subgroups by default (santriman's Radeon 890M on 1.2.6: "size 64"), Intel's 8 to 32. Of the cache's shaders 49 use subgroup operations (15
compute, 34 fragment); on 64 lanes a warp-32 counter sees only lanes 0-31 in its ballot and gives lanes 32-63 slots past the space it reserved, and
`9506b27c35b91e67` (a masked-sum counter) runs every frame in the field and in conversations.
- **Subgroup size control** (Vulkan 1.3), **opt-in in 1.3.1**: with `NXRT_VK_SUBGROUP32=1`, where the driver lets a pipeline require 32
  (`requiredSubgroupSizeStages`), every shader that uses subgroup operations is made with 32-lane subgroups, and compute work groups that are whole
  32-lane rows get full subgroups. One log line says so (or, in red, that the driver cannot). Unset or `0`, the default until an AMD or Intel GPU has
  run it, leaves the driver's size, as in 1.3.0; the log line then names the switch. NVIDIA (always 32) is unchanged either way.
- The ballot rewrite stays NVIDIA's only.

**Checked here** (RTX 4070 Ti): `t_vk_guard` 17 of 17, 18 with a real shader through `NXRT_TEST_SPV` (a sane count unchanged, a count of a billion walks the table's items at once, one and two
rows, the edited module valid, the guard on by default only for NVIDIA; `test_guard_u.comp`, 560a0262's shape: word 13, unsigned, 0xffffffff and 1.0f
walk the table's items; on the real `634565ab` two comparisons limited, on the real `560a0262` four, spirv-val passes, decompiled only those lines differ);
`t_vk_ballot` 16 of 16; the 2.0.1 route with validation on, `NXRT_VK_SUBGROUP32=1` and the guard: 0 errors, pictures right. **Not checked:** an AMD or
Intel GPU (only santriman on 1.3.1, or a Steam Deck tester), a real lost device's new report line.

## After 1.3.3: a list loop whose length is data (Styphix)

**On 1.3.3** (most likely NVIDIA, about 3.3 GB of video memory; the log's header was cut off), after about 30 minutes of Link Play online, the GPU hung
in a one-group dispatch of `2cb5df5cb96b324e`. The driver gave no description; its fault was an unknown instruction pointer, which means a timeout
rather than a bad access.
- **What the shader does:** one 64-lane group walks a list of entries in the game's data texture (`cp_t_tcb_C`, rows of 16384 words). Each entry's
  high half picks a case of a switch: the inner `do … while` loops end within two passes, and the case draws random numbers or writes results.
- **The only unbounded loop** is the walk itself: `do { …; i++; } while (!(i >= count))`. `count` is a word of the same texture at the list's start,
  made the same for every lane (ballot, lowest lane, shuffle) and kept in function variable `%2154`. A huge `count` (stale data, a ring's
  write-minus-read gone negative, a start that points at the wrong place) runs it for billions of passes.
- **The guard:** `vkspv_cap_bound` puts min(count, 16384) in that variable's one store, as an unsigned minimum. It checks the module first: a uint
  function variable, stored once, read by an unsigned `i >= bound`; otherwise it logs in red and changes nothing.
  - It is NVIDIA only by default and switched with the other guards by `NXRT_VK_LOOP_GUARD`.
  - Why 16384: the card's whole frame took about 7 ms of GPU time, so real lists are short, and 16384 passes of one group cost tens of milliseconds at
    worst.
- **Not known:** what made `count` huge, and whether it happens outside Link Play.

**Checked here** (RTX 4070 Ti): `t_vk_guard` 23 of 23. `test_guard_v.comp` has 2cb5df5c's shape: a count of 0xffffffff stops at the cap, sane counts are
unchanged, and a variable stored twice is left alone. The real `2cb5df5c`, capped through `NXRT_TEST_SPV`, passes spirv-val. In the game, the cap is
applied after the ballot rewrite, and the prewarm makes its pipeline. The shader is not dispatched in single-player village play, so the capped loop
itself has not run here.

## After 1.3.4: the same hang offline, AMD's hangs, and a driver that crashes making a pipeline

**The 2cb5df5c hang without Link Play.** A GTX 1650 player (D-11's) on 1.3.4: after about an hour of single-player play the GPU hung in a one-group
dispatch of `2cb5df5cb96b324e`, the same as Styphix's. So it is not Link Play's. The cap above covers it on NVIDIA.

**AMD hangs after about an hour** (a Radeon 890M player, the D-8 one): a lost device 3 s after an autosave, with nothing to say which shader, because
NVIDIA's checkpoints do not exist there. Two changes:
- **The loop guards on every GPU.** On NVIDIA they stay the table-size guards. Elsewhere `634565ab`'s and `560a0262`'s loops are capped at 16384
  instead: `vkspv_limit_count` without a table is a plain minimum, with no size query (the edit AMD's compiler crashed on in 1.3.1). `2cb5df5c`'s cap
  was a plain minimum already.
  - `NXRT_VK_LOOP_GUARD`: `1` the table-size guards, `cap` the caps, `0` none, on any GPU.
  - Each guarded shader keeps its module as translated beside the guarded one. If the driver cannot make the guarded pipeline, it is made from that
    module (below).
- **AMD's buffer markers** (`VK_AMD_buffer_marker`) where there are no checkpoints, at the same places. Each marker writes the checkpoint's number into
  host memory twice: once as the GPU's front reaches it, and once everything before it has finished. A lost device then names what was started and
  what was in flight, with the dispatch's constants, as on NVIDIA. The stats lines show the markers moving. `NXRT_VK_CHECKPOINTS=markers` takes them
  instead of NVIDIA's checkpoints (tests), and `=0` turns both off.

**A driver that crashes making a pipeline** (an RX 6750 XT on 1.3.4): AMD's Windows driver crashed in its compiler (a write to 0x8 in amdvlk64.dll)
making fragment shader `b1f4b2d7c3863ab0`'s pipeline, at the same spot every time.
- **What is known about the trigger:** nothing in that shader is unusual. Every operation, image type and capability it uses also appears in shaders
  that driver made in the same session. Its interface matches its vertex shader. Its binding count and size are matched by siblings that worked
  (`6e582a8105e0c915` is nearly the same shader). Without AMD's compiler here the trigger is not known.
- **The catch:** `vkCreateGraphicsPipelines` and `vkCreateComputePipelines` run inside a structured exception handler (vk_pipe.c). It covers an
  access violation, an illegal instruction, a division by zero, a stack overflow and a page error.
  - A crash there counts as a pipeline that could not be made. Its draws or dispatches are left out, the log says where the driver crashed, and the
    game goes on.
  - A loop-guarded compute shader is made again from its module as translated.
  - What the driver held when it crashed is lost. If that leaves it unable to make later pipelines, those are left out the same way.
  - `NXRT_VK_TEST_PIPE_FAULT=<key>` makes such a crash for pipelines with that shader (tests).
- **Until then:** `NXRT_VK_SKIP=b1f4b2d7c3863ab0` leaves that shader's draws out before its pipeline is made.

**Checked here** (RTX 4070 Ti): `t_vk_guard` 33 of 33, with validation on and no validation errors. The new checks:
- **The caps:** they limit both test shaders' comparisons without a size query. Sane counts are unchanged; a count of 0xffffffff or 1.0f walks
  16384 items.
- **The catch:** through the real `vksh_make` and `vkp_compute`, a crash staged while making 634565ab's guarded pipeline is caught, and the
  pipeline is made from the module as translated. A shader with nothing to fall back to is left out, and stays out.
- **AMD's buffer markers:** NVIDIA's driver has the extension too. A marker's number comes back as started and finished.
- **In the game** (2.0.1, the play save, 130 s) with `NXRT_VK_LOOP_GUARD=cap` and `NXRT_VK_CHECKPOINTS=markers`:
  - the caps applied to the real `560a0262` (2 + 2 comparisons), `634565ab` (2) and `2cb5df5c`;
  - the markers moved with every stats line;
  - a crash staged while making the beam effect's pipeline (vertex `47ce9f1d88b72b56`, fragment `7760d1202244455a`) was caught at 1.4 s, and the game
    ran on to 7800 frames at 60 fps.
- **Not run:** on an AMD GPU. The full ctest passes (61 of 61).

## The 1.3.5 kit check: the guards on the kit's own shaders

The kit's test run (2.0.1, RTX 4070 Ti, the shaders the kit translated itself) logged `2cb5df5c`'s cap as "could not be capped". The guards had only been
checked on our own shader cache, and the kit's translation differs from it in two ways:

- **Ids.** The cap named its variable by id (`%2154`). In the kit's module that id is another variable; the count is `%2072`. `vkspv_cap_bound` now finds
  the variable by its shape (`var` 0): the one uint function variable that is stored once and is the right operand of an unsigned `>=`. In both
  translations exactly one variable has that shape.
- **Tables.** The kit lays NVN's texel buffers out as texel buffers (Dim Buffer), ours as 2D textures. `634565ab`'s and `560a0262`'s table-size guards
  queried the size with `OpImageQuerySizeLod`, which is not valid on a texel buffer (spirv-val: "Image 'Dim' must be 1D, 2D, 3D or Cube").
  - NVIDIA's driver took those modules. What the query then returned is not known, so the light lists' limit may have been wrong on every kit-built
    install since these guards shipped.
  - A texel buffer table is now sized by `OpImageQuerySize`, its texel count. It accepts both declarations: the kit's sampled image over a Buffer image,
    and glslang's plain Buffer image.

**The check that would have caught it** (`nxrt.vk_guard_corpus_nv` and `nxrt.vk_guard_corpus_other`): every guarded shader of whole shader caches goes
through `vksh_make`. That means every rewrite in the runtime's order, first as NVIDIA gets them (ballot rewrite, table-size guards), then as other GPUs get
them (caps). Each is made into a pipeline with validation on.
- The test checks the comparisons each guard limited (634565ab 2, 560a0262 4, 2cb5df5c 1) and that the guarded module adds no validation errors.
- `NXRT_TEST_CORPUS` lists the caches, '|'-separated. By default these are our cache and the kit test's `m7-test/build201/windows/shaders`, and missing
  ones are skipped.
- Run it on a kit-built cache before a release.

**Checked:**
- **`t_vk_guard`, 40 checks**, including a texel buffer table walked on the GPU (`test_guard_b.comp`). A sane count is unchanged; 0xffffffff walks the
  buffer's 40 texels; 1.0f read as a count walks its 70000 texels. The GPU part now runs only when every edit applied: an edit that did not apply would
  give the GPU an endless loop.
- **Both corpus tests, 52 checks each:** both caches, all three shaders guarded, no validation errors.
- **spirv-val:** passes on the kit's three guarded modules.
- **In the game,** with a copy of the kit's shaders and its seed list: 560a0262 2 + 2, 634565ab 2, and 2cb5df5c "kept to at most 16384 entries".
