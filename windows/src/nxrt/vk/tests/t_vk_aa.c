/* Anti-aliasing (vk_aa.c) on test pictures: white on black as a game draws it without AA (a pixel is white when its centre is inside), against its true
   coverage (16 x 16 samples a pixel). A disc (every slope), and a long shallow edge (1 in 7.7, the stair steps the game's own light FXAA leaves).
   SMAA and FXAA must bring the picture closer to the coverage along the edge, leave every pixel two or more away from the edge exactly as it was, and
   do nothing when off; on an sRGB target too, measured in linear light (mixing in gamma there made FXAA's edges darker than their coverage, further
   from it than no AA). Validation on, no errors.
   The GPU part is skipped without a device (77). */
#include "vk_test.h"
#include "vk_aa.h"

NXRT_EARLY_RESERVE()

enum { W = 128, H = 72 };
static float cov[W * H];
static int far_px[W * H];   // two or more pixels from any pixel the edge passes through
static int inside(int shape, float x, float y) {   // 0 the disc, 1 below a shallow line
    if (shape == 1) return y > 30.3f + 0.13f * x;
    float dx = x - 64.3f, dy = y - 36.2f; return dx * dx + dy * dy < 21.7f * 21.7f;
}
static void coverage(int shape, uint8_t *in) {   // the true coverage, and the picture without AA
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int n = 0;
        for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) n += inside(shape, x + (i + 0.5f) / 16, y + (j + 0.5f) / 16);
        cov[y * W + x] = n / 256.0f;
        uint8_t v = inside(shape, x + 0.5f, y + 0.5f) ? 255 : 0, *q = in + 4 * (y * W + x);
        q[0] = q[1] = q[2] = v; q[3] = 255;
    }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int f = 1;
        for (int j = -2; j <= 2; j++) for (int i = -2; i <= 2; i++) { int u = x + i, v = y + j; if (u >= 0 && v >= 0 && u < W && v < H && cov[v * W + u] > 0 && cov[v * W + u] < 1) f = 0; }
        far_px[y * W + x] = f;
    }
}
static float s2l(float s) { return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f); }

typedef struct { VkImage img; vk_alloc mem; VkImageView view; VkFormat f; uint32_t rw, rh; } timg;
static VkBuffer g_buf; static vk_alloc g_buf_mem;

static int img_make(timg *t, VkFormat f) {
    memset(t, 0, sizeof *t); t->f = f; t->rw = W; t->rh = H;
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, VK_IMAGE_TYPE_2D, f, {W, H, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(g_vk.dev, &ic, NULL, &t->img) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_vk.dev, t->img, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &t->mem) || vkBindImageMemory(g_vk.dev, t->img, t->mem.mem, t->mem.off) != VK_SUCCESS) return 0;
    VkImageViewCreateInfo vc = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, t->img, VK_IMAGE_VIEW_TYPE_2D, f, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    return vkCreateImageView(g_vk.dev, &vc, NULL, &t->view) == VK_SUCCESS;
}
static void img_free(timg *t) {
    vkDestroyImageView(g_vk.dev, t->view, NULL); vkDestroyImage(g_vk.dev, t->img, NULL); vk_free_mem(&t->mem);
}
static void upload(timg *t, const uint8_t *px) {
    memcpy(g_buf_mem.map, px, W * H * 4);
    VkCommandBuffer cb = vk_once_begin();
    vk_image_barrier(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy c = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
    vkCmdCopyBufferToImage(cb, g_buf, t->img, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    vk_once_end(cb);
}
static void download(timg *t, uint8_t *px) {
    VkCommandBuffer cb = vk_once_begin();
    VkBufferImageCopy c = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
    vkCmdCopyImageToBuffer(cb, t->img, VK_IMAGE_LAYOUT_GENERAL, g_buf, 1, &c);
    vk_once_end(cb);
    memcpy(px, g_buf_mem.map, W * H * 4);
}
static int run(timg *t, int mode) {
    vkaa_set_mode(mode);
    VkCommandBuffer cb = vk_once_begin();
    int r = vkaa_run(cb, t->img, t->f, 0, 0, W, H, t->rw, t->rh, t->view, t->f);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    vk_once_end(cb);
    return r;
}
// The picture's distance from the true coverage (summed over the pixels, in linear light), and whether the pixels away from the edge are untouched
static double err(const uint8_t *px, int srgb) {
    double e = 0;
    for (int i = 0; i < W * H; i++) { float v = px[4 * i + 1] / 255.0f; e += fabs((srgb ? s2l(v) : v) - cov[i]); }
    return e;
}
static int far_same(const uint8_t *a, const uint8_t *b) { for (int i = 0; i < W * H; i++) if (far_px[i] && memcmp(a + 4 * i, b + 4 * i, 4)) return 0; return 1; }

static void one(VkFormat f, const char *name, const uint8_t *in, double smaa_at_most) {
    int srgb = f == VK_FORMAT_R8G8B8A8_SRGB;
    timg t; CHECK(img_make(&t, f), "%s: test image made", name);
    uint8_t *out = malloc(W * H * 4);
    double e0 = err(in, srgb);
    upload(&t, in); CHECK(!run(&t, VKAA_OFF), "%s: off does nothing", name);
    download(&t, out); CHECK(!memcmp(in, out, W * H * 4), "%s: off leaves the picture as it was", name);
    static const int modes[2] = {VKAA_SMAA, VKAA_FXAA}; static const char *const mn[2] = {"SMAA", "FXAA"};
    for (int m = 0; m < 2; m++) {
        upload(&t, in); CHECK(run(&t, modes[m]), "%s %s: run", name, mn[m]);
        download(&t, out);
        double e = err(out, srgb); int changed = 0;
        for (int i = 0; i < W * H; i++) changed += memcmp(in + 4 * i, out + 4 * i, 4) != 0;
        printf("%s %s: distance from the true coverage %.1f -> %.1f (%.0f%%), %d pixels changed\n", name, mn[m], e0, e, 100 * e / e0, changed);
        double most = m == 0 ? smaa_at_most : 0.8;
        CHECK(e < most * e0, "%s %s: the edge is closer to its true coverage (%.1f -> %.1f, at most %.0f%%)", name, mn[m], e0, e, 100 * most);
        CHECK(far_same(in, out), "%s %s: pixels away from the edge are untouched", name, mn[m]);
        int alpha = 1; for (int i = 0; i < W * H; i++) alpha &= out[4 * i + 3] == 255;
        CHECK(alpha, "%s %s: alpha stays opaque", name, mn[m]);
        // a picture drawn at a lower resolution into part of its target (handheld mode): only its rectangle from 0,0 changes
        t.rw = W - 40; t.rh = H - 22; upload(&t, in); CHECK(run(&t, modes[m]), "%s %s: run on a rectangle", name, mn[m]); download(&t, out);
        int outside = 1, inside = 0;
        for (int i = 0; i < W * H; i++) { int x = i % W, y = i / W, d = memcmp(in + 4 * i, out + 4 * i, 4) != 0; if (x >= (int)t.rw || y >= (int)t.rh) outside &= !d; else inside += d; }
        CHECK(outside && inside > 0, "%s %s: on a %ux%u rectangle only it changes (%d pixels in it)", name, mn[m], t.rw, t.rh, inside);
        t.rw = W; t.rh = H;
    }
    free(out); img_free(&t);
}

int main(void) {
    crash_install();
    int rc = vt_up(0, 1);
    if (rc == 77) { printf("GPU part skipped (no device)\n"); return 77; }
    if (rc) return rc;
    uint8_t *in = malloc(W * H * 4);
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, W * H * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    vkCreateBuffer(g_vk.dev, &bi, NULL, &g_buf);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, g_buf, &mr);
    CHECK(!vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &g_buf_mem), "staging buffer");
    vkBindBufferMemory(g_vk.dev, g_buf, g_buf_mem.mem, g_buf_mem.off);
    CHECK(vkaa_mode_from("off") == VKAA_OFF && vkaa_mode_from("fxaa") == VKAA_FXAA && vkaa_mode_from("smaa") == VKAA_SMAA && vkaa_mode_from(NULL) == VKAA_SMAA,
          "the setting's names (SMAA when unset)");
    coverage(0, in);
    one(VK_FORMAT_R8G8B8A8_UNORM, "disc, RGBA8", in, 0.8);
    one(VK_FORMAT_R8G8B8A8_SRGB, "disc, RGBA8 sRGB", in, 0.8);
    coverage(1, in);
    one(VK_FORMAT_R8G8B8A8_SRGB, "shallow edge, RGBA8 sRGB", in, 0.25);   // SMAA 12%, FXAA 39% (2026-11-08)
    vks_finish();
    vkaa_shutdown();
    vkDestroyBuffer(g_vk.dev, g_buf, NULL); vk_free_mem(&g_buf_mem);
    free(in);
    uint32_t verr = atomic_load(&g_vk.val_errors);
    vt_down(0);
    CHECK(verr == 0, "no validation errors (%u)", verr);
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
