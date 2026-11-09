// Submissions, batching and the completion thread (vk_submit.h; docs/m5-vulkan.md §3.4). Every queue submission that signals the GPU timeline goes
// through here, so timeline values reach the queue in increasing order: a value is assigned at commit, and committed submissions are handed to the
// queue in commit order. The ordering rule of the NVN layer (nvn_mtl.h, gfx_backend.h): every visible effect of GPU completion happens only after
// the GPU has actually completed the submission.
#include "vk_submit.h"
#include "nvn_dirty.h"
#include <process.h>
#include <stdlib.h>
#include <string.h>

enum { NSLOT = 32, AGE_FLUSH_NS = 2000000 };
enum { S_FREE, S_RECORDING, S_PENDING, S_SUBMITTED };
typedef struct { int kind; volatile uint32_t *val; uint32_t mode, v; uint64_t addr; uint32_t type; void (*fn)(void *); void *arg; } post_t;
typedef struct {
    int state;
    VkCommandPool pool; VkCommandBuffer up, main;
    int up_used;
    uint64_t value, t_commit;
    post_t *posts; size_t nposts, cap;
} slot_t;

static slot_t g_slot[NSLOT];
static int g_order[NSLOT], g_norder;        // committed slots (pending, then submitted), in value order
static int g_cur = -1;                      // the NVN submission being recorded
static DWORD g_cur_tid;                     // the thread recording it (M6 phase B: another thread's uploads go to a one-shot of its own, not into this slot)
static SRWLOCK g_lk = SRWLOCK_INIT;         // slots, order, value assignment
static CONDITION_VARIABLE g_cv = CONDITION_VARIABLE_INIT;   // a slot freed / a value completed
static HANDLE g_thread; static _Atomic int g_stop;
static int g_batch = 4;                     // NXRT_VK_BATCH: committed submissions held back before one queue submit
static _Atomic uint64_t g_subs, g_qsubs, g_empty, g_posted;
#include "nvn_timestamp.h"   // gfx/: NVN counter records count 614.4 MHz ticks of the guest clock
uint64_t guest_ns(void);

static void slot_init(slot_t *s) {
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, g_vk.qfam};
    vkCreateCommandPool(g_vk.dev, &pci, NULL, &s->pool);
    VkCommandBuffer cbs[2];
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, s->pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
    vkAllocateCommandBuffers(g_vk.dev, &ai, cbs);
    s->up = cbs[0]; s->main = cbs[1];
}

// A free slot, reset and begun; waits (flushing first) while every slot is in flight. Caller holds g_lk
static int slot_take(void) {
    for (;;) {
        for (int i = 0; i < NSLOT; i++) if (g_slot[i].state == S_FREE) {
            slot_t *s = &g_slot[i];
            s->state = S_RECORDING; s->up_used = 0; s->value = 0; s->nposts = 0;
            vkResetCommandPool(g_vk.dev, s->pool, 0);
            VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
            vkBeginCommandBuffer(s->up, &bi); vkBeginCommandBuffer(s->main, &bi);
            return i;
        }
        ReleaseSRWLockExclusive(&g_lk); vks_flush(); AcquireSRWLockExclusive(&g_lk);
        SleepConditionVariableSRW(&g_cv, &g_lk, 50, 0);
    }
}

// Hands every pending slot to the queue in one call. Caller holds g_lk
static void flush_locked(void) {
    VkSubmitInfo2 si[NSLOT]; VkCommandBufferSubmitInfo cbi[NSLOT][2]; VkSemaphoreSubmitInfo sig[NSLOT]; int n = 0; uint64_t last = 0;
    for (int k = 0; k < g_norder; k++) {
        slot_t *s = &g_slot[g_order[k]];
        if (s->state != S_PENDING) continue;
        int nc = 0;
        if (s->up_used) cbi[n][nc++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, NULL, s->up, 0};
        cbi[n][nc++] = (VkCommandBufferSubmitInfo){VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, NULL, s->main, 0};
        sig[n] = (VkSemaphoreSubmitInfo){VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, NULL, g_vk.tl, s->value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
        si[n] = (VkSubmitInfo2){VK_STRUCTURE_TYPE_SUBMIT_INFO_2, NULL, 0, 0, NULL, (uint32_t)nc, cbi[n], 1, &sig[n]};
        s->state = S_SUBMITTED; last = s->value; n++;
    }
    if (!n) return;
    AcquireSRWLockExclusive(&g_vk.qlock);
    VkResult r = vkQueueSubmit2(g_vk.q, (uint32_t)n, si, VK_NULL_HANDLE);
    ReleaseSRWLockExclusive(&g_vk.qlock);
    if (r != VK_SUCCESS) { VKRED("vkQueueSubmit2 failed (%d) for %d submissions up to %llu", r, n, (unsigned long long)last); vk_lost("a submit", r); }
    atomic_store(&g_vk.tl_submitted, last);
    atomic_fetch_add(&g_qsubs, 1);
}
void vks_flush(void) { AcquireSRWLockExclusive(&g_lk); flush_locked(); ReleaseSRWLockExclusive(&g_lk); }

static void post_run(post_t *p) {
    if (p->kind == 1) { if (p->mode == 1) *p->val += p->v; else *p->val = p->v; }
    else if (p->kind == 2) {   // NVN's counter record: the counter (a timestamp's own value for the timestamp counters, 0 for the rest until the replay has queries), then the time
        uint64_t t = nvn_timestamp_from_ns(guest_ns()), rec[2] = {p->type == 0 || p->type == 0x10 ? t : 0, t};
        void dm_guest_write_prepare(void *addr, size_t len);
        dm_guest_write_prepare((void *)(uintptr_t)p->addr, sizeof rec);
        memcpy((void *)(uintptr_t)p->addr, rec, sizeof rec);
    }
    else if (p->kind == 3) p->fn(p->arg);
}

// The completion thread: waits for the oldest submitted value, applies its effects in order, frees its slot; flushes submissions held back too long
static unsigned __stdcall completion_main(void *arg) {
    (void)arg;
    SetThreadDescription(GetCurrentThread(), L"vk completion");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    while (!atomic_load(&g_stop)) {
        AcquireSRWLockExclusive(&g_lk);
        uint64_t want = 0, age_from = 0;
        for (int k = 0; k < g_norder; k++) {
            slot_t *s = &g_slot[g_order[k]];
            if (s->state == S_SUBMITTED && !want) want = s->value;
            if (s->state == S_PENDING && !age_from) age_from = s->t_commit;
        }
        if (age_from && now_ns() - age_from > AGE_FLUSH_NS) flush_locked();
        ReleaseSRWLockExclusive(&g_lk);
        if (!want) { vk_retire_poll(); Sleep(1); continue; }
        if (!vk_tl_wait(want, 1000000)) continue;
        uint64_t done = vk_tl_value();
        for (;;) {   // apply in value order; effects run outside the lock (an event write can wake a guest thread that submits)
            AcquireSRWLockExclusive(&g_lk);
            int idx = g_norder && g_slot[g_order[0]].state == S_SUBMITTED && g_slot[g_order[0]].value <= done ? g_order[0] : -1;
            ReleaseSRWLockExclusive(&g_lk);
            if (idx < 0) break;
            slot_t *s = &g_slot[idx];
            for (size_t i = 0; i < s->nposts; i++) post_run(&s->posts[i]);
            AcquireSRWLockExclusive(&g_lk);
            atomic_store(&g_vk.tl_done, s->value);
            s->state = S_FREE; memmove(g_order, g_order + 1, (size_t)(--g_norder) * sizeof *g_order);
            WakeAllConditionVariable(&g_cv);
            ReleaseSRWLockExclusive(&g_lk);
        }
        vk_retire_poll();
    }
    return 0;
}

int vks_init(void) {
    const char *e = getenv("NXRT_VK_BATCH"); if (e && atoi(e) > 0) g_batch = atoi(e) < NSLOT / 2 ? atoi(e) : NSLOT / 2;
    for (int i = 0; i < NSLOT; i++) slot_init(&g_slot[i]);
    atomic_store(&g_stop, 0);
    g_thread = (HANDLE)_beginthreadex(NULL, 0, completion_main, NULL, 0, NULL);
    return g_thread ? 0 : -1;
}

void vks_shutdown(void) {
    if (!g_thread) return;
    vks_finish();
    atomic_store(&g_stop, 1); WaitForSingleObject(g_thread, 5000); CloseHandle(g_thread); g_thread = NULL;
    for (int i = 0; i < NSLOT; i++) { if (g_slot[i].pool) vkDestroyCommandPool(g_vk.dev, g_slot[i].pool, NULL); free(g_slot[i].posts); memset(&g_slot[i], 0, sizeof g_slot[i]); }
    g_norder = 0; g_cur = -1;
}

void vks_begin(void) {
    nvn_dirty_submission();
    AcquireSRWLockExclusive(&g_lk);
    if (g_cur >= 0) VKRED("submission begun while another is being recorded");
    else { g_cur = slot_take(); g_cur_tid = GetCurrentThreadId(); }
    ReleaseSRWLockExclusive(&g_lk);
}
static int cur_here(void) { return g_cur >= 0 && GetCurrentThreadId() == g_cur_tid; }
int vks_recording(void) { return g_cur >= 0; }
VkCommandBuffer vks_cb(void) { return g_cur >= 0 ? g_slot[g_cur].main : VK_NULL_HANDLE; }
VkCommandBuffer vks_upload_cb(void) { if (!cur_here()) return VK_NULL_HANDLE; g_slot[g_cur].up_used = 1; return g_slot[g_cur].up; }
uint64_t vks_recording_value(void) { return g_cur >= 0 ? atomic_load(&g_vk.tl_assigned) + 1 : 0; }

static void post_add(post_t p) {
    if (g_cur < 0) { VKRED("completion effect posted outside a submission (kind %d): applied now", p.kind); post_run(&p); return; }
    slot_t *s = &g_slot[g_cur];
    if (s->nposts == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->posts = realloc(s->posts, s->cap * sizeof *s->posts); }
    s->posts[s->nposts++] = p; atomic_fetch_add(&g_posted, 1);
}
void vks_post_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { post_add((post_t){1, val, mode, v, 0, 0, NULL, NULL}); }
void vks_post_counter(uint64_t addr, uint32_t type) { post_add((post_t){2, NULL, 0, 0, addr, type, NULL, NULL}); }
void vks_post_fn(void (*fn)(void *), void *arg) { post_add((post_t){3, NULL, 0, 0, 0, 0, fn, arg}); }

// Commits slot i: ends its command buffers (a barrier ends the uploads), assigns the next timeline value, queues it in order. Caller holds g_lk
static uint64_t commit_locked(int i) {
    slot_t *s = &g_slot[i];
    if (s->up_used) vk_mem_barrier(s->up, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    vkEndCommandBuffer(s->up); vkEndCommandBuffer(s->main);
    s->value = atomic_fetch_add(&g_vk.tl_assigned, 1) + 1;
    s->t_commit = now_ns(); s->state = S_PENDING;
    void vkm_committed(int owner, uint64_t value); vkm_committed(i, s->value);   // staging the slot used lives until this value
    g_order[g_norder++] = i;
    atomic_fetch_add(&g_subs, 1);
    return s->value;
}
uint64_t vks_commit(void) {
    AcquireSRWLockExclusive(&g_lk);
    if (g_cur < 0) { ReleaseSRWLockExclusive(&g_lk); VKRED("commit without a submission"); return atomic_load(&g_vk.tl_assigned); }
    if (!g_slot[g_cur].up_used) atomic_fetch_add(&g_empty, 1);
    uint64_t v = commit_locked(g_cur); g_cur = -1;
    int pend = 0; for (int k = 0; k < g_norder; k++) pend += g_slot[g_order[k]].state == S_PENDING;
    if (pend >= g_batch) flush_locked();
    ReleaseSRWLockExclusive(&g_lk);
    return v;
}

int vks_wait(uint64_t fence, uint64_t timeout_ns) {
    if (!fence || atomic_load(&g_vk.tl_done) >= fence) return 1;
    if (atomic_load(&g_vk.tl_submitted) < fence) vks_flush();
    if (!timeout_ns) return atomic_load(&g_vk.tl_done) >= fence;
    uint64_t t0 = now_ns();
    AcquireSRWLockExclusive(&g_lk);
    while (atomic_load(&g_vk.tl_done) < fence) {
        uint64_t el = now_ns() - t0;
        if (timeout_ns != UINT64_MAX && el >= timeout_ns) break;
        DWORD ms = timeout_ns == UINT64_MAX ? 100 : (DWORD)((timeout_ns - el) / 1000000 + 1);
        SleepConditionVariableSRW(&g_cv, &g_lk, ms < 100 ? ms : 100, 0);
    }
    ReleaseSRWLockExclusive(&g_lk);
    return atomic_load(&g_vk.tl_done) >= fence;
}
void vks_finish(void) { vks_flush(); vks_wait(atomic_load(&g_vk.tl_assigned), UINT64_MAX); }

void vks_stats(uint64_t *subs, uint64_t *qsubs, uint64_t *empty, uint64_t *posted) {
    *subs = atomic_load(&g_subs); *qsubs = atomic_load(&g_qsubs); *empty = atomic_load(&g_empty); *posted = atomic_load(&g_posted);
}

// ---------- One-shot command buffers (bring-up, readbacks, tests) on a slot of their own, ordered with everything else ----------
static __thread int t_once = -1;
int vks_owner(void) { return t_once >= 0 ? t_once : cur_here() ? g_cur : -1; }   // the slot this thread records into (-1 = none): staging allocations belong to it
VkCommandBuffer vk_once_begin(void) {
    AcquireSRWLockExclusive(&g_lk); t_once = slot_take(); ReleaseSRWLockExclusive(&g_lk);
    return g_slot[t_once].main;
}
int vk_once_end(VkCommandBuffer cb) {
    (void)cb;
    AcquireSRWLockExclusive(&g_lk); uint64_t v = commit_locked(t_once); t_once = -1; flush_locked(); ReleaseSRWLockExclusive(&g_lk);
    int ok = vks_wait(v, 20000000000ull);
    if (!ok) VKRED("one-shot submission %llu did not complete in 20 s", (unsigned long long)v);
    return ok ? 0 : -1;
}
