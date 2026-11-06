# The NVIDIA hang after early returns (#12, #21, #22, D-1)

Status, 2026-11-06: `vkspv_ballot_per_lane` (`src/nxrt/vk/vk_shader.c`) rewrites every warp-aggregated counter of the shape below in the 2.0.1 shader
cache: 12 ballots in 11 shaders. Three counters of other shapes are left as translated (below). Local on `gfx`.

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
predicate). `NXRT_VK_BALLOT_FIX=0` leaves every ballot as translated. The log names each shader rewritten.

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
