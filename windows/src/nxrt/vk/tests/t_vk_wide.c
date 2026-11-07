/* Wide view (NXRT_VK_WIDE, vk_replay.c), run with NXRT_VK_SCALE=2: a 16:9 target drawn at 256x144 whose middle half (the 16:9 part of a view twice as wide)
   is green and its sides red. With the squeeze at 0.5, a copy of the whole picture out to a buffer or into a texture at the game's size takes the middle
   part, stretched (a photo); a copy of part of it, or with the squeeze off, stays as it was. A presented texture with no full-screen draw gets bars.
   The GPU part is skipped without a device (77). */
#include "vk_test.h"
#include "vk_replay.h"

NXRT_EARLY_RESERVE()

extern float g_vkr_wide_squeeze;

static uint8_t *rec(uint8_t *p, uint32_t op, const void *payload, uint32_t n) {
    op_hdr h = {op, (8 + n + 7) & ~7u}; memcpy(p, &h, 8); memset(p + 8, 0, h.size - 8); if (n) memcpy(p + 8, payload, n); return p + h.size;
}
static void run(const uint8_t *rb) { vks_begin(); vkr_sub_begin(); vkr_exec(rb); vkr_sub_end(); vks_wait(vks_commit(), UINT64_MAX); }
static int is_px(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b) { return p[0] == r && p[1] == g && p[2] == b && p[3] == 255; }
enum { TW = 128, TH = 72 };
// columns [x0, x1) of a TW-wide picture green, the rest red
static int cols(const uint8_t *px, int stride, int x0, int x1) {
    int ok = 1;
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) ok &= x >= x0 && x < x1 ? is_px(px + y * stride + 4 * x, 0, 200, 0) : is_px(px + y * stride + 4 * x, 255, 0, 0);
    return ok;
}

int main(void) {
    crash_install();
    _putenv("NXRT_VK_SCALE=2");
    int rc = vt_up(0, 1);
    if (rc == 77) { printf("GPU part skipped (no device)\n"); return 77; }
    if (rc) return rc;
    CHECK(!vkr_init(), "replay started");

    enum { PSZ = 1 << 21 };
    uint8_t *mem = VirtualAlloc(NULL, PSZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(mem, 0, PSZ);
    vkm_pool_add(mem, PSZ);
    tex_t ta = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TH, 1, 1, 0, 0, mem + 0x000000);
    tex_t tq = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TW, 1, 1, 0, 0, mem + 0x080000);   // square: at the game's size
    vkt_tex_init(&ta); vkt_tex_init(&tq);
    vkt_image *ia = vkt_resolve(&ta), *iq = vkt_resolve(&tq);
    CHECK(ia && ia->vw == 2 * TW && ia->vh == 2 * TH, "the 16:9 target is drawn at 256x144 (%ux%u)", ia ? ia->vw : 0, ia ? ia->vh : 0);
    CHECK(iq && iq->vw == TW, "the square one stays at the game's size");

    uint8_t *rb = malloc(1 << 16), *p = rb, *dst = mem + 0x100000;
    p_set_rt rt; memset(&rt, 0, sizeof rt); rt.n = 1; rt.color[0] = &ta; p = rec(p, OP_SET_RT, &rt, sizeof rt);
    p_scissor full = {0, 0, TW, TH}; p = rec(p, OP_SCISSOR, &full, sizeof full);
    p_clear_color red = {0, 0xf, {1.0f, 0.0f, 0.0f, 1.0f}}; p = rec(p, OP_CLEAR_COLOR, &red, sizeof red);
    p_scissor mid = {TW / 4, 0, TW / 2, TH}; p = rec(p, OP_SCISSOR, &mid, sizeof mid);
    p_clear_color green = {0, 0xf, {0.0f, 200 / 255.0f, 0.0f, 1.0f}}; p = rec(p, OP_CLEAR_COLOR, &green, sizeof green);
    p = rec(p, OP_END, NULL, 0);
    run(rb);

    p_copy_b2t tb = {0}; tb.src = (uint64_t)(uintptr_t)dst; tb.tex = &ta; tb.r = (NVNcopyRegion){0, 0, 0, TW, TH, 1};
    p_copy_t2t tt = {0}; tt.src = &ta; tt.dst = &tq; tt.sr = tt.dr = (NVNcopyRegion){0, 0, 0, TW, TH, 1};
    uint8_t q[TW * TW * 4]; NVNcopyRegion qa = {0, 0, 0, TW, TH, 1};

    g_vkr_wide_squeeze = 1.0f;   // off
    p = rec(rb, OP_COPY_T2B, &tb, sizeof tb); rec(p, OP_END, NULL, 0); run(rb);
    CHECK(cols(dst, TW * 4, TW / 4, 3 * TW / 4), "squeeze off: the copy out is the whole picture");
    CHECK(vkr_wide_bars(ta.serial) == 0, "squeeze off: no bars");

    g_vkr_wide_squeeze = 0.5f;
    memset(dst, 0, TW * TH * 4);
    p = rec(rb, OP_COPY_T2B, &tb, sizeof tb); rec(p, OP_END, NULL, 0); run(rb);
    CHECK(cols(dst, TW * 4, 0, TW), "a copy of the whole picture out to a buffer is its middle part, stretched (%u,%u,%u at the left)", dst[0], dst[1], dst[2]);
    p = rec(rb, OP_COPY_T2T, &tt, sizeof tt); rec(p, OP_END, NULL, 0); run(rb);
    vkt_read_texels(&tq, 0, 0, &qa, q, TW * 4, TW * TH * 4);
    CHECK(cols(q, TW * 4, 0, TW), "so is one into a texture at the game's size");
    p_copy_b2t part = tb; part.r = (NVNcopyRegion){0, 0, 0, TW / 2, TH, 1};
    memset(dst, 0, TW * TH * 4);
    p = rec(rb, OP_COPY_T2B, &part, sizeof part); rec(p, OP_END, NULL, 0); run(rb);
    int pok = 1;
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW / 2; x++) pok &= x >= TW / 4 ? is_px(dst + y * TW * 2 + 4 * x, 0, 200, 0) : is_px(dst + y * TW * 2 + 4 * x, 255, 0, 0);
    CHECK(pok, "a copy of part of it stays as it is");
    vkr_note_presented(ta.serial);
    int first = vkr_wide_bars(ta.serial), second = vkr_wide_bars(ta.serial);
    CHECK(!first && second && !vkr_wide_bars(0), "a presented texture with nothing full screen drawn into it since its last present gets bars (%d %d)", first, second);

    g_vkr_wide_squeeze = 1.0f;
    vkt_tex_release(&ta); vkt_tex_release(&tq); vkt_drop_pool(ta.pool);
    vkm_pool_del(mem);
    free(rb);
    uint32_t verr = atomic_load(&g_vk.val_errors);
    vkr_shutdown();
    vt_down(0);
    CHECK(verr == 0, "no validation errors (%u)", verr);
    VirtualFree(mem, 0, MEM_RELEASE);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
