# The replay thread: our Vulkan recording off the game's render thread

On by default with the Vulkan backend. `NXRT_REPLAY_THREAD=0` replays on the game's own thread as before; `=1` forces it on (the null backend
too). The log says which at the first NVN call: `[nvn] submissions are replayed on a thread of their own`.

**Why.** `nvnQueueSubmitCommands` used to replay the whole submission on the game's render thread before returning (`gfx_sub_begin` →
`vk_replay.c` → `gfx_sub_commit`). On a Steam Deck core that is about 16–18 ms a frame, on top of the game's own 10–17 ms on the same thread: about
30 fps. On two threads the frame takes about the longer of the two instead of their sum. The plan and numbers: deck-60fps-plan.md and
replay-thread-brief.md in the session notes.

## How it works (`hle/hle_nvn.c`, "The replay thread")

1. **Handing over.**
   - A submit copies its command handles, its queue's serial and origin into a job. Its syncs are marked (below), and the call returns.
   - A present (`nvnQueuePresentTexture`) is a job too, behind the submissions before it.
   - One thread runs the jobs in order, exactly as the game's thread did: `gfx_sub_begin`, the records, `gfx_sub_commit`; then `gfx_present`.
   - The queue holds 64 jobs; a game further ahead waits at its next submit.
2. **The command memory stays valid.** NVN lets the game reuse it only after the submission has completed on the GPU, which is after its replay.
   Guest memory (uploads, texture and sampler pools) is read at replay, as the GPU reads it at execution on the console.
3. **Syncs.**
   - A sync fenced in a submission not replayed yet holds that submission's sequence number (`fenced` 3; 4 for an acquire's).
   - `nvnSyncWait` waits for that job's replay, then for its backend fence value (`rt_value`).
   - `nvnQueueFenceSync` takes the last submission handed over.
   - Fences recorded in command buffers (`OP_FENCE_SYNC`) are found at submit by walking the submitted lists. This walk happens only once a game has
     recorded a fence at all.
   - The backends no longer set a fenced sync at commit (`nvn_replay_async`).
4. **Waits for the replay.**
   - `nvnWindowAcquireTexture` waits for its texture's last present to be handed over: its ticket is then known.
   - These drain the queue first, because the replays before them may still read what they change:
     - `nvnQueueFinish`;
     - `nvnTextureReadTexels`;
     - the finalizes of textures, pools, programs and queues;
     - a texture or program set up again in place;
     - `nvnMemoryPoolMapVirtual`.
   - Events, counters and write-backs already apply at GPU completion (the completion thread), so they are unchanged.
5. **Owner checks.** The replay thread is the recording thread (`vks_begin` takes its thread id). Uploads from other threads keep their one-shot
   command buffers. The texture and memory tables were already shared with the present thread under their locks.
6. **Timing.** `submit+present` in the `[nvn] frame pacing` line is now the game thread's hand-over. The same line adds `replay thread N ms a frame`,
   the thread's own time, and says when the game waited on a full queue.

## Measured

docs/cpu-work.md's run: a copy of the save, the clock fixed, losttest's presses into the field, 7,800 presents, a warm cache; the means of
frames 4,500–7,800. RTX 4070 Ti, i9-13900KF. "Game thread" is `submit+present` (its hand-over, or the whole replay with the thread off).

| Run | Thread | Frames/s | Game thread | Replay thread |
|---|---|---|---|---|
| 2.0.1, all cores | on | 59.9 | 0.03 ms | 4.19 ms |
| | off | 59.9 | 4.53 ms | – |
| 1.0.5, all cores | on | 59.9 | 0.03 ms | 4.31 ms |
| | off | 59.9 | 4.51 ms | – |
| 2.0.1, 4 E-cores | on | 59.1 | 0.05 ms | 8.83 ms |
| | off | 59.6 | 8.68 ms | – |
| 2.0.1, 2 E-cores | on | **40.3** | 0.27 ms | 9.16 ms |
| | off | 34.7 | 12.40 ms | – |

- With cores to spare, both hold 60 and the replay moves off the game's thread.
- 4 E-cores are not a Steam Deck: their replay takes about 8.8 ms a frame, the Deck's 16–18 ms. Both settings stay near 60 there.
  - The thread-on run was the one that reached new game content: a first-use shader compile held one frame for 494 ms. The others never got
    there.
- On 2 E-cores the game is CPU-bound. There the thread gives 16% more frames: 95th-percentile frame interval 39 ms against 48 ms; vsyncs without
  a new frame 1,645 against 2,420.
- The game never waited on a full queue in any run.
- The Deck decides whether it holds 60.

**Checks** (thread on):
- Vulkan validation:
  - 0 errors at 1x (2.0.1), switching to 3840x2160 from the in-game menu while playing;
  - 0 errors at 2x (1.0.5), switching to the game's own 1920x1080.
  - The 12 warnings in each come from a screen recorder's layer and an unused fragment output; none is about threads.
- Screenshots before and after each switch are correct.
- `ctest`: all tests, and `nxrt.nvn_thread` / `nxrt.nvn_rec_thread` (the NVN tests with the thread forced on, null backend).
- Not run here: co-op in game (its tests pass), Linux and the Deck.
