/* F-5, render targets drawn larger (docs/f5-resolution.md), run with NXRT_VK_SCALE=2 (the test's environment): which images are scaled; a 128x64 colour target
   drawn at 256x128 with a clear's scissor mapped into its pixels; read at the game's size (ReadTexels' path) and at its own; a buffer copied into it (blitted up)
   and out of it (blitted down); a copy into a square target at the game's size (a blit); one taken back to the game's size keeps its content; the render scale
   entry of a shader's textures (the translator's order) and whether a module reads them. The GPU part is skipped without a device (77). */
#include "vk_test.h"
#include "vk_shader.h"
#include "vk_replay.h"
#include "test_bind_frag.h"

NXRT_EARLY_RESERVE()

static uint8_t *rec(uint8_t *p, uint32_t op, const void *payload, uint32_t n) {
    op_hdr h = {op, (8 + n + 7) & ~7u}; memcpy(p, &h, 8); memset(p + 8, 0, h.size - 8); if (n) memcpy(p + 8, payload, n); return p + h.size;
}
static void run(const uint8_t *rb) { vks_begin(); vkr_sub_begin(); vkr_exec(rb); vkr_sub_end(); vks_wait(vks_commit(), UINT64_MAX); }
static int is_px(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return p[0] == r && p[1] == g && p[2] == b && p[3] == a; }

int main(void) {
    crash_install();
    _putenv("NXRT_VK_SCALE=2");
    int rc = vt_up(0, 1);
    if (rc == 77) { printf("GPU part skipped (no device)\n"); return 77; }
    if (rc) return rc;
    CHECK(!vkr_init(), "replay started");
    CHECK(vkt_scale() == 2.0f, "resolution x2 from NXRT_VK_SCALE");

    enum { PSZ = 1 << 21, TW = 128, TH = 64 };
    uint8_t *mem = VirtualAlloc(NULL, PSZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(mem, 0, PSZ);
    vkm_pool_add(mem, PSZ);
    tex_t ta = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TH, 1, 1, 0, 0, mem + 0x000000);
    tex_t tq = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TW, 1, 1, 0, 0, mem + 0x080000);   // square: at the game's size
    tex_t tsm = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, 32, 16, 1, 1, 0, 0, mem + 0x100000);   // small: at the game's size
    tex_t tu = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TH, 1, 1, 0, 0, mem + 0x180000);
    vkt_tex_init(&ta); vkt_tex_init(&tq); vkt_tex_init(&tsm); vkt_tex_init(&tu);
    vkt_image *ia = vkt_resolve(&ta), *iq = vkt_resolve(&tq), *ism = vkt_resolve(&tsm), *iu = vkt_resolve(&tu);
    CHECK(ia && ia->vw == 2 * TW && ia->vh == 2 * TH && ia->w == TW && ia->scale == 2.0f, "a 128x64 render target is drawn at 256x128 (%ux%u)", ia ? ia->vw : 0, ia ? ia->vh : 0);
    CHECK(iq && iq->vw == TW && iq->noscale_why && !strcmp(iq->noscale_why, "square"), "a square one stays at the game's size (%s)", iq && iq->noscale_why ? iq->noscale_why : "-");
    CHECK(ism && ism->vw == 32 && ism->noscale_why, "a 32x16 one stays at the game's size");
    CHECK(vkt_sx(ia, 0, 16) == 32 && vkt_sy(ia, 0, 64) == 128 && vkt_sx(ia, 1, 32) == 64 && vkt_sx(iq, 0, 17) == 17, "coordinates map into the image's own pixels");

    // cleared to (10, 20, 30, 255), then a clear of the scissor 16,8 32x16 to (255, 0, 128, 255)
    uint8_t *rb = malloc(1 << 16), *p = rb;
    p_set_rt rt; memset(&rt, 0, sizeof rt); rt.n = 1; rt.color[0] = &ta; p = rec(p, OP_SET_RT, &rt, sizeof rt);
    p_scissor full = {0, 0, TW, TH}; p = rec(p, OP_SCISSOR, &full, sizeof full);
    p_clear_color c0 = {0, 0xf, {10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 1.0f}}; p = rec(p, OP_CLEAR_COLOR, &c0, sizeof c0);
    p_scissor sc = {16, 8, 32, 16}; p = rec(p, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color c1 = {0, 0xf, {1.0f, 0.0f, 128 / 255.0f, 1.0f}}; p = rec(p, OP_CLEAR_COLOR, &c1, sizeof c1);
    p = rec(p, OP_END, NULL, 0);
    run(rb);
    uint8_t *own = malloc(4 * TW * TH * 4); NVNcopyRegion all_own = {0, 0, 0, 2 * TW, 2 * TH, 1};
    int ok = !vkt_readback_region_raw(ia, 0, 0, &all_own, own, 4 * TW * TH * 4), in = 1, out = 1;
    for (int y = 0; ok && y < 2 * TH; y++) for (int x = 0; x < 2 * TW; x++) {
        const uint8_t *c = own + 4 * (y * 2 * TW + x); int inside = x >= 32 && x < 96 && y >= 16 && y < 48;
        if (inside) in &= is_px(c, 255, 0, 128, 255); else out &= is_px(c, 10, 20, 30, 255);
    }
    CHECK(ok && in && out, "the clear's scissor covers 32,16 64x32 of the image's own pixels (inside %d, outside %d)", in, out);
    uint8_t game[TW * TH * 4]; NVNcopyRegion all = {0, 0, 0, TW, TH, 1};
    vkt_read_texels(&ta, 0, 0, &all, game, TW * 4, TW * TH * 4);
    in = out = 1;
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        const uint8_t *c = game + 4 * (y * TW + x); int inside = x >= 16 && x < 48 && y >= 8 && y < 24;
        if (inside) in &= is_px(c, 255, 0, 128, 255); else out &= is_px(c, 10, 20, 30, 255);
    }
    CHECK(in && out, "ReadTexels gives the picture at the game's size (inside %d, outside %d)", in, out);

    // a buffer copied into a corner of it (blitted up), the whole copied into the square target at the game's size (a blit), and out into a buffer (blitted down)
    uint8_t *src = mem + 0x1c0000, *dst = mem + 0x1d0000, *dsq = mem + 0x1e0000;
    for (int i = 0; i < 8 * 8; i++) { src[4 * i] = 0; src[4 * i + 1] = 200; src[4 * i + 2] = 0; src[4 * i + 3] = 255; }
    p = rb;
    p_copy_b2t cb = {0}; cb.src = (uint64_t)(uintptr_t)src; cb.tex = &ta; cb.r = (NVNcopyRegion){120, 56, 0, 8, 8, 1}; p = rec(p, OP_COPY_B2T, &cb, sizeof cb);
    p_copy_t2t tt = {0}; tt.src = &ta; tt.dst = &tq; tt.sr = tt.dr = (NVNcopyRegion){0, 0, 0, TW, TH, 1}; p = rec(p, OP_COPY_T2T, &tt, sizeof tt);
    p_copy_b2t tb = {0}; tb.src = (uint64_t)(uintptr_t)dst; tb.tex = &ta; tb.r = (NVNcopyRegion){0, 0, 0, TW, TH, 1}; p = rec(p, OP_COPY_T2B, &tb, sizeof tb);
    tb.src = (uint64_t)(uintptr_t)dsq; tb.tex = &tq; p = rec(p, OP_COPY_T2B, &tb, sizeof tb);
    p = rec(p, OP_END, NULL, 0);
    run(rb);
    int b_ok = 1, q_ok = 1;
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        const uint8_t *c = dst + 4 * (y * TW + x), *q = dsq + 4 * (y * TW + x);
        int corner = x >= 120 && y >= 56, inside = x >= 16 && x < 48 && y >= 8 && y < 24;
        if (corner) { b_ok &= is_px(c, 0, 200, 0, 255); q_ok &= is_px(q, 0, 200, 0, 255); }
        else if (inside) { b_ok &= is_px(c, 255, 0, 128, 255); q_ok &= is_px(q, 255, 0, 128, 255); }
        else { b_ok &= is_px(c, 10, 20, 30, 255); q_ok &= is_px(q, 10, 20, 30, 255); }
    }
    CHECK(b_ok, "buffer -> scaled target (blitted up) -> buffer (blitted down) keeps the picture (%u,%u,%u at the corner)", dst[4 * (63 * TW + 127)], dst[4 * (63 * TW + 127) + 1], dst[4 * (63 * TW + 127) + 2]);
    CHECK(q_ok, "a copy from the scaled target into one at the game's size is the picture at that size");

    // taken back to the game's size: the content scaled down into it, never scaled again
    VkCommandBuffer cb1 = vk_once_begin();
    CHECK(!vkt_unscale(ia, cb1, "test") && !vk_once_end(cb1), "taken back to the game's size");
    CHECK(ia->vw == TW && ia->vh == TH && ia->scale == 1.0f, "now %ux%u", ia->vw, ia->vh);
    memset(game, 0, sizeof game);
    vkt_read_texels(&ta, 0, 0, &all, game, TW * 4, TW * TH * 4);
    CHECK(is_px(game + 4 * (10 * TW + 20), 255, 0, 128, 255) && is_px(game + 4 * (2 * TW + 2), 10, 20, 30, 255) && is_px(game + 4 * (60 * TW + 124), 0, 200, 0, 255), "its content kept");
    vkt_tex_release(&ta); vkt_drop_pool(ta.pool);
    tex_t ta2 = vt_tex(NVN_TEXTURE_TARGET_2D, NVN_FORMAT_RGBA8, TW, TH, 1, 1, 0, 0, mem + 0x000000); vkt_tex_init(&ta2);
    vkt_image *ia2 = vkt_resolve(&ta2);
    CHECK(ia2 && ia2->vw == TW && ia2->noscale_why, "a new image over the same storage stays at the game's size (%s)", ia2 && ia2->noscale_why ? ia2->noscale_why : "-");
    CHECK(iu && iu->vw == 2 * TW, "another target is still drawn larger");

    // the render scale entries: textures in binding order, images after them; a module that reads none has none
    static const char js[] = "{\"key\":\"t_scale\",\"stage\":\"ps\",\"bindings\":["
        "{\"set\":0,\"binding\":0,\"descriptor\":\"uniform_buffer\",\"count\":1,\"nvn\":{\"kind\":\"support\"}},"
        "{\"set\":0,\"binding\":1,\"descriptor\":\"combined_image_sampler\",\"count\":1,\"nvn\":{\"kind\":\"tex\",\"reg\":3,\"sampler\":{\"kind\":\"samp_tex\",\"reg\":3},\"translator\":{\"type\":3}}},"
        "{\"set\":0,\"binding\":2,\"descriptor\":\"uniform_buffer\",\"count\":1,\"nvn\":{\"kind\":\"cbuf\",\"reg\":3}},"
        "{\"set\":0,\"binding\":3,\"descriptor\":\"uniform_texel_buffer\",\"count\":1,\"nvn\":{\"kind\":\"tbuf\",\"reg\":5,\"translator\":{\"type\":2}}}],"
        "\"inputs\":[],\"outputs\":[{\"location\":0,\"type\":\"float\",\"components\":4}]}";
    const char *why = NULL;
    vks_shader *fs = vksh_make("t_scale", (const uint8_t *)test_bind_frag_spv, sizeof test_bind_frag_spv, js, sizeof js - 1, &why);
    int reads = vkspv_reads_member(test_bind_frag_spv, sizeof test_bind_frag_spv / 4, 0, 5);
    CHECK(fs && fs->reads_scale == reads, "the module's own answer kept (%d)", reads);
    if (fs && fs->reads_scale) CHECK(fs->b[1].scale_idx == 1 && fs->b[3].scale_idx == 2 && fs->b[0].scale_idx == -1, "texture entries 1 and 2 in binding order");
    else if (fs) CHECK(fs->b[1].scale_idx == -1 && fs->b[3].scale_idx == -1, "a module that reads no render scale has no entries");
    if (fs) vksh_free(fs);

    vkt_tex_release(&ta2); vkt_tex_release(&tq); vkt_tex_release(&tsm); vkt_tex_release(&tu); vkt_drop_pool(ta2.pool);
    vkm_pool_del(mem);
    free(rb); free(own);
    uint32_t verr = atomic_load(&g_vk.val_errors);
    vkr_shutdown();
    vt_down(0);
    CHECK(verr == 0, "no validation errors (%u)", verr);
    VirtualFree(mem, 0, MEM_RELEASE);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
