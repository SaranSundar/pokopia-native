/* The pool mirror and the write-protect dirty tracking (docs/m5-vulkan.md §3, §6): a pool's pages reach its device-local buffer through the staging ring;
   a guest write faults once and only that page is copied again, untouched pages are not; host writes through dm_guest_write_prepare count; a page written
   every frame turns hot (no more faults) and cools down; GPU copies, fills and inline updates land in guest memory at completion; and the cost of a write
   fault on this PC. The tracker part runs without a GPU; the mirror part is skipped (77 overall only when nothing could run). */
#include "vk_test.h"

NXRT_EARLY_RESERVE()
void dm_guest_write_prepare(void *addr, size_t len);

enum { NP = 256, PG = 4096 };

static void submit_sync_all(void) { vks_begin(); vkm_sync_all(); vks_wait(vks_commit(), UINT64_MAX); }
static int mirror_equals(uint8_t *mem, size_t n) {   // the pool's device copy, read back, against guest memory
    VkBuffer pb; VkDeviceSize po; if (!vkm_addr((uint64_t)(uintptr_t)mem, n, &pb, &po)) return 0;
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, n, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    VkBuffer b; vk_alloc m; vkCreateBuffer(g_vk.dev, &bci, NULL, &b);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, b, &mr);
    vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &m); vkBindBufferMemory(g_vk.dev, b, m.mem, m.off);
    VkCommandBuffer cb = vk_once_begin();
    VkBufferCopy c = {po, 0, n}; vkCmdCopyBuffer(cb, pb, b, 1, &c);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    vk_once_end(cb);
    size_t bad = 0; for (size_t i = 0; i < n; i++) bad += ((uint8_t *)m.map)[i] != mem[i];
    vkDestroyBuffer(g_vk.dev, b, NULL); vk_free_mem(&m);
    if (bad) printf("  mirror differs in %zu bytes\n", bad);
    return bad == 0;
}

int main(void) {
    crash_install();
    // ---- The tracker on its own (gfx/nvn_dirty.c, the recording layer's): fault once, stamp, unprotect ----
    uint8_t *mem = VirtualAlloc(NULL, NP * PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (size_t i = 0; i < NP * PG; i++) mem[i] = (uint8_t)vt_rand();
    nvn_dirty_add_range(mem, NP * PG);
    uint64_t st = nvn_dirty_now(); nvn_dirty_protect(mem, NP * PG);
    uint64_t f0, p0; nvn_dirty_stats(&f0, &p0);
    mem[5 * PG + 7] ^= 1; mem[5 * PG + 9] ^= 1;   // two writes, one page
    uint64_t f1, p1; nvn_dirty_stats(&f1, &p1);
    CHECK(f1 - f0 == 1, "one fault for two writes to one page (%llu)", (unsigned long long)(f1 - f0));
    CHECK(nvn_dirty_since(mem + 5 * PG, PG, st) && !nvn_dirty_since(mem + 6 * PG, PG, st), "only the written page is newer than the stamp");
    dm_guest_write_prepare(mem + 9 * PG, 10);
    CHECK(nvn_dirty_since(mem + 9 * PG, PG, st), "a host pre-write stamps its page");
    // the cost of one write fault (protect, write, fault, unprotect), on this PC
    uint64_t t0 = now_ns(); int nf = 2000;
    for (int i = 0; i < nf; i++) { nvn_dirty_protect(mem + (i % NP) * PG, PG); mem[(i % NP) * PG + 1]++; }
    uint64_t dt = now_ns() - t0; uint64_t f2, p2; nvn_dirty_stats(&f2, &p2);
    double us = dt / 1e3 / nf;
    printf("tracker: %d protect + write-fault rounds in %.2f ms = %.2f us each (%llu faults)\n", nf, vt_ms(dt), us, (unsigned long long)(f2 - f1));
    CHECK(f2 - f1 >= (uint64_t)nf - 1, "every round faulted");
    // a range with a hole the heap has not committed (#27: textures whose storage spans an untouched 1 MB chunk): Windows refuses the whole run, so the
    // committed parts are protected one by one and only the hole counts as written
    uint8_t *hm = VirtualAlloc(NULL, 16 * PG, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(hm, 4 * PG, MEM_COMMIT, PAGE_READWRITE); VirtualAlloc(hm + 8 * PG, 8 * PG, MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < 4 * PG; i++) hm[i] = 1;
    for (int i = 8 * PG; i < 16 * PG; i++) hm[i] = 2;
    nvn_dirty_add_range(hm, 16 * PG);
    uint64_t hs = nvn_dirty_now(); nvn_dirty_protect(hm, 16 * PG);
    CHECK(!nvn_dirty_since(hm, 4 * PG, hs) && !nvn_dirty_since(hm + 8 * PG, 8 * PG, hs), "a hole: the committed parts are tracked (clean after the protect)");
    CHECK(nvn_dirty_since(hm + 4 * PG, PG, hs) && nvn_dirty_since(hm + 7 * PG, PG, hs), "a hole: its own pages count as written");
    uint64_t hf0, hp0, hf1, hp1; nvn_dirty_stats(&hf0, &hp0); hm[9 * PG + 3] = 7; nvn_dirty_stats(&hf1, &hp1);
    CHECK(hf1 - hf0 == 1 && nvn_dirty_since(hm + 9 * PG, PG, hs) && !nvn_dirty_since(hm + 10 * PG, PG, hs), "a hole: a write past it faults and only its page is newer");
    nvn_dirty_remove_range(hm); VirtualFree(hm, 0, MEM_RELEASE);

    // ---- The mirror (needs a GPU) ----
    int rc = vt_up(0, 1);
    if (rc == 77) { printf("mirror part skipped (no GPU); %s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail); return g_fail ? 1 : 0; }
    if (rc) return rc;
    vkm_pool_add(mem, NP * PG);
    vkm_stats_t a, b;
    vkm_stats(&a); submit_sync_all(); vkm_stats(&b);
    CHECK(b.up_pages - a.up_pages == NP, "first sync copies every page (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    CHECK(mirror_equals(mem, NP * PG), "the device copy equals guest memory");
    vkm_frame_tick(1);
    vkm_stats(&a); submit_sync_all(); vkm_stats(&b);
    CHECK(b.up_pages == a.up_pages, "nothing written: nothing copied (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    uint64_t fa, pa, fb, pb_; nvn_dirty_stats(&fa, &pa);
    mem[7 * PG + 100] = 0x5a; mem[30 * PG] = 0xa5; mem[30 * PG + 4095] = 0x11; mem[31 * PG + 1] = 0x22;
    nvn_dirty_stats(&fb, &pb_);
    vkm_frame_tick(2);
    vkm_stats(&a); submit_sync_all(); vkm_stats(&b);
    CHECK(fb - fa == 3, "three pages written, three faults (%llu)", (unsigned long long)(fb - fa));
    CHECK(b.up_pages - a.up_pages == 3, "only the three written pages are copied again (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    CHECK(b.up_runs - a.up_runs == 2, "pages 30 and 31 go in one run (%llu runs)", (unsigned long long)(b.up_runs - a.up_runs));
    CHECK(mirror_equals(mem, NP * PG), "the device copy follows the writes");
    // a page written every frame turns hot: copied at every use, no more faults
    for (int f = 3; f < 9; f++) { mem[40 * PG + f] = (uint8_t)f; vkm_frame_tick((uint64_t)f); submit_sync_all(); }
    vkm_stats(&a);
    CHECK(a.hot_pages >= 1, "a page written in consecutive frames is hot (%llu)", (unsigned long long)a.hot_pages);
    nvn_dirty_stats(&fa, &pa); mem[40 * PG + 200] = 1; mem[40 * PG + 201] = 2; nvn_dirty_stats(&fb, &pb_);
    CHECK(fb == fa, "a hot page is left writable (%llu faults)", (unsigned long long)(fb - fa));
    vkm_frame_tick(9); submit_sync_all();
    CHECK(mirror_equals(mem, NP * PG), "a hot page is copied at every use");
    vkm_frame_tick(240); submit_sync_all();   // cooling: protected again at its next copy
    nvn_dirty_stats(&fa, &pa); mem[40 * PG + 300] = 3; nvn_dirty_stats(&fb, &pb_);
    CHECK(fb - fa == 1, "a cooled page faults again (%llu)", (unsigned long long)(fb - fa));
    // vkm_use: only the range asked for
    mem[100 * PG] = 9; mem[200 * PG] = 9;
    vkm_stats(&a);
    vks_begin(); VkBuffer bb; VkDeviceSize bo; CHECK(vkm_use((uint64_t)(uintptr_t)(mem + 100 * PG), 64, &bb, &bo) && bo == 100 * PG, "vkm_use resolves the pool"); vks_wait(vks_commit(), UINT64_MAX);
    vkm_stats(&b);
    CHECK(b.up_pages - a.up_pages == 1, "vkm_use copies the written page in its range only (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    // GPU writes reach guest memory when the submission completes: a copy, a fill, an inline update
    uint8_t expect[PG]; memcpy(expect, mem + 10 * PG, PG);
    vks_begin();
    VkBuffer sb_, db_; VkDeviceSize so_, do_;
    vkm_use((uint64_t)(uintptr_t)(mem + 10 * PG), PG, &sb_, &so_); vkm_use((uint64_t)(uintptr_t)(mem + 50 * PG), PG, &db_, &do_);
    VkBufferCopy c = {so_, do_, PG}; vkCmdCopyBuffer(vks_cb(), sb_, db_, 1, &c); vkm_gpu_wrote((uint64_t)(uintptr_t)(mem + 50 * PG), PG);
    vkm_use((uint64_t)(uintptr_t)(mem + 60 * PG), 256, &db_, &do_); vkCmdFillBuffer(vks_cb(), db_, do_, 256, 0xdeadbeef); vkm_cpu_also((uint64_t)(uintptr_t)(mem + 60 * PG), 256, NULL, 1, 0xdeadbeef);
    uint8_t upd[64]; for (int i = 0; i < 64; i++) upd[i] = (uint8_t)(i * 3);
    vkm_use((uint64_t)(uintptr_t)(mem + 70 * PG + 128), 64, &db_, &do_); vkCmdUpdateBuffer(vks_cb(), db_, do_, 64, upd); vkm_cpu_also((uint64_t)(uintptr_t)(mem + 70 * PG + 128), 64, upd, 0, 0);
    uint64_t v = vks_commit();
    vks_wait(v, UINT64_MAX);
    uint32_t w0; memcpy(&w0, mem + 60 * PG, 4);
    CHECK(!memcmp(mem + 50 * PG, expect, PG), "a GPU copy lands in guest memory at completion");
    CHECK(w0 == 0xdeadbeef, "a GPU fill lands in guest memory (%08x)", w0);
    CHECK(!memcmp(mem + 70 * PG + 128, upd, 64), "an inline update lands in guest memory");
    vkm_frame_tick(241); submit_sync_all();
    CHECK(mirror_equals(mem, NP * PG), "after the write backs the device copy and guest memory agree");
    // #27: a page wholly written back by the GPU is not copied up again (the device copy holds those bytes); a CPU write after it is seen as before
    vkm_stats(&a);
    vks_begin(); vkm_use((uint64_t)(uintptr_t)(mem + 50 * PG), PG, &db_, &do_); vks_wait(vks_commit(), UINT64_MAX);
    vkm_stats(&b);
    CHECK(b.up_pages == a.up_pages, "a page the GPU wrote whole: not copied up again (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    nvn_dirty_stats(&fa, &pa); mem[50 * PG + 17] ^= 0xff; nvn_dirty_stats(&fb, &pb_);
    vkm_stats(&a);
    vks_begin(); vkm_use((uint64_t)(uintptr_t)(mem + 50 * PG), PG, &db_, &do_); vks_wait(vks_commit(), UINT64_MAX);
    vkm_stats(&b);
    CHECK(fb - fa == 1 && b.up_pages - a.up_pages == 1, "it is protected again: a CPU write faults (%llu) and the page is copied (%llu)",
          (unsigned long long)(fb - fa), (unsigned long long)(b.up_pages - a.up_pages));
    CHECK(mirror_equals(mem, NP * PG), "the device copy follows that write");
    // #27: a hot page is copied once per submission, however many uses it has there, and again in the next one (a write between them is seen)
    for (int f = 242; f < 248; f++) { mem[41 * PG + f] = (uint8_t)f; vkm_frame_tick((uint64_t)f); submit_sync_all(); }
    vkm_stats(&a); CHECK(a.hot_pages >= 1, "page 41 is hot (%llu)", (unsigned long long)a.hot_pages);
    mem[41 * PG + 500] = 0x77;
    vks_begin();
    for (int u = 0; u < 5; u++) vkm_use((uint64_t)(uintptr_t)(mem + 41 * PG), PG, &db_, &do_);
    vks_wait(vks_commit(), UINT64_MAX);
    vkm_stats(&b);
    CHECK(b.up_pages - a.up_pages == 1, "a hot page used five times in one submission: copied once (%llu)", (unsigned long long)(b.up_pages - a.up_pages));
    mem[41 * PG + 501] = 0x78;
    vkm_stats(&a); vks_begin(); vkm_use((uint64_t)(uintptr_t)(mem + 41 * PG), PG, &db_, &do_); vks_wait(vks_commit(), UINT64_MAX); vkm_stats(&b);
    CHECK(b.up_pages - a.up_pages == 1 && mirror_equals(mem, NP * PG), "the next submission copies it again, with the write since");
    // #27: allocations past the staging ring share host blocks within their submission instead of a buffer each
    vkm_stats(&a);
    vks_begin();
    VkDeviceSize last_off = 0; VkBuffer last_buf = VK_NULL_HANDLE; int same = 1, n_ovf = 0, ring_mb = 0;
    for (int k = 0; k < 80; k++) {   // 80 MB in 1 MB pieces: the 64 MB ring first, then the rest past it
        VkBuffer sbuf; VkDeviceSize soff; uint8_t *sp = vkm_staging(1 << 20, 16, &sbuf, &soff);
        if (!sp) { same = 0; break; }
        memset(sp, k, 1 << 20);
        vkm_stats(&b);
        if (b.staging_overflow > a.staging_overflow + (uint64_t)n_ovf) {   // this one went past the ring
            if (n_ovf && (sbuf != last_buf || soff < last_off + (1 << 20))) same = 0;
            last_buf = sbuf; last_off = soff; n_ovf++;
        } else ring_mb++;
    }
    vks_wait(vks_commit(), UINT64_MAX);
    vkm_stats(&b);
    CHECK(n_ovf >= 8 && same && b.staging_overflow_buffers - a.staging_overflow_buffers == 1,
          "%d MB past the %d MB in the ring went into %llu host buffer(s), one after another", n_ovf, ring_mb,
          (unsigned long long)(b.staging_overflow_buffers - a.staging_overflow_buffers));

    vkm_stats_t s; vkm_stats(&s);
    printf("mirror: %llu pages copied in %llu runs (%.2f ms CPU), staging %llu KB, %llu hot pages, %.1f KB written back; pool backed %.1f MB of %.1f MB (%s)\n",
           (unsigned long long)s.up_pages, (unsigned long long)s.up_runs, vt_ms(s.up_ns), (unsigned long long)(s.staging_bytes >> 10), (unsigned long long)s.hot_pages,
           s.writeback_bytes / 1024.0, s.bound_bytes / 1048576.0, s.pool_bytes / 1048576.0, s.sparse_pools ? "sparse" : "fully backed");
    vkm_pool_del(mem);
    nvn_dirty_remove_range(mem);
    uint32_t verr = atomic_load(&g_vk.val_errors);
    vt_down(0);
    CHECK(verr == 0, "no validation errors (%u)", verr);
    VirtualFree(mem, 0, MEM_RELEASE);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
