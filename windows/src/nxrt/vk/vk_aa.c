// Anti-aliasing of the game's 3D picture (vk_aa.h; docs/anti-aliasing.md). Pokopia's last full-screen pass already runs the cheap console variant of
// FXAA (its shader 20cbfdc0, switched on by the game), which leaves stair steps along long shallow edges. This adds our own pass right after that
// composite, before the HUD is drawn over it, so text and menus stay sharp: FXAA 3.11 quality (one pass) or SMAA (three: edges, blending weights with the
// areas computed in the shader, neighbourhood blending), both over a copy of the picture. Both are our own implementations of the published algorithms
// (shaders/aa_*.frag; docs/PROVENANCE.md). Pixels on no edge are not written, so they stay exactly as the game drew them.
#include "vk_aa.h"
#include "vk_fmt.h"
#include "vk_submit.h"
#include "aa_vert.h"
#include "aa_fxaa_frag.h"
#include "aa_edge_frag.h"
#include "aa_weight_frag.h"
#include "aa_blend_frag.h"
#include <stdlib.h>
#include <string.h>

void vkpc_note_created(int n);

enum { K_FXAA, K_EDGE, K_WEIGHT, K_BLEND, NK, NQ = 64, NSET = 4 };
static const char *const mode_name[3] = {"off", "FXAA", "SMAA"};

typedef struct {   // what one picture size and format needs: a copy of the picture, SMAA's edges and weights, a descriptor set per pass
    VkFormat img_fmt, view_fmt; uint32_t w, h, rw, rh; uint64_t used; int said;   // rw x rh: the rectangle the last run covered
    VkImage im[3]; vk_alloc mem[3]; VkImageView copy_stored, copy_view, edges, weights;
    VkDescriptorPool dp; VkDescriptorSet ds[NK];
} aa_set;

static _Atomic int g_mode = -1;
static struct {
    int said, failed, bad_fmt;
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkSampler samp, border; VkShaderModule mod[NK + 1];   // border: the weights' gathers (zero outside)
    struct { int kind; VkFormat f; VkPipeline p; } pipe[16]; int npipe;
    aa_set *s[NSET]; uint64_t runs;   // the pictures it runs on (the game's frame, a character drawn for a menu...), the least recently used made again
    int timing; VkQueryPool qp; uint32_t qnext; double t_sum[4]; int t_n, t_mode; uint64_t t_area;   // NXRT_VK_AA_TIME=1: the passes' GPU time on pictures at least
                                                                                                      // half the largest, logged every 300 runs
} A;

int vkaa_mode_from(const char *s) {
    if (s && (!strcmp(s, "off") || !strcmp(s, "0"))) return VKAA_OFF;
    if (s && (!strcmp(s, "fxaa") || !strcmp(s, "FXAA"))) return VKAA_FXAA;
    return VKAA_SMAA;
}
int vkaa_mode(void) {
    int m = atomic_load(&g_mode);
    if (m < 0) { int u = -1; if (atomic_compare_exchange_strong(&g_mode, &u, vkaa_mode_from(getenv("NXRT_VK_AA")))) VKLOG("anti-aliasing: %s at the start (NXRT_VK_AA)", mode_name[atomic_load(&g_mode)]);
                 m = atomic_load(&g_mode); }
    return m;
}
void vkaa_set_mode(int mode) { atomic_store(&g_mode, mode < VKAA_OFF || mode > VKAA_SMAA ? VKAA_SMAA : mode); }

static VkShaderModule module(const uint32_t *code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, code};
    VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(g_vk.dev, &ci, NULL, &m); return m;
}
static int setup(void) {
    if (A.dsl) return 1;
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, b};
    if (vkCreateDescriptorSetLayout(g_vk.dev, &dl, NULL, &A.dsl) != VK_SUCCESS) return 0;
    VkPushConstantRange pr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &A.dsl, 1, &pr};
    VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0, VK_FALSE, 1, VK_FALSE,
                               VK_COMPARE_OP_NEVER, 0, 0, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE};
    if (vkCreatePipelineLayout(g_vk.dev, &pl, NULL, &A.pl) != VK_SUCCESS || vkCreateSampler(g_vk.dev, &sci, NULL, &A.samp) != VK_SUCCESS) return 0;
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    if (vkCreateSampler(g_vk.dev, &sci, NULL, &A.border) != VK_SUCCESS) return 0;
    const uint32_t *code[NK + 1] = {aa_vert_spv, aa_fxaa_frag_spv, aa_edge_frag_spv, aa_weight_frag_spv, aa_blend_frag_spv};
    size_t bytes[NK + 1] = {sizeof aa_vert_spv, sizeof aa_fxaa_frag_spv, sizeof aa_edge_frag_spv, sizeof aa_weight_frag_spv, sizeof aa_blend_frag_spv};
    for (int i = 0; i <= NK; i++) if (!(A.mod[i] = module(code[i], bytes[i]))) return 0;
    const char *e = getenv("NXRT_VK_AA_TIME");
    if (e && *e == '1' && g_vk.props.limits.timestampComputeAndGraphics) {
        VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_TIMESTAMP, NQ, 0};
        A.timing = vkCreateQueryPool(g_vk.dev, &qi, NULL, &A.qp) == VK_SUCCESS;
    }
    return 1;
}

static VkPipeline pipe_for(int kind, VkFormat f) {
    for (int i = 0; i < A.npipe; i++) if (A.pipe[i].kind == kind && A.pipe[i].f == f) return A.pipe[i].p;
    if (A.npipe == 16) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, A.mod[0], "main", NULL},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, A.mod[1 + kind], "main", NULL}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                                                 VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba = {VK_FALSE, 0, 0, 0, 0, 0, 0, 0xf};
    VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, &ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &f, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri, 0, 2, st, &vi, &ia, NULL, &vp, &rs, &ms, NULL, &cbs, &dy, A.pl,
                                       VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1};
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &ci, NULL, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
    vkpc_note_created(1);
    A.pipe[A.npipe].kind = kind; A.pipe[A.npipe].f = f; A.pipe[A.npipe].p = p; A.npipe++;
    return p;
}

// The formats it takes: colour formats the game could present, normalized or float (no integer ones), filtered in their own format and as UNORM
static int format_ok(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32: case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32: case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: break;
    default: return 0;
    }
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    VkFormatProperties a, b; vkGetPhysicalDeviceFormatProperties(g_vk.pd, f, &a); vkGetPhysicalDeviceFormatProperties(g_vk.pd, vkf_unorm_of(f), &b);
    return (a.optimalTilingFeatures & need) == need && (b.optimalTilingFeatures & need) == need;
}

static void set_free(void *p) {
    aa_set *s = p;
    if (s->dp) vkDestroyDescriptorPool(g_vk.dev, s->dp, NULL);
    VkImageView v[4] = {s->copy_stored, s->copy_view, s->edges, s->weights};
    for (int i = 0; i < 4; i++) if (v[i]) vkDestroyImageView(g_vk.dev, v[i], NULL);
    for (int i = 0; i < 3; i++) { if (s->im[i]) vkDestroyImage(g_vk.dev, s->im[i], NULL); if (s->mem[i].mem) vk_free_mem(&s->mem[i]); }
    free(s);
}
static VkImageView view_of(VkImage im, VkFormat f) {
    VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, im, VK_IMAGE_VIEW_TYPE_2D, f, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView v = VK_NULL_HANDLE; vkCreateImageView(g_vk.dev, &ci, NULL, &v); return v;
}
static aa_set *set_make(VkCommandBuffer cb, VkFormat img_fmt, VkFormat view_fmt, uint32_t w, uint32_t h) {
    aa_set *s = calloc(1, sizeof *s); if (!s) return NULL;
    s->img_fmt = img_fmt; s->view_fmt = view_fmt; s->w = w; s->h = h;
    const VkFormat fm[3] = {img_fmt, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    const VkImageUsageFlags us[3] = {VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    for (int i = 0; i < 3; i++) {
        VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, i ? 0 : VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, VK_IMAGE_TYPE_2D, fm[i], {w, h, 1}, 1, 1,
                                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, us[i], VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
        if (vkCreateImage(g_vk.dev, &ic, NULL, &s->im[i]) != VK_SUCCESS) { set_free(s); return NULL; }
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_vk.dev, s->im[i], &mr);
        if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &s->mem[i]) || vkBindImageMemory(g_vk.dev, s->im[i], s->mem[i].mem, s->mem[i].off) != VK_SUCCESS) { set_free(s); return NULL; }
        vk_image_barrier(cb, s->im[i], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
    s->copy_stored = view_of(s->im[0], vkf_unorm_of(view_fmt)); s->copy_view = view_of(s->im[0], view_fmt);
    s->edges = view_of(s->im[1], VK_FORMAT_R8G8_UNORM); s->weights = view_of(s->im[2], VK_FORMAT_R8G8B8A8_UNORM);
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * NK};
    VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, NK, 1, &ps};
    VkDescriptorSetLayout l[NK]; for (int k = 0; k < NK; k++) l[k] = A.dsl;
    VkDescriptorSetAllocateInfo da = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, VK_NULL_HANDLE, NK, l};
    if (!s->copy_stored || !s->copy_view || !s->edges || !s->weights || vkCreateDescriptorPool(g_vk.dev, &dp, NULL, &s->dp) != VK_SUCCESS) { set_free(s); return NULL; }
    da.descriptorPool = s->dp;
    if (vkAllocateDescriptorSets(g_vk.dev, &da, s->ds) != VK_SUCCESS) { set_free(s); return NULL; }
    // the edges (and FXAA's luma) read the copy as stored, gamma-encoded; the weights read the edges; the mixing reads the copy in its own format (linear
    // light for sRGB)
    const VkImageView in[NK][2] = {{s->copy_stored, s->copy_view}, {s->copy_stored, s->copy_stored}, {s->edges, s->edges}, {s->copy_view, s->weights}};
    VkDescriptorImageInfo ii[NK][2]; VkWriteDescriptorSet wr[NK * 2];
    for (int k = 0; k < NK; k++) for (int j = 0; j < 2; j++) {
        ii[k][j] = (VkDescriptorImageInfo){k == K_WEIGHT ? A.border : A.samp, in[k][j], VK_IMAGE_LAYOUT_GENERAL};
        wr[k * 2 + j] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s->ds[k], (uint32_t)j, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii[k][j], NULL, NULL};
    }
    vkUpdateDescriptorSets(g_vk.dev, NK * 2, wr, 0, NULL);
    return s;
}

static void full_barrier(VkCommandBuffer cb) {
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}
// One pass over the whole image (a cleared target is cleared whole: nothing of an earlier, larger picture is left around this one), drawn in its
// rectangle rw x rh
static int pass(VkCommandBuffer cb, const aa_set *s, int kind, VkFormat f, VkImageView out, uint32_t w, uint32_t h, uint32_t rw, uint32_t rh, int clear, const float pc[4]) {
    VkPipeline p = pipe_for(kind, f);
    if (!p) return 0;
    VkRenderingAttachmentInfo a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, out, VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                   clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0, 0, 0, 0}}}};
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, {w, h}}, 1, 0, 1, &a, NULL, NULL};
    vkCmdBeginRendering(cb, &ri);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1}; VkRect2D sc = {{0, 0}, {rw, rh}};
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, A.pl, 0, 1, &s->ds[kind], 0, NULL);
    vkCmdPushConstants(cb, A.pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);
    return 1;
}

// The completion thread, after the submission: one run's five timestamps (its start, after the copy and after each pass); a change of mode starts the
// average again
static void time_read(void *arg) {
    uint32_t q = (uint32_t)(uintptr_t)arg & 0xffff; int mode = (int)((uintptr_t)arg >> 16); uint64_t t[5];
    if (mode != A.t_mode) { A.t_mode = mode; memset(A.t_sum, 0, sizeof A.t_sum); A.t_n = 0; }
    if (vkGetQueryPoolResults(g_vk.dev, A.qp, q, 5, sizeof t, t, sizeof t[0], VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    for (int i = 0; i < 4; i++) A.t_sum[i] += t[i + 1] >= t[i] ? (double)(t[i + 1] - t[i]) * g_vk.props.limits.timestampPeriod / 1e6 : 0;
    if (++A.t_n < 300) return;
    double s[4]; for (int i = 0; i < 4; i++) s[i] = A.t_sum[i] / A.t_n;
    if (mode == VKAA_SMAA) VKLOG("anti-aliasing: SMAA took %.3f ms of GPU time a frame (copy %.3f, edges %.3f, weights %.3f, blending %.3f; 300 frames)", s[0] + s[1] + s[2] + s[3], s[0], s[1], s[2], s[3]);
    else VKLOG("anti-aliasing: FXAA took %.3f ms of GPU time a frame (copy %.3f, FXAA %.3f; 300 frames)", s[0] + s[1] + s[2] + s[3], s[0], s[1] + s[2] + s[3]);
    memset(A.t_sum, 0, sizeof A.t_sum); A.t_n = 0;
}
static void stamp(VkCommandBuffer cb, uint32_t q) { vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, A.qp, q); }

int vkaa_run(VkCommandBuffer cb, VkImage img, VkFormat img_fmt, uint32_t level, uint32_t layer, uint32_t w, uint32_t h, uint32_t rw, uint32_t rh,
             VkImageView view, VkFormat view_fmt) {
    int mode = vkaa_mode();
    if (mode != A.said) { A.said = mode; if (A.runs || mode == VKAA_OFF) VKLOG("anti-aliasing: %s", mode_name[mode]); }
    if (rw > w) rw = w; if (rh > h) rh = h;
    if (mode == VKAA_OFF || A.failed || rw < 16 || rh < 16) return 0;
    if (!setup()) { A.failed = 1; VKRED("anti-aliasing: could not be set up: left off"); return 0; }
    if (!format_ok(view_fmt)) { if (A.bad_fmt++ < 4) VKLOG("anti-aliasing: the picture's format %d is not one it takes: not applied", view_fmt); return 0; }
    aa_set *s = NULL; int lru = 0;
    for (int i = 0; i < NSET; i++) {
        aa_set *c = A.s[i];
        if (c && c->img_fmt == img_fmt && c->view_fmt == view_fmt && c->w == w && c->h == h) { s = c; break; }
        if (!c || (A.s[lru] && c->used < A.s[lru]->used)) lru = i;
    }
    if (!s) {
        if (A.s[lru]) vk_retire(set_free, A.s[lru]);
        if (!(s = A.s[lru] = set_make(cb, img_fmt, view_fmt, w, h))) { A.failed = 1; VKRED("anti-aliasing: no room for its %ux%u images: left off", w, h); return 0; }
        VKLOG("anti-aliasing: %s on a %ux%u picture (format %d), before the 2D drawn over it", mode_name[mode], w, h, view_fmt);
    }
    s->used = ++A.runs;
    if (rw != s->rw || rh != s->rh) { if (s->said++ < 4 && (rw != w || rh != h)) VKLOG("anti-aliasing: the %ux%u picture drawn at %ux%u", w, h, rw, rh); s->rw = rw; s->rh = rh; }
    full_barrier(cb);   // the picture's writes, before the copy reads them
    uint32_t q = A.qnext; uint64_t area = (uint64_t)rw * rh;
    if (area > A.t_area) A.t_area = area;
    int timed = A.timing && area * 2 >= A.t_area;   // the game's picture, also at a lower resolution (handheld), not a menu's smaller one
    if (timed) { A.qnext = (A.qnext + 8) % NQ; vkCmdResetQueryPool(cb, A.qp, q, 5); stamp(cb, q); }
    VkImageCopy r = {{VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {rw, rh, 1}};
    vkCmdCopyImage(cb, img, VK_IMAGE_LAYOUT_GENERAL, s->im[0], VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    full_barrier(cb); if (timed) stamp(cb, q + 1);
    float pc[4] = {1.0f / (float)w, 1.0f / (float)h, (float)rw, (float)rh};   // the shaders read only inside the rectangle
    int ok;
    if (mode == VKAA_FXAA) { ok = pass(cb, s, K_FXAA, view_fmt, view, w, h, rw, rh, 0, pc); if (timed) { stamp(cb, q + 2); stamp(cb, q + 3); stamp(cb, q + 4); } }
    else {
        ok = pass(cb, s, K_EDGE, VK_FORMAT_R8G8_UNORM, s->edges, w, h, rw, rh, 1, pc); full_barrier(cb); if (timed) stamp(cb, q + 2);
        ok = ok && pass(cb, s, K_WEIGHT, VK_FORMAT_R8G8B8A8_UNORM, s->weights, w, h, rw, rh, 1, pc); full_barrier(cb); if (timed) stamp(cb, q + 3);
        ok = ok && pass(cb, s, K_BLEND, view_fmt, view, w, h, rw, rh, 0, pc); if (timed) stamp(cb, q + 4);
    }
    if (timed) vks_post_fn(time_read, (void *)(uintptr_t)(q | (uint32_t)mode << 16));
    if (!ok) { A.failed = 1; VKRED("anti-aliasing: its pipelines could not be made: left off"); }
    return 1;
}

void vkaa_shutdown(void) {
    for (int i = 0; i < NSET; i++) if (A.s[i]) { set_free(A.s[i]); A.s[i] = NULL; }
    for (int i = 0; i < A.npipe; i++) vkDestroyPipeline(g_vk.dev, A.pipe[i].p, NULL);
    for (int i = 0; i <= NK; i++) if (A.mod[i]) vkDestroyShaderModule(g_vk.dev, A.mod[i], NULL);
    if (A.qp) vkDestroyQueryPool(g_vk.dev, A.qp, NULL);
    if (A.samp) vkDestroySampler(g_vk.dev, A.samp, NULL);
    if (A.border) vkDestroySampler(g_vk.dev, A.border, NULL);
    if (A.pl) vkDestroyPipelineLayout(g_vk.dev, A.pl, NULL);
    if (A.dsl) vkDestroyDescriptorSetLayout(g_vk.dev, A.dsl, NULL);
    memset(&A, 0, sizeof A);
}
