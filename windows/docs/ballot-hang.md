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
- **The report:** on NVIDIA, a lost device now prints the first eight words of each dispatch's first constant buffer as guest memory held them when it
  was recorded, beside the checkpoint that names the dispatch.

**64-lane GPUs.** NVN's shaders are written for 32-lane warps: ballots read `.x`, the shuffle helpers clamp to lane 31, votes span 32. AMD's GPUs run
64-lane subgroups by default (santriman's Radeon 890M on 1.2.6: "size 64"), Intel's 8 to 32. Of the cache's shaders 49 use subgroup operations (15
compute, 34 fragment); on 64 lanes a warp-32 counter sees only lanes 0-31 in its ballot and gives lanes 32-63 slots past the space it reserved, and
`9506b27c35b91e67` (a masked-sum counter) runs every frame in the field and in conversations.
- **Subgroup size control** (Vulkan 1.3), **opt-in in 1.3.1**: with `NXRT_VK_SUBGROUP32=1`, where the driver lets a pipeline require 32
  (`requiredSubgroupSizeStages`), every shader that uses subgroup operations is made with 32-lane subgroups, and compute work groups that are whole
  32-lane rows get full subgroups. One log line says so (or, in red, that the driver cannot). Unset or `0`, the default until an AMD or Intel GPU has
  run it, leaves the driver's size, as in 1.3.0; the log line then names the switch. NVIDIA (always 32) is unchanged either way.
- The ballot rewrite stays NVIDIA's only.

**Checked here** (RTX 4070 Ti): `t_vk_guard` 11 of 11, 12 with the real shader through `NXRT_TEST_SPV` (a sane count unchanged, a count of a billion walks the table's items at once, one and two
rows, the edited module valid, the guard on by default only for NVIDIA; on the real `634565ab` two comparisons limited, spirv-val passes, decompiled only those two lines differ);
`t_vk_ballot` 16 of 16; the 2.0.1 route with validation on, `NXRT_VK_SUBGROUP32=1` and the guard: 0 errors, pictures right. **Not checked:** an AMD or
Intel GPU (only santriman on 1.3.1, or a Steam Deck tester), a real lost device's new report line.
