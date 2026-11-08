// The replay of NVN command streams on Vulkan (vk_replay.h; docs/m5-replay.md). Ported behaviour from nxnative's dm/gfx/nvn/nvn_mtl.m (MIT): the record
// walk (mtl_exec), the binding state and its defaults (state_op, state_reset, per-queue state), what each shader binding takes (encode_args_: uniform
// slots, the driver segments c0 / c2 and their tables, the support buffer, c1, textures through the registered handles), the viewport, scissor and
// winding rules (draw_), clears with their scissor and channel mask (OP_CLEAR_COLOR / OP_CLEAR_DS), copies (copy_b2t / copy_t2b / copy_t2t) with the
// storage mirror of storage-backed textures, counters (occlusion queries summed from reset to report). The Vulkan side is ours: dynamic rendering, one
// descriptor set per stage, extended dynamic state, the pool mirrors of vk_mem.c and the images of vk_tex.c, GENERAL as the resting layout of GPU images.
#include "vk_replay.h"
#include <ctype.h>
#include "vk_submit.h"
#include "vk_mem.h"
#include "vk_tex.h"
#include "vk_fmt.h"
#include "vk_shader.h"
#include "vk_pipe.h"
#include "vk_hmap.h"
#include "vk_shot.h"
#include "nvn_timestamp.h"
#include "clear_vert.h"
#include "clear_f_frag.h"
#include "clear_u_frag.h"
#include "clear_i_frag.h"
#include "quad_args_comp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dm_guest_write_prepare(void *addr, size_t len);
int nvn_dpool_base(const void *pool, int tex);   // hle_nvn.c: where a descriptor pool's IDs start in the host table
void vk_note_fence_sync(sync_t *s);              // gfx_vk.c: the sync gets the submission's value at the commit
uint64_t guest_ns(void);
void vkpc_note_created(int n);

#define RED_ONCE(id, ...) do { static int once_##id; if (!once_##id++) VKRED(__VA_ARGS__); } while (0)
enum { SUPPORT_SIZE = 1392, ZERO_SIZE = 65536, NTEX = 256, MAXB = 192, NQ = 4096 };

static vkr_stats_t g_st;
static uint64_t g_pd_frame, g_pd_last; static int g_pd;   // NXRT_VK_PASSDUMP=<present>[-<present>]: the frames after those presents, pass by pass and draw by draw, in the log
#define PD(...) do { if (g_pd) nxrt_logf("[pd] " __VA_ARGS__); } while (0)
static vkt_image *g_td[1024]; static int g_ntd, g_td_on; static uint64_t g_td_frame;   // NXRT_VK_TEXDUMP=<present>: the images that frame drew into or wrote, as PNG at the next present
static void td_note(vkt_image *im) { if (!g_td_on || !im) return; for (int i = 0; i < g_ntd; i++) if (g_td[i] == im) return; if (g_ntd < 1024) g_td[g_ntd++] = im; }
const char *nxrt_log_path(void);
static int g_origin_ll = 1, g_queue_origin_ll = 1, g_depth_zero = 0;
static VkPipeline g_last_pipe;   // the graphics pipeline bound last in this submission
// Quantum's dynamic-state cache: the dynamic state last recorded into the current command buffer, so a draw sets only what changed (about 25 vkCmdSet*
// calls a draw, 2,000+ draws a frame). Forgotten (every value impossible) when the command buffer changes, at each submission and render pass, and
// after the clear pipeline (its own static state). NXRT_VK_STATE_CACHE=0: every draw sets everything, as before
static struct {
    VkCommandBuffer cb; VkViewport vp; VkRect2D sc; VkCullModeFlags cull; VkFrontFace ff; VkPrimitiveTopology topo; VkBool32 bias_en; float bias[3]; float bc[4];
    VkBool32 dt, dw; VkCompareOp dcmp; VkBool32 st_en; VkStencilOp sop[2][3]; VkCompareOp scmp[2]; uint32_t scm[2], swm[2], sref[2]; VkBool32 rd, pr, fixed;
    uint32_t ncol; VkBool32 ben[8]; VkColorBlendEquationEXT beq[8]; VkColorComponentFlags wm[8]; VkBool32 logic_en; VkLogicOp lop; VkBool32 a2c; VkPolygonMode pm; VkBool32 dclamp;
    struct { VkBuffer b; VkDeviceSize o, n; } vb[17];
} DC;
static int g_dc_on = -1;
static void dc_reset(void) { memset(&DC, 0xff, sizeof DC); DC.cb = VK_NULL_HANDLE; }
static inline int dc_chg(void *slot, const void *v, size_t n) { if (!memcmp(slot, v, n)) return 0; memcpy(slot, v, n); return 1; }

// ---------- Resources that live with a submission: arena chunks, descriptor pools, buffer views (returned at completion) ----------
typedef struct rchunk { VkBuffer buf; vk_alloc mem; VkDeviceSize size, used; struct rchunk *next; } rchunk;
typedef struct rdpool { VkDescriptorPool pool; struct rdpool *next; } rdpool;
typedef struct { rchunk *chunks; rdpool *pools; VkBufferView *views; int nviews, cviews; int tq; } subres;   // tq: timestamp pair + 1
static subres g_sub;
// GPU time: a timestamp before and after each submission's replay, read at its completion
enum { NTQ = 256 };
static VkQueryPool g_tqp; static uint32_t g_tq_next; static _Atomic uint64_t g_gpu_ns, g_gpu_subs;
static rchunk *g_free_chunks; static rdpool *g_free_pools; static SRWLOCK g_free_lk = SRWLOCK_INIT;

static void subres_done(void *arg) {
    subres *r = arg;
    uint64_t t[2];
    if (r->tq && vkGetQueryPoolResults(g_vk.dev, g_tqp, 2 * (uint32_t)(r->tq - 1), 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && t[1] >= t[0]) {
        atomic_fetch_add(&g_gpu_ns, (uint64_t)((double)(t[1] - t[0]) * g_vk.props.limits.timestampPeriod)); atomic_fetch_add(&g_gpu_subs, 1);
    }
    AcquireSRWLockExclusive(&g_free_lk);
    while (r->chunks) { rchunk *c = r->chunks; r->chunks = c->next; c->next = g_free_chunks; g_free_chunks = c; }
    while (r->pools) { rdpool *p = r->pools; r->pools = p->next; p->next = g_free_pools; g_free_pools = p; }
    ReleaseSRWLockExclusive(&g_free_lk);
    for (int i = 0; i < r->nviews; i++) vkDestroyBufferView(g_vk.dev, r->views[i], NULL);
    free(r->views); free(r);
}
static rchunk *chunk_take(VkDeviceSize need) {
    AcquireSRWLockExclusive(&g_free_lk);
    rchunk **pp = &g_free_chunks; while (*pp && (*pp)->size < need) pp = &(*pp)->next;
    rchunk *c = *pp; if (c) *pp = c->next;
    ReleaseSRWLockExclusive(&g_free_lk);
    if (c) { c->used = 0; return c; }
    c = calloc(1, sizeof *c); c->size = need > (4u << 20) ? need : (4u << 20);
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, c->size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, &c->buf) != VK_SUCCESS) { free(c); return NULL; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, c->buf, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &c->mem)) { vkDestroyBuffer(g_vk.dev, c->buf, NULL); free(c); return NULL; }
    vkBindBufferMemory(g_vk.dev, c->buf, c->mem.mem, c->mem.off);
    vk_name((uint64_t)c->buf, VK_OBJECT_TYPE_BUFFER, "replay arena");
    return c;
}
// Host-visible memory the GPU reads in this submission (driver segments, expanded indices, copies of unaligned ranges)
static void *arena(VkDeviceSize n, VkDeviceSize align, VkBuffer *buf, VkDeviceSize *off) {
    if (!align) align = 16;
    rchunk *c = g_sub.chunks;
    if (!c || (c->used + align - 1) / align * align + n > c->size) {
        if (!(c = chunk_take(n))) { RED_ONCE(1, "replay arena: no host memory"); return NULL; }
        c->next = g_sub.chunks; g_sub.chunks = c;
    }
    VkDeviceSize o = (c->used + align - 1) / align * align;
    c->used = o + n; *buf = c->buf; *off = o;
    return (uint8_t *)c->mem.map + o;
}
static VkDescriptorPool dpool_take(void) {
    AcquireSRWLockExclusive(&g_free_lk);
    rdpool *p = g_free_pools; if (p) g_free_pools = p->next;
    ReleaseSRWLockExclusive(&g_free_lk);
    if (p) { vkResetDescriptorPool(g_vk.dev, p->pool, 0); }
    else {
        p = calloc(1, sizeof *p);
        VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1024}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 512},
                                     {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1024}};
        VkDescriptorPoolCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 2048, sizeof ps / sizeof *ps, ps};
        if (vkCreateDescriptorPool(g_vk.dev, &ci, NULL, &p->pool) != VK_SUCCESS) { free(p); return VK_NULL_HANDLE; }
    }
    p->next = g_sub.pools; g_sub.pools = p;
    return p->pool;
}
static VkDescriptorSet dset_alloc(VkDescriptorSetLayout l) {
    VkDescriptorSet s = VK_NULL_HANDLE;
    for (int attempt = 0; attempt < 2; attempt++) {
        VkDescriptorPool p = g_sub.pools && !attempt ? g_sub.pools->pool : dpool_take();
        if (!p) break;
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, p, 1, &l};
        if (vkAllocateDescriptorSets(g_vk.dev, &ai, &s) == VK_SUCCESS) { g_st.desc_sets++; return s; }
    }
    RED_ONCE(2, "descriptor sets exhausted"); return VK_NULL_HANDLE;
}
static void view_keep(VkBufferView v) {
    if (g_sub.nviews == g_sub.cviews) { g_sub.cviews = g_sub.cviews ? 2 * g_sub.cviews : 32; g_sub.views = realloc(g_sub.views, (size_t)g_sub.cviews * sizeof *g_sub.views); }
    g_sub.views[g_sub.nviews++] = v;
}

// ---------- Fixed resources ----------
static struct { VkBuffer zero, sink, support, quads, bounce; vk_alloc zero_m, sink_m, support_m, quads_m, bounce_m; VkSampler samp; VkQueryPool qp; } R0;
// A storage buffer at an offset Vulkan cannot bind (minStorageBufferOffsetAlignment; Metal binds at any 4-byte offset): for a dispatch its bytes are copied into
// an aligned place of R0.bounce, bound there and, when the shader writes it, copied back after the dispatch (M6: a 4-byte counter of a gameplay compute shader)
enum { BOUNCE_SIZE = 1 << 20, BOUNCE_MAX = 64 << 10 };
static struct { VkBuffer pool; VkDeviceSize poff, boff, n; } g_bnc[16]; static int g_nbnc; static VkDeviceSize g_bnc_next;
static int mkbuf(VkDeviceSize n, VkBufferUsageFlags u, VkMemoryPropertyFlags want, VkBuffer *b, vk_alloc *m, const char *name) {
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, n, u, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, b) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, *b, &mr);
    if (vk_alloc_mem(&mr, want, 0, m)) return -1;
    vkBindBufferMemory(g_vk.dev, *b, m->mem, m->off);
    vk_name((uint64_t)*b, VK_OBJECT_TYPE_BUFFER, name);
    return 0;
}

// ---------- Binding state (nvn_mtl.m's S and R: per queue, kept across submissions) ----------
typedef struct rs {
    uint64_t serial;
    const prog_t *bst[5]; const prog_t *cprog;
    blend_st blend[8]; color_st color; chmask_st chmask; ms_st ms; poly_st poly; ds_st ds;
    va_st attr[16]; int nattr; vs_st stream[16]; int nstream; NVNbufferRange vbuf[16];
    p_bind_buf ubo[6][16], ssbo[6][16];
    uint64_t tex[6][NTEX], samp[6][128], img[6][32];
    uint32_t st_vmask[2], st_wmask[2], st_ref[2];
    p_poly_offset po; float vp[16][4]; float dr[2]; int has_vp, depth_clamp; uint8_t vp_sw[16][4];
    float blend_color[4], alpha_ref; int64_t copy_stride, copy_img_stride;
    int tf_on, rast_discard, tex_base, samp_base, render_skip, vis_on;
    p_prim_restart restart;
    // render targets (the snapshots copied: the record's command memory may be reused after the submission)
    int nrt; tex_t ct[8]; uint8_t has_ct[8]; view_t cview[8]; uint8_t has_cview[8]; tex_t dt; uint8_t has_dt; view_t dview; uint8_t has_dview;
    p_scissor sc; int has_sc;
    struct rs *next;
} rs;
static rs *g_states, *S;
static void rs_reset(rs *s) {
    uint64_t serial = s->serial; struct rs *next = s->next;
    memset(s, 0, sizeof *s); s->serial = serial; s->next = next;
    for (int i = 0; i < 8; i++) s->blend[i] = (blend_st){(uint64_t)i, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ZERO, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ZERO, NVN_BLEND_EQUATION_ADD, NVN_BLEND_EQUATION_ADD};
    s->color = (color_st){0, NVN_LOGIC_OP_COPY, NVN_ALPHA_FUNC_ALWAYS}; s->chmask.mask = 0xffffffffu;
    s->poly = (poly_st){NVN_FACE_NONE, NVN_FRONT_FACE_CCW, NVN_POLYGON_MODE_FILL, 0};
    s->ds = (ds_st){0, 1, NVN_DEPTH_FUNC_LESS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, 1, 1, 1, 1, 1, 1};
    s->st_vmask[0] = s->st_vmask[1] = s->st_wmask[0] = s->st_wmask[1] = 0xff; s->dr[1] = 1;
}
static rs *rs_get(uint64_t serial) {
    for (rs *s = g_states; s; s = s->next) if (s->serial == serial) return s;
    rs *s = calloc(1, sizeof *s); s->serial = serial; s->next = g_states; g_states = s; rs_reset(s);
    return s;
}

// ---------- Programs: the shader of each NVN stage, cached by program serial ----------
typedef struct { const vks_shader *sh[6]; } progc;
static vk_hmap g_progc = {8};
static const vks_shader *prog_stage(const prog_t *p, int stage) {
    if (!p || !p->serial) return NULL;
    progc *c = vk_hm_get(&g_progc, &p->serial);
    if (c && c->sh[stage]) return c->sh[stage];
    if (!c) { c = calloc(1, sizeof *c); vk_hm_put(&g_progc, &p->serial, c); }
    for (unsigned i = 0; i < p->nstage && i < 6; i++) { const vks_shader *s = vksh_get(p, i); if (s && s->stage >= 0 && s->stage < 6) c->sh[s->stage] = s; }
    return c->sh[stage];
}

// ---------- Barriers ----------
static void barrier_all(VkCommandBuffer cb) {
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    g_st.barriers++;
}

// ---------- GPU writes into pools: copied back to guest memory at completion (vkm_gpu_wrote; transfers cannot be recorded inside a pass) ----------
static struct { uint64_t a, n; } g_wb[256]; static int g_nwb;
static void wb_flush(void) { for (int i = 0; i < g_nwb; i++) { vkm_gpu_wrote(g_wb[i].a, g_wb[i].n); g_st.writebacks++; } g_nwb = 0; }

// ---------- Occlusion queries: samples counted from a reset to each report, summed on the CPU when the GPU is done (in submission order) ----------
static _Atomic uint8_t g_qbusy[NQ]; static uint32_t g_qnext; static uint64_t g_vis_acc;
typedef struct { int kind; uint32_t q; uint64_t addr; } qpost;   // kind 0 add the result of q, 1 reset, 2 report into addr
static void q_post_run(void *arg) {
    qpost *p = arg;
    if (p->kind == 0) {
        uint64_t v = 0;
        if (vkGetQueryPoolResults(g_vk.dev, R0.qp, p->q, 1, sizeof v, &v, sizeof v, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) g_vis_acc += v;
        atomic_store(&g_qbusy[p->q], 0);
    } else if (p->kind == 1) g_vis_acc = 0;
    else {
        uint64_t rec[2] = {g_vis_acc, nvn_timestamp_from_ns(guest_ns())};
        dm_guest_write_prepare((void *)(uintptr_t)p->addr, sizeof rec);
        memcpy((void *)(uintptr_t)p->addr, rec, sizeof rec);
    }
    free(p);
}
static void q_post(int kind, uint32_t q, uint64_t addr) { qpost *p = malloc(sizeof *p); p->kind = kind; p->q = q; p->addr = addr; vks_post_fn(q_post_run, p); }
static int q_alloc(void) {
    for (int round = 0; round < 2; round++) {
        for (uint32_t k = 0; k < NQ; k++) { uint32_t q = (g_qnext + k) % NQ; if (!atomic_load(&g_qbusy[q])) { g_qnext = q + 1; atomic_store(&g_qbusy[q], 1); vkResetQueryPool(g_vk.dev, R0.qp, q, 1); return (int)q; } }
        RED_ONCE(3, "all %d occlusion queries in flight", NQ); return -1;
    }
    return -1;
}

// ---------- Render passes (dynamic rendering) ----------
typedef struct {
    int valid, open, ncolor, any;
    VkImageView cv[8]; vkt_image *ci[8]; VkFormat cf[8]; uint32_t cw[8], ch[8], cnl[8], clv[8], cl0[8];   // clv / cl0: the level and first layer (partial clears)
    VkImageView dv; vkt_image *di; VkFormat df; int hd, hs; uint32_t dw, dh, dnl, dlv, dl0;
    uint32_t w, h; uint8_t samples;   // sizes in NVN's pixels (the game's)
    // F-5: the render area in the attachments' own pixels, and how NVN's map into them (the first attachment's level: ref, ref_lv; rx / ry; its scale)
    uint32_t pw, ph; vkt_image *ref; uint32_t ref_lv; float rx, ry, scale;
    int query;
} pass_t;
static pass_t P = {.query = -1};
static uint32_t mipd(uint32_t s, uint32_t l) { s >>= l; return s ? s : 1; }
// An NVN rectangle on level lv of im in the image's own pixels (vkt_sx / vkt_sy), within the size lim_w x lim_h
static VkRect2D px_rect(const vkt_image *im, uint32_t lv, VkRect2D r, uint32_t lim_w, uint32_t lim_h) {
    if (!im || (im->vw == im->w && im->vh == im->h)) return r;
    int32_t x0 = vkt_sx(im, lv, r.offset.x), y0 = vkt_sy(im, lv, r.offset.y), x1 = vkt_sx(im, lv, (int64_t)r.offset.x + r.extent.width), y1 = vkt_sy(im, lv, (int64_t)r.offset.y + r.extent.height);
    if (x1 > (int32_t)lim_w) x1 = (int32_t)lim_w; if (y1 > (int32_t)lim_h) y1 = (int32_t)lim_h;
    if (x1 < x0) x1 = x0; if (y1 < y0) y1 = y0;
    return (VkRect2D){{x0, y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
}

static void pass_end(void) {
    if (!P.open) return;
    VkCommandBuffer cb = vks_cb();
    if (P.query >= 0) { vkCmdEndQuery(cb, R0.qp, (uint32_t)P.query); q_post(0, (uint32_t)P.query, 0); P.query = -1; }
    vkCmdEndRendering(cb);
    P.open = 0;
    wb_flush();
}
// An attachment's view: one level, the layers the NVN view names (else all from its first), the view's format through the image's fallback, identity swizzle
static VkImageView att_view(vkt_image *im, const tex_t *t, const view_t *v, int has_v, int depth, uint32_t *w, uint32_t *h, uint32_t *nl, VkFormat *fmt, uint32_t *lv, uint32_t *l0) {
    uint32_t level = has_v && v->set_levels ? v->base_level : 0, b0 = has_v && v->set_layers ? v->min_layer : 0;
    if (level >= im->levels) level = im->levels - 1;
    int is3d = im->vtype == VK_IMAGE_VIEW_TYPE_3D;
    uint32_t all = is3d ? mipd(im->depth, level) : im->layers, n = has_v && v->set_layers && v->nlayers ? v->nlayers : all > b0 ? all - b0 : 1;
    if (b0 >= all) b0 = all - 1; if (b0 + n > all) n = all - b0;
    VkFormat f = depth ? im->fi->vk : vkf_view_format(t->d.format, has_v && v->set_format ? v->format : t->d.format);
    *w = mipd(im->w, level); *h = mipd(im->h, level); *nl = n; *fmt = f; *lv = level; *l0 = b0;
    VkImageViewType vt = n > 1 || is3d || im->layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    return vkt_view_raw_(im, f, vt, level, 1, b0, n, depth ? im->aspect : VK_IMAGE_ASPECT_COLOR_BIT, NULL);
}
static int scaled_im(const vkt_image *im) { return im && (im->vw != im->w || im->vh != im->h); }
static void pass_targets(void) {
    if (vkt_scale() != 1.0f) {   // F-5: attachments drawn larger and ones at the game's size cannot share a render area: the larger go back (Ryujinx blacklists them)
        vkt_image *ims[9]; int n = 0, big = 0;
        for (int i = 0; i < S->nrt && i < 8; i++) if (S->has_ct[i]) { vkt_image *im = vkt_resolve(&S->ct[i]); if (im && (im->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) { ims[n++] = im; big += scaled_im(im); } }
        if (S->has_dt) { vkt_image *im = vkt_resolve(&S->dt); if (im && (im->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) { ims[n++] = im; big += scaled_im(im); } }
        if (big && big < n) {
            static int nl; if (nl++ < 12) { char b[700]; int o = 0;
                for (int k = 0; k < n; k++) o += snprintf(b + o, sizeof b - o, " ser%llu %ux%u %s (%s)", (unsigned long long)ims[k]->serial, ims[k]->w, ims[k]->h, ims[k]->fi->name, scaled_im(ims[k]) ? "larger" : ims[k]->noscale_why ? ims[k]->noscale_why : "?");
                VKLOG("a pass attaches targets drawn larger and at the game's size:%s", b); }
            pass_end(); for (int k = 0; k < n; k++) if (scaled_im(ims[k])) vkt_unscale(ims[k], vks_cb(), "attached beside a target at the game's size");
        }
    }
    memset(P.cv, 0, sizeof P.cv); memset(P.ci, 0, sizeof P.ci); memset(P.cf, 0, sizeof P.cf);
    P.dv = VK_NULL_HANDLE; P.di = NULL; P.df = VK_FORMAT_UNDEFINED; P.hd = P.hs = 0; P.ncolor = 0; P.any = 0;
    P.w = P.h = 16384; P.samples = 1;
    P.pw = P.ph = 16384; P.ref = NULL; P.ref_lv = 0; P.rx = P.ry = P.scale = 1.0f;
    for (int i = 0; i < S->nrt && i < 8; i++) {
        if (!S->has_ct[i]) continue;
        const tex_t *t = &S->ct[i];
        int dup = 0; for (int j = 0; j < i; j++) dup |= S->has_ct[j] && S->ct[j].serial == t->serial &&
            (S->has_cview[j] && S->cview[j].set_levels ? S->cview[j].base_level : 0) == (S->has_cview[i] && S->cview[i].set_levels ? S->cview[i].base_level : 0) &&
            (S->has_cview[j] && S->cview[j].set_layers ? S->cview[j].min_layer : 0) == (S->has_cview[i] && S->cview[i].set_layers ? S->cview[i].min_layer : 0);
        if (dup) { RED_ONCE(10, "the same texture level is attached at two colour slots: only the first is attached (nvn_mtl.m rt_dup_slot)"); continue; }
        vkt_image *im = vkt_resolve(t);
        if (!im) continue;
        if (!(im->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            static int n; if (n++ < 8) VKRED("render target ser=%llu %ux%u format %#x (%s) cannot be a colour attachment here (%s): draws to it are lost",
                                             (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, im->fi->name, im->cls == VKT_STORAGE ? "its content comes from its storage" : "the format cannot");
            continue;
        }
        P.cv[i] = att_view(im, t, &S->cview[i], S->has_cview[i], 0, &P.cw[i], &P.ch[i], &P.cnl[i], &P.cf[i], &P.clv[i], &P.cl0[i]);
        if (!P.cv[i]) continue;
        P.ci[i] = im; P.ncolor = i + 1; P.any = 1; td_note(im);
        if (P.cw[i] < P.w) P.w = P.cw[i]; if (P.ch[i] < P.h) P.h = P.ch[i];
        if (vkt_vlw(im, P.clv[i]) < P.pw) P.pw = vkt_vlw(im, P.clv[i]); if (vkt_vlh(im, P.clv[i]) < P.ph) P.ph = vkt_vlh(im, P.clv[i]);
        if (!P.ref) { P.ref = im; P.ref_lv = P.clv[i]; }
        P.samples = (uint8_t)im->samples;
    }
    if (S->has_dt) {
        const tex_t *t = &S->dt; vkt_image *im = vkt_resolve(t);
        if (im && (im->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            P.dv = att_view(im, t, &S->dview, S->has_dview, 1, &P.dw, &P.dh, &P.dnl, &P.df, &P.dlv, &P.dl0);
            if (P.dv) {
                P.di = im; P.hd = (im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0; P.hs = (im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0; P.any = 1;
                if (P.dw < P.w) P.w = P.dw; if (P.dh < P.h) P.h = P.dh;
                if (vkt_vlw(im, P.dlv) < P.pw) P.pw = vkt_vlw(im, P.dlv); if (vkt_vlh(im, P.dlv) < P.ph) P.ph = vkt_vlh(im, P.dlv);
                if (!P.ref) { P.ref = im; P.ref_lv = P.dlv; }
                P.samples = (uint8_t)im->samples;
            }
        } else if (im) RED_ONCE(11, "depth target format %#x cannot be a depth attachment: not attached", t->d.format);
    }
    if (!P.any) { P.w = P.h = 0; P.pw = P.ph = 0; }
    if (scaled_im(P.ref)) { P.rx = (float)vkt_vlw(P.ref, P.ref_lv) / mipd(P.ref->w, P.ref_lv); P.ry = (float)vkt_vlh(P.ref, P.ref_lv) / mipd(P.ref->h, P.ref_lv); P.scale = P.ref->scale; }
    P.valid = 1;
}
// Opens the pass over the current targets (loading what they hold). An attachment-less pass covers the viewport (nvn_mtl.m bare_dims)
static int pass_begin(void) {
    if (P.open) return 1;
    if (!P.valid) pass_targets();
    uint32_t w = P.pw, h = P.ph;
    if (!P.any) {
        if (S->nrt || S->has_dt) return 0;   // targets set but none could be attached
        w = S->has_vp ? (uint32_t)(S->vp[0][0] + S->vp[0][2]) : 0; h = S->has_vp ? (uint32_t)(S->vp[0][1] + S->vp[0][3]) : 0;
        if (!w || !h) return 0;
        RED_ONCE(12, "attachment-less pass at the viewport's size %ux%u (nvn_mtl.m bare_dims)", w, h);
    }
    VkCommandBuffer cb = vks_cb();
    barrier_all(cb);
    VkRenderingAttachmentInfo ca[8], da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, sa;
    for (int i = 0; i < P.ncolor; i++)
        ca[i] = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, P.cv[i], VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0}}}};
    if (P.dv) da = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, P.dv, VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0}}}};
    sa = da;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, {w, h}}, 1, 0, (uint32_t)P.ncolor, ca, P.hd ? &da : NULL, P.hs ? &sa : NULL};
    vkCmdBeginRendering(cb, &ri); dc_reset();
    P.open = 1; g_st.passes++;
    if (g_pd) {
        char b[600]; int o = 0;
        for (int i = 0; i < P.ncolor; i++) if (P.cv[i]) o += snprintf(b + o, sizeof b - o, " c%d=ser%llu/%s/%ux%u", i, (unsigned long long)P.ci[i]->serial, P.ci[i]->fi->name, P.cw[i], P.ch[i]);
        if (P.dv) o += snprintf(b + o, sizeof b - o, " d=ser%llu/%s/%ux%u", (unsigned long long)P.di->serial, P.di->fi->name, P.dw, P.dh);
        PD("pass %llu %ux%u%s", (unsigned long long)g_st.passes, w, h, b);
        for (int i = 0; i < S->nrt; i++) if (S->has_ct[i]) PD("  c%d storage pool %p +%#llx mem %p flags %#x size %#llx image %p", i, (const void *)S->ct[i].pool, (unsigned long long)S->ct[i].off, (const void *)S->ct[i].mem, S->ct[i].d.flags, (unsigned long long)S->ct[i].L.size, (void *)P.ci[i]);
    }
    return 1;
}

// ---------- Clears (nvn_mtl.m OP_CLEAR_COLOR / OP_CLEAR_DS): NVN clears the whole target level (every layer of its view) within the scissor ----------
static int fmt_int_kind(VkFormat f) {   // 0 float / normalized, 1 unsigned integer, 2 signed integer
    switch (f) {
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_S8_UINT: return 1;
    case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32A32_SINT: case VK_FORMAT_A2B10G10R10_SINT_PACK32: return 2;
    default: return 0;
    }
}
static unsigned fmt_channels(VkFormat f) {   // the colour channels a format has, as an NVN channel mask (nvn_mtl.m mtl_pf_chmask)
    switch (f) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SNORM: case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SNORM: case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT: case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R8_SRGB: return 0x1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_SNORM: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT: return 0x3;
    case VK_FORMAT_R5G6B5_UNORM_PACK16: case VK_FORMAT_B5G6R5_UNORM_PACK16: case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return 0x7;
    default: return 0xf;
    }
}
static VkClearValue clear_color_value(VkFormat f, const float *rgba, const uint32_t *u, int is_uint) {
    VkClearValue v; int k = fmt_int_kind(f);
    for (int i = 0; i < 4; i++) {
        if (is_uint) { if (k == 0) v.color.float32[i] = (float)u[i]; else v.color.uint32[i] = u[i]; }
        else if (k == 1) v.color.uint32[i] = rgba[i] <= 0 ? 0 : (uint32_t)rgba[i];
        else if (k == 2) v.color.int32[i] = (int32_t)rgba[i];
        else v.color.float32[i] = rgba[i];
    }
    return v;
}
// One attachment cleared within rect: a pass of its own whose load operation clears (the view's format converts the value, sRGB included)
static void clear_pass(VkImageView v, VkFormat f, int ds, int cd, int cs, VkRect2D rect, uint32_t layers, VkClearValue val) {
    pass_end();
    VkCommandBuffer cb = vks_cb();
    barrier_all(cb);
    VkRenderingAttachmentInfo a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, v, VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, val};
    VkRenderingAttachmentInfo d = a, s = a;
    d.loadOp = cd ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD; s.loadOp = cs ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    VkImageAspectFlags asp = vkf_aspect(f);
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, rect, layers ? layers : 1, 0, ds ? 0u : 1u, ds ? NULL : &a,
                          ds && (asp & VK_IMAGE_ASPECT_DEPTH_BIT) ? &d : NULL, ds && (asp & VK_IMAGE_ASPECT_STENCIL_BIT) ? &s : NULL};
    vkCmdBeginRendering(cb, &ri); vkCmdEndRendering(cb);
    g_st.clears++;
}
static int scissor_rect(uint32_t w, uint32_t h, VkRect2D *r) {   // the scissor within a w × h target; 0 when empty
    int64_t x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (S->has_sc) { x0 = S->sc.x > 0 ? S->sc.x : 0; y0 = S->sc.y > 0 ? S->sc.y : 0; x1 = (int64_t)S->sc.x + S->sc.w; y1 = (int64_t)S->sc.y + S->sc.h; if (x1 > w) x1 = w; if (y1 > h) y1 = h; }
    if (x1 <= x0 || y1 <= y0) return 0;
    *r = (VkRect2D){{(int32_t)x0, (int32_t)y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
    return 1;
}
// Partial clears (M6 phase B; nvn_mtl.m clear_color_tri / clear_ds_tri): a colour clear that keeps some channels, or a stencil clear that keeps some bits, is a triangle
// over the target level within the scissor: the clear value (converted for integer targets) through the channel mask, or the masked stencil bits replaced, and depth
// written when it is cleared too (the viewport's depth range is the value). One pass per layer of the attachment
typedef struct { uint32_t fmt; uint8_t ds, kind, samples, cmask, dw, sm, pad[2]; } ckey;
static vk_hmap g_cpipe = {sizeof(ckey)};
static VkPipelineLayout g_cpl; static VkShaderModule g_cmod[4];   // vertex; fragment float / uint / int
static VkPipeline clear_pipe(const ckey *k) {
    VkPipeline p = (VkPipeline)vk_hm_get(&g_cpipe, k);
    if (p) return p;
    if (!g_cpl) {
        VkPushConstantRange pr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &pr};
        if (vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_cpl) != VK_SUCCESS) return VK_NULL_HANDLE;
        const uint32_t *src[4] = {clear_vert_spv, clear_f_frag_spv, clear_u_frag_spv, clear_i_frag_spv};
        size_t sz[4] = {sizeof clear_vert_spv, sizeof clear_f_frag_spv, sizeof clear_u_frag_spv, sizeof clear_i_frag_spv};
        for (int i = 0; i < 4; i++) {
            VkShaderModuleCreateInfo mci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sz[i], src[i]};
            if (vkCreateShaderModule(g_vk.dev, &mci, NULL, &g_cmod[i]) != VK_SUCCESS) return VK_NULL_HANDLE;
        }
    }
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_cmod[0], "main", NULL},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_cmod[1 + (k->kind < 3 ? k->kind : 0)], "main", NULL}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                                                 VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, (VkSampleCountFlagBits)(k->samples ? k->samples : 1)};
    VkStencilOpState so = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE, VK_COMPARE_OP_ALWAYS, 0xff, k->sm, 0};
    VkPipelineDepthStencilStateCreateInfo dss = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0, k->dw, k->dw, VK_COMPARE_OP_ALWAYS, VK_FALSE, k->sm != 0, so, so, 0, 1};
    VkPipelineColorBlendAttachmentState ba = {VK_FALSE, 0, 0, 0, 0, 0, 0, k->cmask};
    VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, k->ds ? 0u : 1u, &ba};
    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 3, dyn};
    VkFormat cf = (VkFormat)k->fmt; VkImageAspectFlags asp = k->ds ? vkf_aspect(cf) : 0;
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, k->ds ? 0u : 1u, k->ds ? NULL : &cf,
                                        asp & VK_IMAGE_ASPECT_DEPTH_BIT ? cf : VK_FORMAT_UNDEFINED, asp & VK_IMAGE_ASPECT_STENCIL_BIT ? cf : VK_FORMAT_UNDEFINED};
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri, 0, k->ds ? 1u : 2u, st, &vi, &ia, NULL, &vp, &rs, &ms, &dss, &cbs, &dy, g_cpl,
                                       VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1};
    if (vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &ci, NULL, &p) != VK_SUCCESS) { RED_ONCE(23, "the clear-draw pipeline for format %u could not be made: masked clears are lost", k->fmt); return VK_NULL_HANDLE; }
    vkpc_note_created(1); vk_hm_put(&g_cpipe, k, (void *)p);
    return p;
}
static void clear_draw(vkt_image *im, VkFormat f, uint32_t level, uint32_t l0, uint32_t nl, int ds, int kind, unsigned cmask, int dw, unsigned sm, VkRect2D rect,
                       const uint32_t v[4], float depth, uint32_t stencil) {
    ckey k; memset(&k, 0, sizeof k); k.fmt = (uint32_t)f; k.ds = (uint8_t)ds; k.kind = (uint8_t)kind; k.samples = (uint8_t)im->samples; k.cmask = (uint8_t)cmask; k.dw = (uint8_t)dw; k.sm = (uint8_t)sm;
    VkPipeline p = clear_pipe(&k); if (!p) return;
    pass_end();
    VkCommandBuffer cb = vks_cb();
    barrier_all(cb);
    uint32_t w = vkt_vlw(im, level), h = vkt_vlh(im, level);   // rect is in the image's own pixels
    VkImageAspectFlags asp = ds ? vkf_aspect(f) : VK_IMAGE_ASPECT_COLOR_BIT;
    for (uint32_t layer = 0; layer < (nl ? nl : 1); layer++) {
        VkImageView view = vkt_view_raw_(im, f, VK_IMAGE_VIEW_TYPE_2D, level, 1, l0 + layer, 1, ds ? im->aspect : VK_IMAGE_ASPECT_COLOR_BIT, NULL);
        if (!view) continue;
        VkRenderingAttachmentInfo a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view, VK_IMAGE_LAYOUT_GENERAL, 0, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0}}}};
        VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, rect, 1, 0, ds ? 0u : 1u, ds ? NULL : &a, ds && (asp & VK_IMAGE_ASPECT_DEPTH_BIT) ? &a : NULL,
                              ds && (asp & VK_IMAGE_ASPECT_STENCIL_BIT) ? &a : NULL};
        vkCmdBeginRendering(cb, &ri);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); g_last_pipe = VK_NULL_HANDLE; dc_reset();
        VkViewport vp = {0, 0, (float)w, (float)h, ds ? depth : 0, ds ? depth : 1};
        vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &rect);
        vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, stencil & 0xff);
        uint32_t pc[4] = {0}; if (v) memcpy(pc, v, 16);
        vkCmdPushConstants(cb, g_cpl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
    }
    g_st.clears++;
}
static void clear_color(const p_clear_color *c) {
    if (S->render_skip) return;
    if (!P.valid) pass_targets();
    int i = c->index;
    if (i < 0 || i >= 8 || !P.cv[i]) { if (i < 0 || i >= S->nrt || !S->has_ct[i]) RED_ONCE(20, "colour clear of target %d, which is not set", i); return; }
    unsigned ch = fmt_channels(P.cf[i]);
    if ((c->mask & ch) == 0) return;
    VkRect2D r; if (!scissor_rect(P.cw[i], P.ch[i], &r)) return;
    r = px_rect(P.ci[i], P.clv[i], r, vkt_vlw(P.ci[i], P.clv[i]), vkt_vlh(P.ci[i], P.clv[i]));
    if ((c->mask & ch) != ch) {   // some channels kept: a clear draw through the channel mask (nvn_mtl.m clear_color_tri)
        int kind = fmt_int_kind(P.cf[i]); uint32_t v[4];
        for (int k = 0; k < 4; k++) { float f = c->rgba[k]; int32_t si;   // integer targets take the value converted, as a clear does
            if (kind == 1) v[k] = f > 0 ? (f < 4294967040.f ? (uint32_t)f : 0xffffffffu) : 0;
            else if (kind == 2) { si = !(f == f) ? 0 : f >= 2147483520.f ? INT32_MAX : f <= -2147483648.f ? INT32_MIN : (int32_t)f; memcpy(&v[k], &si, 4); }
            else memcpy(&v[k], &f, 4); }
        { static int n; if (!n++) VKLOG("colour clear with channel mask %#x: drawn through the mask (the first of its kind)", c->mask); }
        clear_draw(P.ci[i], P.cf[i], P.clv[i], P.cl0[i], P.cnl[i], 0, kind, c->mask & ch, 0, 0, r, v, 0, 0);
    } else clear_pass(P.cv[i], P.cf[i], 0, 0, 0, r, P.cnl[i], clear_color_value(P.cf[i], c->rgba, NULL, 0));
    PD("clear c%d ser%llu to %g,%g,%g,%g mask %#x rect %d,%d %ux%u", i, (unsigned long long)P.ci[i]->serial, c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3], c->mask, r.offset.x, r.offset.y, r.extent.width, r.extent.height);
}
static void clear_ds(const p_clear_ds *c) {
    if (S->render_skip) return;
    if (!P.valid) pass_targets();
    if (!P.dv) return;
    int cd = c->depth_mask && P.hd; unsigned sm = (unsigned)c->stencil_mask & 0xff; int cs = sm && P.hs;
    if (!cd && !cs) return;
    VkRect2D r; if (!scissor_rect(P.dw, P.dh, &r)) return;
    r = px_rect(P.di, P.dlv, r, vkt_vlw(P.di, P.dlv), vkt_vlh(P.di, P.dlv));
    VkClearValue v; v.depthStencil.depth = c->depth < 0 ? 0 : c->depth > 1 ? 1 : c->depth; v.depthStencil.stencil = (uint32_t)c->stencil & 0xff;
    if (cs && sm != 0xff) {   // some stencil bits kept: a clear draw replacing the masked bits, and depth when it is cleared too (nvn_mtl.m clear_ds_tri)
        { static int n; if (!n++) VKLOG("stencil clear with mask %#x: drawn through the mask (the first of its kind)", sm); }
        clear_draw(P.di, P.df, P.dlv, P.dl0, P.dnl, 1, 0, 0, cd, sm, r, NULL, v.depthStencil.depth, v.depthStencil.stencil);
    } else clear_pass(P.dv, P.df, 1, cd, cs, r, P.dnl, v);
    PD("clear depth %d (%g) stencil %d (%u)", cd, c->depth, cs, (unsigned)c->stencil);
}

// ---------- Descriptors (nvn_mtl.m encode_args_) ----------
static inline uint64_t tex_slot_h(int st, unsigned reg) {   // nvn_mtl.m tex_slot_h: combined slots past 31 share their c2 word with an image slot
    return reg >= 32 && reg - 32 < DMSH_C2_IMG_SLOTS && S->img[st][reg - 32] ? S->img[st][reg - 32] : reg < NTEX ? S->tex[st][reg] : 0;
}
static VkSampler samp_get(int sid) {
    samp_par p;
    if (sid <= 0 || !vkt_samp_copy(sid, &p)) return R0.samp;
    VkSampler s = vkt_sampler(&p);
    return s ? s : R0.samp;
}
static int nvn_target_of(VkImageViewType vt, int ms) {
    switch (vt) {
    case VK_IMAGE_VIEW_TYPE_1D: return NVN_TEXTURE_TARGET_1D; case VK_IMAGE_VIEW_TYPE_1D_ARRAY: return NVN_TEXTURE_TARGET_1D_ARRAY;
    case VK_IMAGE_VIEW_TYPE_3D: return NVN_TEXTURE_TARGET_3D; case VK_IMAGE_VIEW_TYPE_CUBE: return NVN_TEXTURE_TARGET_CUBEMAP; case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY: return NVN_TEXTURE_TARGET_CUBEMAP_ARRAY;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY: return ms ? NVN_TEXTURE_TARGET_2D_MULTISAMPLE_ARRAY : NVN_TEXTURE_TARGET_2D_ARRAY;
    default: return ms ? NVN_TEXTURE_TARGET_2D_MULTISAMPLE : NVN_TEXTURE_TARGET_2D;
    }
}
static VkImageViewType view_type_of_target(int target, VkImageViewType dflt) {
    switch (target) {
    case NVN_TEXTURE_TARGET_1D: return VK_IMAGE_VIEW_TYPE_1D; case NVN_TEXTURE_TARGET_1D_ARRAY: return VK_IMAGE_VIEW_TYPE_1D_ARRAY; case NVN_TEXTURE_TARGET_3D: return VK_IMAGE_VIEW_TYPE_3D;
    case NVN_TEXTURE_TARGET_2D_ARRAY: case NVN_TEXTURE_TARGET_2D_MULTISAMPLE_ARRAY: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case NVN_TEXTURE_TARGET_CUBEMAP: return VK_IMAGE_VIEW_TYPE_CUBE; case NVN_TEXTURE_TARGET_CUBEMAP_ARRAY: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case NVN_TEXTURE_TARGET_2D: case NVN_TEXTURE_TARGET_2D_MULTISAMPLE: case NVN_TEXTURE_TARGET_RECTANGLE: return VK_IMAGE_VIEW_TYPE_2D;
    }
    return dflt;
}
// The sampled view of texture ID tid as binding b declares it (a 2D texture read as a 2D array and so on: the view's type is the shader's, nvn_mtl.m tex_fit)
// A format of the shader's numeric class (VKS_CT_*) with the same bits: 32-bit components between float, uint and int, 8- and 16-bit integers between uint and
// int (Maxwell hands the shader the register bits; Vulkan wants the view's class to match the declared one). -1 when it already matches or no format keeps the bits
int vkr_fmt_as_ct(int f, int ct) {
    static const int base[4] = {NVN_FORMAT_R8UI, NVN_FORMAT_RG8UI, NVN_FORMAT_RGB8UI, NVN_FORMAT_RGBA8UI};   // 8UI 8I 16F 16 16SN 16UI 16I 32F 32UI 32I
    for (int g = 0; g < 4; g++) {
        int i = f - base[g]; if (i < 0 || i > 9) continue;
        int cls = i == 0 || i == 5 || i == 8 ? VKS_CT_UINT : i == 1 || i == 6 || i == 9 ? VKS_CT_INT : VKS_CT_FLOAT;
        if (cls == ct) return -1;
        if (i >= 7) return base[g] + (ct == VKS_CT_FLOAT ? 7 : ct == VKS_CT_UINT ? 8 : 9);
        if (ct != VKS_CT_FLOAT && cls != VKS_CT_FLOAT) return base[g] + (i <= 1 ? 0 : 5) + (ct == VKS_CT_INT);
        return -1;
    }
    return -1;
}
static const vks_shader *g_cur_sh;   // the shader whose descriptor set write_set is filling (for the messages below)
// F-5: the support buffers with render scales made in this submission (g_sub_no), the last four, for draws whose scales are the same
static struct { uint64_t sub; int vs; float rs[73]; VkBuffer b; VkDeviceSize o; } g_supc[4]; static unsigned g_supn; static uint64_t g_sub_no = 1;
// Depth comparison on a colour texture: the game binds its default 1x1 / 8x8 black and white textures to the shadow samplers of lights without shadows. Vulkan
// leaves that undefined (VUID-vkCmdDrawIndexed-None-06479, in gameplay); the GPU the game was made for compares with the texel's red channel. A 1x1 depth image
// holding the red value of the texture's first texel stands in, one per value (M6), viewed as the shader declares it (M6 phase B: a 2D view under an arrayed or cube
// declaration was VUID-vkCmdDrawIndexed-viewType-07752): six cube-compatible layers, views 2D, 2D array, cube, cube array
typedef struct { float v; VkImage img; vk_alloc mem; VkImageView view[4]; } dcmp_twin;
static dcmp_twin g_dct[8]; static int g_ndct;
static vk_hmap g_dct_of = {8};   // texture serial → its value + 1 (0: not read yet)
static int read_u32(uint64_t a, uint32_t *v);
static VkImageView dcmp_view(vkt_image *im, VkImageViewType vt) {
    int vi = vt == VK_IMAGE_VIEW_TYPE_2D ? 0 : vt == VK_IMAGE_VIEW_TYPE_2D_ARRAY ? 1 : vt == VK_IMAGE_VIEW_TYPE_CUBE ? 2 : vt == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY && g_vk.f10.imageCubeArray ? 3 : -1;
    if (vi < 0) return VK_NULL_HANDLE;
    uint64_t k = im->serial; uintptr_t got = (uintptr_t)vk_hm_get(&g_dct_of, &k);
    float v = 1.0f;
    if (got) memcpy(&v, &(uint32_t){(uint32_t)(got - 1)}, 4);
    else {
        // the first texel: from storage for a texture read from guest memory (its upload may still be in this submission's command buffers: a readback now
        // would see the image before it), else read back; texel (0, 0) is the first bytes in the block-linear layout too
        uint8_t px[16] = {0}; NVNcopyRegion rr = {0, 0, 0, 1, 1, 1}; uint8_t rgba[4] = {255, 0, 0, 0}; int got = -1;
        if (!(im->fi->flags & VKF_COMPRESSED) && im->fi->vbpb <= 16) {
            if (im->cls == VKT_STORAGE && im->mem) { uint32_t w[4]; got = 0; for (unsigned q = 0; q < (im->fi->vbpb + 3) / 4 && !got; q++) got = !read_u32((uint64_t)(uintptr_t)im->mem + 4 * q, &w[q]); if (!got) memcpy(px, w, im->fi->vbpb); got = -got; }
            else got = vkt_readback_region(im, 0, 0, &rr, px, im->fi->vbpb);
        }
        if (!got && !vks_to_rgba8(im->fi->vk, px, 1, rgba)) v = rgba[0] / 255.0f;
        uint32_t bits; memcpy(&bits, &v, 4); vk_hm_put(&g_dct_of, &k, (void *)(uintptr_t)((uintptr_t)bits + 1));
        VKLOG("depth comparison on a colour texture (ser%llu %s %ux%u; shader %s): compared with its red channel, %.3f", (unsigned long long)im->serial, im->fi->name, im->w, im->h,
              g_cur_sh ? g_cur_sh->key : "?", v);
    }
    for (int i = 0; i < g_ndct; i++) if (g_dct[i].v == v) return g_dct[i].view[vi];
    if (g_ndct == 8) return VK_NULL_HANDLE;
    dcmp_twin *t = &g_dct[g_ndct];
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, {1, 1, 1}, 1, 6, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(g_vk.dev, &ci, NULL, &t->img) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_vk.dev, t->img, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &t->mem)) { vkDestroyImage(g_vk.dev, t->img, NULL); return VK_NULL_HANDLE; }
    vkBindImageMemory(g_vk.dev, t->img, t->mem.mem, t->mem.off);
    static const VkImageViewType vts[4] = {VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY};
    for (int i = 0; i < 4; i++) {
        if (i == 3 && !g_vk.f10.imageCubeArray) continue;
        VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, t->img, vts[i], VK_FORMAT_D32_SFLOAT, {0}, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, i < 2 ? 1u : 6u}};
        if (vkCreateImageView(g_vk.dev, &vci, NULL, &t->view[i]) != VK_SUCCESS) return VK_NULL_HANDLE;
    }
    VkCommandBuffer cb = vk_once_begin(); VkImageSubresourceRange sr = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6};
    vk_image_barrier(cb, t->img, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdClearDepthStencilImage(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &(VkClearDepthStencilValue){v, 0}, 1, &sr);
    vk_image_barrier(cb, t->img, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    if (vk_once_end(cb)) return VK_NULL_HANDLE;
    t->v = v; g_ndct++;
    return t->view[vi];
}
static VkImageView tex_view(int tid, const vks_bind *b, VkImageLayout *layout, vkt_image **out_im) {
    vkt_reg r; *out_im = NULL;
    int pool_n = vkt_pool_n();
    if (tid <= 0 || (pool_n && tid >= pool_n) || !vkt_reg_copy(tid, &r) || r.gone) return VK_NULL_HANDLE;
    if (r.t.d.target == NVN_TEXTURE_TARGET_BUFFER) { RED_ONCE(30, "a buffer texture in a slot a shader samples as an image: it reads 0 (nvn_mtl.m: dummy)"); return VK_NULL_HANDLE; }
    vkt_image *im = vkt_resolve(&r.t);
    if (!im) return VK_NULL_HANDLE;
    view_t v = r.has_view ? r.v : (view_t){0}; int use = r.has_view;
    VkImageViewType have = v.set_target ? view_type_of_target(v.target, im->vtype) : im->vtype;
    if (have != b->vtype) { v.set_target = 1; v.target = (uint8_t)nvn_target_of(b->vtype, b->ms); use = 1; }
    if ((have == VK_IMAGE_VIEW_TYPE_3D) != (b->vtype == VK_IMAGE_VIEW_TYPE_3D)) {
        RED_ONCE(31, "a 3D texture read as another type, or the reverse (texture ID %d, ser%llu %ux%ux%u view type %d; shader %s binding %u declares view type %d): it reads 0", tid,
                 (unsigned long long)im->serial, im->w, im->h, im->depth, (int)have, g_cur_sh ? g_cur_sh->key : "?", b->binding, (int)b->vtype);
        return VK_NULL_HANDLE; }
    if (b->depth && !(im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) {
        VkImageView dv = dcmp_view(im, b->vtype);
        if (dv) { *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; return dv; }
    }
    int cf = vkr_fmt_as_ct(v.set_format ? v.format : r.t.d.format, b->ct);
    if (cf >= 0 && !(im->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) { v.set_format = 1; v.format = (uint16_t)cf; use = 1; }
    *layout = im->rest; *out_im = im; td_note(im);
    return vkt_view(im, use ? &v : NULL, r.t.set_swz, r.t.swz);
}
static VkImageView img_view(int tid, const vks_bind *b, float *scale) {   // a storage image: one level, identity swizzle, a format storage allows
    vkt_reg r; *scale = 1.0f;
    if (tid <= 0 || !vkt_reg_copy(tid, &r) || r.gone || r.t.d.target == NVN_TEXTURE_TARGET_BUFFER) return VK_NULL_HANDLE;
    vkt_image *im = vkt_resolve(&r.t);
    if (!im) return VK_NULL_HANDLE;
    if (!(im->usage & VK_IMAGE_USAGE_STORAGE_BIT)) { RED_ONCE(32, "image binding of texture ID %d (format %#x) whose image has no storage use: writes are lost", tid, r.t.d.format); return VK_NULL_HANDLE; }
    // F-5: a compute shader's dispatch covers the game's size, so an image it stores into is drawn at that size (outside a pass; a fragment shader's stores
    // follow its scaled position through TexelFetchScale)
    if (im->vw != im->w || im->vh != im->h) {
        if (!P.open) vkt_unscale(im, vks_cb(), "bound as a storage image");
        else RED_ONCE(35, "a render target drawn larger is a storage image in a draw: kept larger (ser=%llu)", (unsigned long long)im->serial);
        P.valid = P.open;   // its attachment views may have changed
        *scale = im->scale;
    }
    uint32_t level = r.has_view && r.v.set_levels ? r.v.base_level : 0, b0 = r.has_view && r.v.set_layers ? r.v.min_layer : 0;
    VkFormat f = vkf_unorm_of(vkf_view_format(r.t.d.format, r.has_view && r.v.set_format ? r.v.format : r.t.d.format));
    VkImageViewType vt = b->vtype;
    if (vt == VK_IMAGE_VIEW_TYPE_CUBE || vt == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) vt = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    im->content = 1; td_note(im);
    return vkt_view_raw_(im, f, vt, level, 1, b0, vt == VK_IMAGE_VIEW_TYPE_2D || vt == VK_IMAGE_VIEW_TYPE_1D || vt == VK_IMAGE_VIEW_TYPE_3D ? 1 : 0, VK_IMAGE_ASPECT_COLOR_BIT, NULL);
}
static VkBufferView tbuf_view(int tid, int ct) {   // a buffer texture: a texel buffer view over the pool mirror, in the shader's numeric class when the bits allow
    vkt_reg r;
    if (tid <= 0 || !vkt_reg_copy(tid, &r) || r.gone || r.t.d.target != NVN_TEXTURE_TARGET_BUFFER || !r.t.mem) return VK_NULL_HANDLE;
    int cf = vkr_fmt_as_ct(r.t.d.format, ct);
    const vkf_info *fi = vkf_get(cf >= 0 ? cf : r.t.d.format);
    if (!fi || !fi->vk || fi->conv != VKF_COPY) { RED_ONCE(33, "buffer texture format %#x has no texel buffer format", r.t.d.format); return VK_NULL_HANDLE; }
    uint64_t n = (uint64_t)r.t.d.width * fi->bpb; if (!n) return VK_NULL_HANDLE;
    if (n / fi->bpb > g_vk.props.limits.maxTexelBufferElements) n = (uint64_t)g_vk.props.limits.maxTexelBufferElements * fi->bpb;
    VkBuffer b; VkDeviceSize o;
    if (!vkm_use((uint64_t)(uintptr_t)r.t.mem, n, &b, &o)) return VK_NULL_HANDLE;
    if (o % g_vk.props.limits.minTexelBufferOffsetAlignment) { RED_ONCE(34, "buffer texture at an offset Vulkan cannot view (%#llx)", (unsigned long long)o); return VK_NULL_HANDLE; }
    VkBufferViewCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL, 0, b, fi->vk, o, n};
    VkBufferView v = VK_NULL_HANDLE;
    if (vkCreateBufferView(g_vk.dev, &ci, NULL, &v) == VK_SUCCESS) view_keep(v);
    return v;
}
static int c0_ssbo_base(int st) { return st == 0 ? DMSH_C0_SSBO_VS : st == 1 ? DMSH_C0_SSBO_PS : st == 2 ? DMSH_C0_SSBO_GS : st == 5 ? DMSH_C0_SSBO_CS : -1; }
static uint32_t g_draw_binst, g_dispatch_dim[3];
static void put_range(uint8_t *d, unsigned off, uint64_t a, uint64_t n) { if (off + 12 <= DMSH_DRV_SIZE) { uint32_t s = (uint32_t)n; memcpy(d + off, &a, 8); memcpy(d + off + 8, &s, 4); } }
// A driver segment as the shader reads it (dmsh_abi.h): c0 the buffer tables and per-draw words, c2 the handle tables
static void drv_fill(uint8_t *d, const vks_shader *sh, int st, int bank) {
    memset(d, 0, DMSH_DRV_SIZE);
    if (bank == 0) {
        if (sh->s2cs) {   // the Switch 2 compute bank, packed per program: [0x10][uniforms 16 B each][storage 16 B each][textures 8 B each][images 8 B each]
            for (int r = 0; r < 16 && 0x10u + 16u * r + 16 <= sh->s2_ssbo_base; r++) put_range(d, 0x10 + 16 * r, S->ubo[5][r].addr, S->ubo[5][r].size);
            for (int r = 0; r < 16 && sh->s2_ssbo_base + 16u * r + 16 <= sh->s2_tex_base; r++) put_range(d, sh->s2_ssbo_base + 16 * r, S->ssbo[5][r].addr, S->ssbo[5][r].size);
            for (uint32_t r = 0; r < sh->s2_ntex && sh->s2_tex_base + 8 * r + 4 <= DMSH_DRV_SIZE; r++) { uint32_t h = (uint32_t)S->tex[5][r]; memcpy(d + sh->s2_tex_base + 8 * r, &h, 4); }
            for (uint32_t r = 0; r < 8 && sh->s2_tex_base + 8 * (sh->s2_ntex + r) + 4 <= DMSH_DRV_SIZE; r++) { uint32_t h = (uint32_t)S->img[5][r]; memcpy(d + sh->s2_tex_base + 8 * (sh->s2_ntex + r), &h, 4); }
            return;
        }
        int base = c0_ssbo_base(st);
        if (base >= 0) for (int r = 0; r < DMSH_C0_SSBO_SLOTS; r++) put_range(d, base + DMSH_C0_ROW * r, S->ssbo[st][r].addr, S->ssbo[st][r].size);
        if (st != 5) {
            const float max_point = 2047.0f; memcpy(d + DMSH_C0_POINT_SIZE, &max_point, 4);   // Maxwell's point size limit (nvn_mtl.m)
            memcpy(d + DMSH_C0_BASE_INSTANCE, &g_draw_binst, 4);                              // the draw's base instance, which GLSLC adds to the instance ID
        }
        if (st == 1) { d[DMSH_C0_SAMPLE_TABLE] = 0x88; }                                     // one sample at the pixel's centre (nvn_mtl.m c0_sample_rows)
        if (st == 5) {
            memcpy(d + DMSH_C0_DISPATCH, g_dispatch_dim, 12);
            for (int r = 0; r < DMSH_C0_CS_UBO_SLOTS; r++) put_range(d, DMSH_C0_CS_UBO + DMSH_C0_ROW * r, S->ubo[5][r].addr, S->ubo[5][r].size);
        }
    } else {
        const uint64_t *tx = S->tex[st], *im = S->img[st];
        for (int r = 0; r < DMSH_C2_IMG_SLOTS; r++) { uint32_t v = (uint32_t)im[r]; memcpy(d + DMSH_C2_IMG + DMSH_C2_ROW * r, &v, 4); }
        for (int r = 0; r < DMSH_C2_TEX_SLOTS; r++) {
            if (r >= 32 && (!tx[r] || (r - 32 < DMSH_C2_IMG_SLOTS && im[r - 32]))) continue;
            uint32_t v = (uint32_t)tx[r]; memcpy(d + DMSH_C2_TEX + DMSH_C2_ROW * r, &v, 4);
        }
        for (int r = 0; r < DMSH_C2_SEP_SLOTS; r++) { uint32_t v = (uint32_t)tx[NVN_SEPTEX_SLOT0 + r]; memcpy(d + DMSH_C2_SEPTEX + DMSH_C2_ROW * r, &v, 4); }
        for (int r = 0; r < DMSH_C2_SEP_SLOTS; r++) { uint32_t v = (uint32_t)S->samp[st][r]; memcpy(d + DMSH_C2_SAMP + DMSH_C2_ROW * r, &v, 4); }
    }
}
// A uniform range: the pool mirror at its offset (uploaded as needed), else a copy (offsets Vulkan cannot bind), else zeros
static VkDescriptorBufferInfo ubo_info(const p_bind_buf *u) {
    VkDescriptorBufferInfo zero = {R0.zero, 0, ZERO_SIZE};
    if (!u->addr || !u->size) return zero;
    VkDeviceSize n = u->size > g_vk.props.limits.maxUniformBufferRange ? g_vk.props.limits.maxUniformBufferRange : u->size;
    VkBuffer b; VkDeviceSize o;
    if (!vkm_use(u->addr, n, &b, &o)) { RED_ONCE(40, "a uniform buffer at %#llx is in no memory pool: it reads zeros", (unsigned long long)u->addr); return zero; }
    if (o % g_vk.props.limits.minUniformBufferOffsetAlignment) {
        RED_ONCE(41, "a uniform buffer at an address Vulkan cannot bind (%#llx): read from a copy of guest memory", (unsigned long long)u->addr);
        uint8_t *p = arena(n, g_vk.props.limits.minUniformBufferOffsetAlignment, &b, &o);
        if (!p) return zero;
        memcpy(p, (const void *)(uintptr_t)u->addr, (size_t)n);
    }
    return (VkDescriptorBufferInfo){b, o, n};
}
// Quantum's set reuse: the descriptor sets of this submission (g_sub_no) by their content, so a draw whose bindings are those of an earlier one binds
// that set again (no allocation, no update; about six in ten in gameplay). NXRT_VK_SET_REUSE=0: every draw gets a set of its own, as before
typedef struct { uint64_t h1, h2, sub; VkDescriptorSetLayout l; VkDescriptorSet ds; } dsr_e;
enum { DSR_N = 8192 };
static dsr_e g_dsr[DSR_N]; static int g_dsr_on = -1;
static void dsr_hash(VkDescriptorSetLayout l, const VkWriteDescriptorSet *w, int nw, uint64_t *h1, uint64_t *h2) {
    uint64_t a = 1469598103934665603ull ^ (uint64_t)(uintptr_t)l, b = 0x9E3779B97F4A7C15ull + (uint64_t)nw;
    for (int i = 0; i < nw; i++) {
        uint64_t v[4] = {(uint64_t)w[i].dstBinding << 32 | (uint64_t)w[i].descriptorType, 0, 0, 0};
        if (w[i].pBufferInfo) { v[1] = (uint64_t)(uintptr_t)w[i].pBufferInfo->buffer; v[2] = w[i].pBufferInfo->offset; v[3] = w[i].pBufferInfo->range; }
        else if (w[i].pImageInfo) { v[1] = (uint64_t)(uintptr_t)w[i].pImageInfo->sampler; v[2] = (uint64_t)(uintptr_t)w[i].pImageInfo->imageView; v[3] = (uint64_t)w[i].pImageInfo->imageLayout; }
        else if (w[i].pTexelBufferView) v[1] = (uint64_t)(uintptr_t)*w[i].pTexelBufferView;
        for (int j = 0; j < 4; j++) { a = (a ^ v[j]) * 1099511628211ull; a ^= a >> 29; b = (b + v[j]) * 0xC2B2AE3D27D4EB4Full; b ^= b >> 31; }
    }
    *h1 = a; *h2 = b;
}
// Descriptor set of stage st's shader, filled from the current bindings
static VkDescriptorSet write_set(const vks_shader *sh, int st, int *swrites) {
    VkDescriptorSet ds = VK_NULL_HANDLE;   // allocated once the bindings are known, unless an identical set of this submission is bound again
    g_cur_sh = sh;
    if (sh->nb > MAXB) { RED_ONCE(42, "shader %s has %d bindings (more than %d)", sh->key, sh->nb, MAXB); return VK_NULL_HANDLE; }
    VkWriteDescriptorSet w[MAXB]; VkDescriptorBufferInfo bi[MAXB]; VkDescriptorImageInfo ii[MAXB]; VkBufferView bv[MAXB];
    int nw = 0;
    // F-5: the render scales this shader reads (Ryujinx's SupportBuffer.RenderScale): 0 the pass's (the fragment position's divisor), 1.. its textures' and
    // images'. A fragment shader reading a texture of its pass's scale gets it negative: TexelFetchScale then keeps the fragment's place within the texel
    float rs[73]; int rs_any = 0, sup = -1, fs_big = st == VKS_FS && scaled_im(P.ref) && P.any;
    if (sh->reads_scale) for (int k = 0; k < 73; k++) rs[k] = 1.0f;
    for (int i = 0; i < sh->nb; i++) {
        const vks_bind *b = &sh->b[i];
        VkWriteDescriptorSet *x = &w[nw];
        *x = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, b->binding, 0, 1, b->type, NULL, NULL, NULL};
        if (b->count != 1) { RED_ONCE(43, "shader %s: an array binding (%u elements) is not replayed: left unwritten", sh->key, b->count); continue; }
        switch (b->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
            VkDescriptorBufferInfo *o = &bi[nw]; *o = (VkDescriptorBufferInfo){R0.zero, 0, ZERO_SIZE};
            if (b->kind == VKB_SUPPORT) { *o = (VkDescriptorBufferInfo){R0.support, 0, SUPPORT_SIZE}; sup = nw; }
            else if (b->kind == VKB_CBUF) { if (b->reg >= 0 && b->reg < 16) *o = ubo_info(&S->ubo[st][b->reg]); }
            else if (b->kind == VKB_C1) { if (sh->c1buf) *o = (VkDescriptorBufferInfo){sh->c1buf, sh->c1off, (sh->c1_len + 15) & ~15u}; }
            else if (b->kind == VKB_DRV && (b->bank == 0 || b->bank == 2) && sh->drv_read[b->bank]) {
                VkBuffer ab; VkDeviceSize ao; uint8_t *d = arena(DMSH_DRV_SIZE, g_vk.props.limits.minUniformBufferOffsetAlignment, &ab, &ao);
                if (d) { drv_fill(d, sh, st, b->bank); *o = (VkDescriptorBufferInfo){ab, ao, DMSH_DRV_SIZE}; }
            }
            x->pBufferInfo = o; break; }
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
            VkDescriptorBufferInfo *o = &bi[nw]; *o = (VkDescriptorBufferInfo){R0.sink, 0, ZERO_SIZE};
            const p_bind_buf *u = b->kind == VKB_SBUF && b->reg >= 0 && b->reg < 16 ? &S->ssbo[st][b->reg] : NULL;
            VkBuffer bb; VkDeviceSize oo;
            if (u && u->addr && u->size && vkm_use(u->addr, u->size, &bb, &oo)) {
                VkDeviceSize al = g_vk.props.limits.minStorageBufferOffsetAlignment, need = (u->size + 255) & ~(VkDeviceSize)255;
                if (oo % al && st == 5 && R0.bounce && u->size <= BOUNCE_MAX && g_nbnc < 16) {   // a dispatch (no pass open): through the bounce buffer
                    if (g_bnc_next + need > BOUNCE_SIZE) g_bnc_next = 0;
                    VkDeviceSize bo = g_bnc_next; g_bnc_next += need < al ? al : need;
                    VkCommandBuffer cb = vks_cb(); VkBufferCopy c = {oo, bo, u->size}; barrier_all(cb);   // earlier GPU writes of the pool first
                    vkCmdCopyBuffer(cb, bb, R0.bounce, 1, &c);   // dispatch's barrier_all orders it before the shader
                    *o = (VkDescriptorBufferInfo){R0.bounce, bo, u->size};
                    if (b->rw) { g_bnc[g_nbnc].pool = bb; g_bnc[g_nbnc].poff = oo; g_bnc[g_nbnc].boff = bo; g_bnc[g_nbnc].n = u->size; g_nbnc++;
                        if (swrites) { *swrites = 1; int dup = 0; for (int k = 0; k < g_nwb; k++) dup |= g_wb[k].a == u->addr && g_wb[k].n == u->size;
                                       if (!dup && g_nwb < 256) { g_wb[g_nwb].a = u->addr; g_wb[g_nwb].n = u->size; g_nwb++; } } }
                    RED_ONCE(45, "a storage buffer at an address Vulkan cannot bind (%#llx, %#llx bytes, %s; shader %s binding %u): bound through a copy (later ones are not logged)",
                             (unsigned long long)u->addr, (unsigned long long)u->size, b->rw ? "written" : "read only", sh->key, b->binding);
                }
                else if (oo % al) RED_ONCE(44, "a storage buffer at an address Vulkan cannot bind (%#llx, %#llx bytes, %s; shader %s binding %u): not bound",
                                                                                   (unsigned long long)u->addr, (unsigned long long)u->size, b->rw ? "written" : "read only", sh->key, b->binding);
                else {
                    *o = (VkDescriptorBufferInfo){bb, oo, u->size};
                    if (b->rw && swrites) { *swrites = 1; int dup = 0; for (int k = 0; k < g_nwb; k++) dup |= g_wb[k].a == u->addr && g_wb[k].n == u->size;
                        if (!dup && g_nwb < 256) { g_wb[g_nwb].a = u->addr; g_wb[g_nwb].n = u->size; g_nwb++; } }
                }
            }
            x->pBufferInfo = o; break; }
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
            VkDescriptorImageInfo *o = &ii[nw]; *o = (VkDescriptorImageInfo){R0.samp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if (b->kind == VKB_TEX) {
                uint64_t h = tex_slot_h(st, (unsigned)b->reg);
                int tid = (int)(h & 0xfffff), sid = b->samp_kind == VKB_SAMP_SLOT ? (b->samp_reg >= 0 && b->samp_reg < 128 ? (int)((S->samp[st][b->samp_reg] >> 20) & 0xfff) : 0)
                                                  : b->samp_kind == VKB_SAMP_ID ? b->samp_reg : (int)((h >> 20) & 0xfff);
                vkt_image *im;
                o->imageView = tex_view(tid, b, &o->imageLayout, &im);
                o->sampler = samp_get(sid);
                if (!o->imageView) o->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (b->scale_idx > 0 && scaled_im(im)) { rs[b->scale_idx] = fs_big && P.scale == im->scale ? -im->scale : im->scale; rs_any = 1; }
            }
            x->pImageInfo = o; break; }
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
            VkDescriptorImageInfo *o = &ii[nw]; *o = (VkDescriptorImageInfo){VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL};
            if (b->kind == VKB_IMG && b->reg >= 0 && b->reg < 32) { float isc; o->imageView = img_view((int)(S->img[st][b->reg] & 0xfffff), b, &isc); if (swrites) *swrites = 1;
                if (b->scale_idx > 0 && isc != 1.0f) { rs[b->scale_idx] = isc; rs_any = 1; } }
            x->pImageInfo = o; break; }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            bv[nw] = b->kind == VKB_TBUF ? tbuf_view((int)(tex_slot_h(st, (unsigned)b->reg) & 0xfffff), b->ct) : VK_NULL_HANDLE;
            x->pTexelBufferView = &bv[nw]; break; }
        default: continue;
        }
        nw++;
    }
    if (sup >= 0 && sh->reads_scale && (rs_any || fs_big)) {   // this draw's own support buffer: the fixed one with its scales (the same scales share one in a submission)
        if (fs_big) rs[0] = P.rx;
        int vs = st == VKS_VS, hit = -1;
        for (int k = 0; k < 4 && hit < 0; k++) if (g_supc[k].sub == g_sub_no && g_supc[k].vs == vs && !memcmp(g_supc[k].rs, rs, sizeof rs)) hit = k;
        if (hit < 0) {
            VkBuffer ab; VkDeviceSize ao; uint8_t *d = arena(SUPPORT_SIZE, g_vk.props.limits.minUniformBufferOffsetAlignment, &ab, &ao);
            if (d) {
                memcpy(d, R0.support_m.map, SUPPORT_SIZE);
                if (vs) { int32_t one = 1; memcpy(d + 176, &one, 4); }   // a vertex shader's entries start at FragmentRenderScaleCount
                for (int k = 0; k < 73; k++) if (rs[k] != 1.0f) memcpy(d + 192 + 16 * k, &rs[k], 4);
                hit = g_supn++ & 3; g_supc[hit].sub = g_sub_no; g_supc[hit].vs = vs; memcpy(g_supc[hit].rs, rs, sizeof rs); g_supc[hit].b = ab; g_supc[hit].o = ao;
            }
        }
        if (hit >= 0) { bi[sup] = (VkDescriptorBufferInfo){g_supc[hit].b, g_supc[hit].o, SUPPORT_SIZE}; g_st.scaled_sets++; }
    }
    if (g_dsr_on < 0) { const char *e = getenv("NXRT_VK_SET_REUSE"); g_dsr_on = !(e && *e == '0'); }
    uint64_t h1 = 0, h2 = 0; unsigned di = 0;
    if (g_dsr_on) {
        dsr_hash(sh->dsl, w, nw, &h1, &h2); di = (unsigned)(h1 >> 40) & (DSR_N - 1);
        for (int k = 0; k < 8; k++) { dsr_e *e = &g_dsr[(di + k) & (DSR_N - 1)]; if (e->sub != g_sub_no) break;
            if (e->h1 == h1 && e->h2 == h2 && e->l == sh->dsl) { g_st.sets_reused++; return e->ds; } }
    }
    if (!(ds = dset_alloc(sh->dsl))) return VK_NULL_HANDLE;
    for (int i = 0; i < nw; i++) w[i].dstSet = ds;
    vkUpdateDescriptorSets(g_vk.dev, (uint32_t)nw, w, 0, NULL);
    if (g_dsr_on)
        for (int k = 0; k < 8; k++) { dsr_e *e = &g_dsr[(di + k) & (DSR_N - 1)]; if (e->sub != g_sub_no) { *e = (dsr_e){h1, h2, g_sub_no, sh->dsl, ds}; break; } }
    return ds;
}

// ---------- Draws (nvn_mtl.m draw_) ----------
static VkFormatFeatureFlags fmt_features(VkFormat f, int buffer) {
    static vk_hmap m = {8};
    uint64_t k = (uint64_t)f | (uint64_t)buffer << 32;
    void *v = vk_hm_get(&m, &k);
    if (v) return (VkFormatFeatureFlags)((uintptr_t)v >> 1);
    VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(g_vk.pd, f, &p);
    VkFormatFeatureFlags r = buffer ? p.bufferFeatures : p.optimalTilingFeatures;
    vk_hm_put(&m, &k, (void *)(((uintptr_t)r << 1) | 1));
    return r;
}
// The inputs whose format's numeric class differs from the declared one in a way Vulkan cannot fetch bit-exactly: the vertex shader variant that declares them
// so (vksh_vin_variant). 32-bit components need none (an integer format read into a float input keeps the bits, and the reverse)
static const vks_shader *vin_shader(const vks_shader *vs) {
    uint32_t mask = 0; uint8_t ct[32] = {0};
    for (int i = 0; i < vs->nin; i++) {
        int a = vs->in[i].loc, nct; if (a >= 16 || a >= S->nattr || !S->attr[a].fmt) continue;
        int exact; VkFormat f = vkp_vertex_format(S->attr[a].fmt, vs->in[i].ct, &exact);
        if (f && exact) continue;
        if (vkp_vertex_format_natural(S->attr[a].fmt, &nct) && nct != vs->in[i].ct) { mask |= 1u << a; ct[a] = (uint8_t)nct; }
    }
    return mask ? vksh_vin_variant(vs, mask, ct) : vs;
}
static void set_vertex_input(VkCommandBuffer cb, const vks_shader *vs, vkp_gkey *k, uint32_t *used, int *use_zero) {
    VkVertexInputBindingDescription2EXT vb[17]; VkVertexInputAttributeDescription2EXT va[32];
    uint32_t na = 0, nb = 0; *used = 0; *use_zero = 0;
    for (int i = 0; i < vs->nin && na < 32; i++) {
        int a = vs->in[i].loc, ct = vs->in[i].ct;
        VkFormat f = VK_FORMAT_UNDEFINED; int exact = 1, st = 0; uint32_t off = 0;
        if (a < 16 && a < S->nattr && S->attr[a].fmt) {
            f = vkp_vertex_format(S->attr[a].fmt, ct, &exact); st = S->attr[a].stream; off = S->attr[a].offset;
            if (st >= 16) f = VK_FORMAT_UNDEFINED;
            if (f && !(fmt_features(f, 1) & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) { RED_ONCE(50, "vertex format %#x (Vulkan %d) is not a vertex buffer format on this device: the attribute reads zeros", S->attr[a].fmt, f); f = VK_FORMAT_UNDEFINED; }
            if (f && !exact) { g_st.inexact_vertex++; static int n; if (n++ < 8) VKRED("vertex attribute %d: format %#x into a %s input of shader %s cannot keep Maxwell's bits (nvn_mtl.m converts these on the CPU): read as %d",
                                                                                          a, S->attr[a].fmt, ct == VKS_CT_UINT ? "uint" : ct == VKS_CT_INT ? "int" : "float", vs->key, f); }
        }
        if (!f) {   // no attribute: the zero buffer (nvn_mtl.m ZERO_ATTR)
            f = ct == VKS_CT_UINT ? VK_FORMAT_R32G32B32A32_UINT : ct == VKS_CT_INT ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_SFLOAT;
            va[na++] = (VkVertexInputAttributeDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, NULL, (uint32_t)a, 16, f, 0};
            *use_zero = 1; continue;
        }
        va[na++] = (VkVertexInputAttributeDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, NULL, (uint32_t)a, (uint32_t)st, f, off};
        *used |= 1u << st;
    }
    for (int st = 0; st < 16; st++) if (*used >> st & 1) {
        uint32_t div = S->stream[st].divisor;
        vb[nb++] = (VkVertexInputBindingDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, NULL, (uint32_t)st, S->stream[st].stride,
                                                         div ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX, div ? div : 1};
    }
    if (*use_zero) vb[nb++] = (VkVertexInputBindingDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, NULL, 16, 0, VK_VERTEX_INPUT_RATE_VERTEX, 1};
    if (g_vkp_dyn.vertex_input) vkCmdSetVertexInputEXT(cb, nb, vb, na, va);
    else {   // baked into the pipeline
        k->nattr = (uint8_t)(na > 16 ? 16 : na); k->nbind = 0;
        for (uint32_t i = 0; i < k->nattr; i++) { k->attr[i].loc = (uint8_t)va[i].location; k->attr[i].bind = (uint8_t)va[i].binding; k->attr[i].fmt = va[i].format; k->attr[i].off = va[i].offset; }
        for (uint32_t i = 0; i < nb; i++) { uint32_t bi = vb[i].binding; k->bind[bi].stride = vb[i].stride; k->bind[bi].rate = (uint8_t)vb[i].inputRate; k->bind[bi].divisor = vb[i].divisor; if (bi + 1 > k->nbind) k->nbind = (uint8_t)(bi + 1); }
    }
}
static void bind_vertex_buffers(VkCommandBuffer cb, uint32_t used, int use_zero) {   // a stream bound as it already is is not bound again (DC)
    for (int st = 0; st < 16; st++) if (used >> st & 1) {
        NVNbufferRange r = S->vbuf[st]; VkBuffer b = R0.zero; VkDeviceSize o = 0, n = ZERO_SIZE;
        if (r.address && r.size && vkm_use(r.address, r.size, &b, &o)) n = r.size;
        else { b = R0.zero; o = 0; n = ZERO_SIZE; if (r.address) RED_ONCE(51, "vertex stream %d at %#llx is in no memory pool: it reads zeros", st, (unsigned long long)r.address); }
        if (DC.vb[st].b != b || DC.vb[st].o != o || DC.vb[st].n != n) { DC.vb[st].b = b; DC.vb[st].o = o; DC.vb[st].n = n; vkCmdBindVertexBuffers2(cb, (uint32_t)st, 1, &b, &o, &n, NULL); }
    }
    if (use_zero && (DC.vb[16].b != R0.zero || DC.vb[16].o != 0 || DC.vb[16].n != ZERO_SIZE)) {
        VkDeviceSize o = 0, n = ZERO_SIZE; DC.vb[16].b = R0.zero; DC.vb[16].o = 0; DC.vb[16].n = n; vkCmdBindVertexBuffers2(cb, 16, 1, &R0.zero, &o, &n, NULL); }
}
static int read_u32(uint64_t a, uint32_t *v) {   // guest memory, guarded
    __try { memcpy(v, (const void *)(uintptr_t)a, 4); return 1; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return 0; }
}
// Indirect quads (#17, #20, F-22): the GPU writes their arguments earlier in the same submission, so at recording guest memory held 0 or a stale count and the
// effect was not drawn (or kept the GPU busy). A one-thread dispatch (shaders/quad_args.comp) turns them into an indexed indirect draw over R0.quads on the GPU,
// outside the pass: the draw reopens it, loading what the targets hold. 0 when it cannot be made (the draw is skipped)
static VkDescriptorSetLayout g_qadl; static VkPipelineLayout g_qapl; static VkPipeline g_qap;
static int quad_args(uint64_t ind, VkBuffer *ab, VkDeviceSize *ao) {
    if (!g_qap) {
        VkDescriptorSetLayoutBinding lb[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
        VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, lb};
        VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
        if (!g_qadl && vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_qadl) != VK_SUCCESS) return 0;
        VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_qadl, 1, &pr};
        if (!g_qapl && vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_qapl) != VK_SUCCESS) return 0;
        VkShaderModuleCreateInfo mci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof quad_args_comp_spv, quad_args_comp_spv};
        VkShaderModule m = VK_NULL_HANDLE; if (vkCreateShaderModule(g_vk.dev, &mci, NULL, &m) != VK_SUCCESS) return 0;
        VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                                          {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", NULL}, g_qapl, VK_NULL_HANDLE, 0};
        VkResult r = vkCreateComputePipelines(g_vk.dev, g_vk.pcache, 1, &ci, NULL, &g_qap);
        vkDestroyShaderModule(g_vk.dev, m, NULL);
        if (r != VK_SUCCESS) { g_qap = VK_NULL_HANDLE; RED_ONCE(56, "the indirect quads' argument shader could not be made: those draws are skipped"); return 0; }
    }
    VkBuffer sb; VkDeviceSize so;
    if (!vkm_use(ind, 16, &sb, &so)) { RED_ONCE(59, "indirect draw arguments at %#llx are in no memory pool", (unsigned long long)ind); return 0; }
    VkDeviceSize al = g_vk.props.limits.minStorageBufferOffsetAlignment, base = so / al * al;
    if ((so - base) % 4) { RED_ONCE(58, "indirect quads whose arguments are not 4-byte aligned: not drawn"); return 0; }
    VkDescriptorSet ds = dset_alloc(g_qadl);
    if (!ds || !arena(20, al > 16 ? al : 16, ab, ao)) return 0;
    VkDescriptorBufferInfo bi[2] = {{sb, base, so - base + 16}, {*ab, *ao, 20}};
    VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[0], NULL},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[1], NULL}};
    vkUpdateDescriptorSets(g_vk.dev, 2, w, 0, NULL);
    pass_end();
    VkCommandBuffer cb = vks_cb();
    barrier_all(cb);   // the GPU's own write of the arguments, earlier in this submission
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_qap); g_last_pipe = VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_qapl, 0, 1, &ds, 0, NULL);
    uint32_t at = (uint32_t)((so - base) / 4); vkCmdPushConstants(cb, g_qapl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &at);
    vkCmdDispatch(cb, 1, 1, 1);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    g_st.barriers++;
    RED_ONCE(57, "indirect quads: their arguments are turned into an indexed draw on the GPU (at most 65536 quads and instances)");
    return 1;
}

// ---------- NXRT_VK_SKIP=<key>[,<key>...]: draws with any of these shaders in any stage, and dispatches of them, are left out (diagnosis: a tester on another GPU
// switches suspects off one at a time, D-7). Read again at each frame when its value has changed (DM_CTL "env" sets it in a running game); each key's first skip is logged.
// Two lists, the one in use published by index, so the replay never reads one being rewritten ----------
enum { SKIP_MAX = 16 };
static struct { char key[SKIP_MAX][17]; int n; } g_skip[2]; static _Atomic int g_skip_cur;
static _Atomic uint32_t g_skip_said;   // keys whose first skip was logged (by their place in the list)
int vkr_skip_set(const char *list) {
    int nx = !atomic_load(&g_skip_cur), n = 0, bad = 0;
    const char *p = list ? list : "";
    while (*p) {
        while (*p == ',' || *p == ';' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *q = p; while (*q && *q != ',' && *q != ';' && *q != ' ' && *q != '\t') q++;
        int ok = q - p == 16; for (const char *c = p; ok && c < q; c++) ok = isxdigit((unsigned char)*c) != 0;
        if (ok && n < SKIP_MAX) { for (int i = 0; i < 16; i++) g_skip[nx].key[n][i] = (char)tolower((unsigned char)p[i]); g_skip[nx].key[n][16] = 0; n++; }
        else bad = 1;
        p = q;
    }
    g_skip[nx].n = n;
    atomic_store(&g_skip_said, 0); atomic_store(&g_skip_cur, nx);
    return bad ? -1 : n;
}
static int skip_index(const char *key) {   // its place in the list in use, -1 when not there
    int c = atomic_load(&g_skip_cur);
    for (int i = 0; key && i < g_skip[c].n; i++) if (!strcmp(g_skip[c].key[i], key)) return i;
    return -1;
}
int vkr_skip_has(const char *key) { return skip_index(key) >= 0; }
static int skip_these(const vks_shader *const *sh, int n) {   // one of these shaders is on the list: logged once per key
    if (!g_skip[atomic_load(&g_skip_cur)].n) return 0;
    for (int s = 0; s < n; s++) {
        int i = sh[s] ? skip_index(sh[s]->key) : -1;
        if (i < 0) continue;
        if (!(atomic_fetch_or(&g_skip_said, 1u << i) & 1u << i)) VKLOG("NXRT_VK_SKIP: the draws and dispatches of shader %s are left out", sh[s]->key);
        g_st.skip_named++;
        return 1;
    }
    return 0;
}
static void skip_poll(void) {   // at each frame: the variable as it is now
    static char last[512]; const char *e = getenv("NXRT_VK_SKIP"); if (!e) e = "";
    if (!strncmp(last, e, sizeof last - 1)) return;
    snprintf(last, sizeof last, "%s", e);
    int n = vkr_skip_set(e);
    if (n < 0) VKRED("NXRT_VK_SKIP=%s: only shader keys of 16 hex digits are taken (%d kept)", e, g_skip[atomic_load(&g_skip_cur)].n);
    else if (n) VKLOG("NXRT_VK_SKIP: draws and dispatches of %d shader%s left out (%s)", n, n > 1 ? "s" : "", e);
    else VKLOG("NXRT_VK_SKIP: nothing left out");
}

// ---------- Wide view (NXRT_VK_WIDE=1, experimental): the 2D drawn into a presented texture is squeezed into its middle 16:9 ----------
// The present fills a wider window with the game's 16:9 picture (vk_present.c), whose 3D a wider camera drew (host/rt.c); what the game draws into the
// texture it presents, after its 3D, is its 2D (HUD, menus, text, markers), laid out for 16:9: squeezed toward the middle by f = 16:9 / the window's aspect,
// the stretch undoes it. Full-screen draws without vertex inputs (the 3D composite, fades) keep the whole width. Markers over 3D objects near the sides
// drift toward the middle (the game places them for 16:9); its own UI widened to the window is the follow-up
float g_vkr_wide_squeeze = 1.0f;   // vk_present.c: f, 1 = off
static _Atomic uint64_t g_pres[4]; static _Atomic int g_full[4]; static _Atomic unsigned g_npres;   // g_full: a full-screen draw went into it since its last present
void vkr_note_presented(uint64_t serial) {   // gfx_vk.c: the textures the game presents (its window's, two or three)
    if (!serial) return;
    for (int i = 0; i < 4; i++) if (atomic_load(&g_pres[i]) == serial) return;
    unsigned k = atomic_fetch_add(&g_npres, 1) % 4;
    atomic_store(&g_full[k], 1); atomic_store(&g_pres[k], serial);   // its draws so far were not looked at: no bars
}
static int pres_slot(uint64_t serial) {
    for (int i = 0; serial && i < 4; i++) if (atomic_load(&g_pres[i]) == serial) return i;
    return -1;
}
static void wide_squeeze(const vks_shader *vs, uint64_t count, VkViewport *vp, VkRect2D *sc) {
    float f = g_vkr_wide_squeeze;
    int k = f < 0.999f && P.any && P.ncolor && P.ci[0] ? pres_slot(P.ci[0]->serial) : -1;
    if (k < 0) return;
    float cx = P.pw * 0.5f;
    if (vs && !vs->nin && count <= 6 && vp->x <= 0.5f && vp->x + vp->width >= P.pw - 0.5f) {   // the composite or a fade
        if (sc->offset.x <= 0 && (float)sc->offset.x + (float)sc->extent.width >= P.pw - 0.5f) atomic_store(&g_full[k], 1);   // (menus draw a corner of it)
        return;
    }
    vp->x = cx + (vp->x - cx) * f; vp->width *= f;
    float x0 = cx + ((float)sc->offset.x - cx) * f, x1 = cx + ((float)sc->offset.x + (float)sc->extent.width - cx) * f;
    sc->offset.x = (int32_t)floorf(x0); sc->extent.width = (uint32_t)(ceilf(x1) - floorf(x0));
}
// A frame with no full-screen draw (a menu over the whole screen, no 3D behind it) leaves the sides of the texture with older pictures: shown with bars
int vkr_wide_bars(uint64_t serial) {
    int k = pres_slot(serial);
    return k >= 0 && !atomic_exchange(&g_full[k], 0) && g_vkr_wide_squeeze < 0.999f;
}
// A copy of a whole 16:9 picture of a target drawn larger, out to the CPU or into a texture at the game's size, is what the game keeps of what its camera
// saw (a photo, its thumbnail): the picture holds the wider view squeezed into 16:9, so the copy takes its middle 16:9 part, stretched. Copies between
// targets drawn larger (history, refraction) keep the whole view. The source's x range in its own pixels, or 0 when the copy stays as it is
static int wide_unsqueeze(const vkt_image *im, unsigned level, const NVNcopyRegion *r, int32_t *x0, int32_t *x1) {
    float f = g_vkr_wide_squeeze;
    if (!(f < 0.999f) || !scaled_im(im) || level >= im->levels) return 0;
    uint32_t w = mipd(im->w, level), h = mipd(im->h, level);
    if (r->xoffset || r->yoffset || (uint32_t)r->width != w || (uint32_t)r->height != h || fabsf((float)w / (float)h - 16.0f / 9.0f) > 0.01f) return 0;
    float vw = (float)vkt_vlw(im, level);
    *x0 = (int32_t)floorf(vw * (1 - f) * 0.5f + 0.5f); *x1 = (int32_t)floorf(vw * (1 + f) * 0.5f + 0.5f);
    static int n; if (n++ < 8) VKLOG("wide view: a copy of the whole picture of ser=%llu (%ux%u) takes its middle 16:9 part (pixels %d to %d of %.0f)", (unsigned long long)im->serial, w, h, *x0, *x1, vw);
    return 1;
}

static void draw(const p_draw *dw, const p_draw_idx *di) {
    if (S->render_skip) { g_st.draws_skipped++; return; }
    int prim = dw ? dw->prim : di->prim, expand;
    uint64_t ind = dw ? dw->ind : di->ind, mdi = dw ? dw->mdi_cnt : di->mdi_cnt; int mdi_i = dw ? dw->mdi_i : di->mdi_i;
    if (mdi) { uint32_t cnt; if (read_u32(mdi, &cnt) && (uint32_t)mdi_i >= cnt) return;
        RED_ONCE(52, "an indirect-count multi-draw: its count is read from guest memory at replay (a count the GPU writes in the same submission is missed)"); }
    VkPrimitiveTopology topo = vkp_topology(prim, &expand);
    if (expand == VKP_EXPAND_UNSUPPORTED || expand == VKP_EXPAND_LINE_LOOP) { static int n; if (n++ < 4) VKRED("primitive type %d is not drawn", prim); g_st.draws_skipped++; return; }
    if (S->poly.cull == NVN_FACE_FRONT_AND_BACK && !S->rast_discard) return;
    const vks_shader *sh[5] = {0};
    for (int s = 0; s < 5; s++) if (S->bst[s]) sh[s] = prog_stage(S->bst[s], s);
    if (!sh[0] || (S->bst[1] && !sh[1] && !S->rast_discard)) { g_st.skip_no_shader++; g_st.draws_skipped++; return; }
    if (skip_these(sh, 5)) { g_st.draws_skipped++; return; }
    sh[0] = vin_shader(sh[0]);
    if (S->rast_discard) sh[1] = NULL;
    VkBuffer qab = VK_NULL_HANDLE; VkDeviceSize qao = 0;   // indirect quads: the indexed draw's arguments, made on the GPU before the pass opens
    if (dw && ind && expand == VKP_EXPAND_QUADS && !quad_args(ind, &qab, &qao)) { g_st.draws_skipped++; return; }
    // the render pass, its scissor and viewport (nvn_mtl.m draw_: viewport flipped for the lower-left origin there; Vulkan's y axis runs the other way)
    if (!P.open && !pass_begin()) { g_st.skip_no_target++; g_st.draws_skipped++; return; }
    uint32_t tw = P.any ? P.w : (uint32_t)(S->vp[0][0] + S->vp[0][2]), th = P.any ? P.h : (uint32_t)(S->vp[0][1] + S->vp[0][3]);
    VkRect2D sc; if (!scissor_rect(tw, th, &sc)) { if (!S->rast_discard) return; sc = (VkRect2D){{0, 0}, {tw, th}}; }
    VkViewport vp = S->has_vp ? (VkViewport){S->vp[0][0], S->vp[0][1], S->vp[0][2], S->vp[0][3], S->dr[0], S->dr[1]} : (VkViewport){0, 0, (float)tw, (float)th, S->dr[0], S->dr[1]};
    if (vp.width <= 0 || vp.height == 0) return;
    if (P.any && scaled_im(P.ref)) {   // F-5: into the attachments' own pixels
        vp.x *= P.rx; vp.y *= P.ry; vp.width *= P.rx; vp.height *= P.ry;
        sc = px_rect(P.ref, P.ref_lv, sc, P.pw, P.ph);
    }
    wide_squeeze(sh[0], dw ? (uint64_t)dw->count : (uint64_t)di->count, &vp, &sc);
    int flip = !g_origin_ll;
    { uint32_t sw; memcpy(&sw, S->vp_sw[0], 4);
      if (sw == 0x100) flip = !flip;   // NEGATIVE_Y: clip y negated, the viewport flipped about its centre
      else if (sw) RED_ONCE(53, "viewport swizzle %d,%d,%d,%d not implemented (drawn unswizzled)", S->vp_sw[0][0], S->vp_sw[0][1] ^ 2, S->vp_sw[0][2] ^ 4, S->vp_sw[0][3] ^ 6); }
    if (flip) { vp.y += vp.height; vp.height = -vp.height; }
    vp.minDepth = vp.minDepth < 0 ? 0 : vp.minDepth > 1 ? 1 : vp.minDepth; vp.maxDepth = vp.maxDepth < 0 ? 0 : vp.maxDepth > 1 ? 1 : vp.maxDepth;
    VkCommandBuffer cb = vks_cb();
    // M6 phase B: target 0 blending with output 1 as its second source takes the dual-source variant (nvn_mtl.m pso_reads_src1 / fs_dual), an alpha function other than
    // ALWAYS the alpha test variant (nvn_mtl.m atest_key: no test when colour 0 goes to an integer target), with the function and reference as push constants (at_bind)
    int fsv = 0, atest = 0;
    if (sh[1] && P.ncolor && P.cv[0] && (S->chmask.mask & 0xf) && (S->color.blend_en & 1) && (sh[1]->out_mask & 2) && g_vk.f10.dualSrcBlend) {
        blend_st b0 = vkp_adv_blend(S->blend[0]);
        if (vkp_blend_is_src1(b0.src_rgb) || vkp_blend_is_src1(b0.dst_rgb) || vkp_blend_is_src1(b0.src_a) || vkp_blend_is_src1(b0.dst_a)) fsv |= VKSH_FS_DUAL;
    }
    if (sh[1] && S->color.alpha_func && S->color.alpha_func != NVN_ALPHA_FUNC_ALWAYS && (sh[1]->out_mask & 1) && !(P.ncolor && P.cv[0] && fmt_int_kind(P.cf[0]))) fsv |= VKSH_FS_ALPHA;
    if (fsv) { sh[1] = vksh_fs_variant(sh[1], fsv); atest = sh[1]->alpha; }
    // pipeline: what is not dynamic here
    vkp_gkey k; memset(&k, 0, sizeof k);
    for (int s = 0; s < 5; s++) k.sh[s] = sh[s];
    k.ncolor = (uint8_t)P.ncolor; for (int i = 0; i < P.ncolor; i++) k.cfmt[i] = P.cv[i] ? (uint32_t)P.cf[i] : 0;
    k.dfmt = P.dv ? (uint32_t)P.df : 0; k.has_depth = (uint8_t)P.hd; k.has_stencil = (uint8_t)P.hs; k.samples = P.samples;
    k.topo_class = g_vkp_dyn.topo_unrestricted ? VKP_TOPO_ANY : (uint8_t)vkp_topology_class(topo);
    k.depth_m1 = !g_depth_zero;
    VkBool32 ben[8]; VkColorBlendEquationEXT beq[8]; VkColorComponentFlags wm[8];
    const vks_shader *fs = sh[1];
    for (int i = 0; i < P.ncolor; i++) {
        uint32_t m = (S->chmask.mask >> (4 * i)) & 0xf;
        if (!P.cv[i] || !fs || !(fs->out_mask >> i & 1)) m = 0;   // an output the shader does not write: Maxwell leaves the target as it was (nvn_mtl.m pso_desc)
        blend_st bl = vkp_adv_blend(S->blend[i]);
        int en = m && (S->color.blend_en >> i & 1) && (fmt_features(P.cf[i], 0) & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
        if (en && (vkp_blend_is_src1(bl.src_rgb) || vkp_blend_is_src1(bl.dst_rgb) || vkp_blend_is_src1(bl.src_a) || vkp_blend_is_src1(bl.dst_a))) {
            if (i == 0 && fs && fs->dual) { static int n; if (!n++) VKLOG("dual-source blending: shader %s's output 1 is target 0's second source", fs->key); }
            else {   // an unwritten second source counts as zero, as nvn_mtl.m blend_zero_src1
                RED_ONCE(54, "dual-source blending (shader %s, target %d): the second source counts as zero (%s)", fs ? fs->key : "-", i, !fs || !(fs->out_mask & 2) ? "the shader does not write output 1" :
                         !g_vk.f10.dualSrcBlend ? "the device has no dual-source blending" : i ? "only target 0 blends with a second source" : "no dual-source variant");
                bl.src_rgb = vkp_blend_zero_src1(bl.src_rgb); bl.dst_rgb = vkp_blend_zero_src1(bl.dst_rgb); bl.src_a = vkp_blend_zero_src1(bl.src_a); bl.dst_a = vkp_blend_zero_src1(bl.dst_a);
            }
        }
        if (m && fs && fs->out_ct[i] == VKS_CT_FLOAT && fmt_int_kind(P.cf[i])) RED_ONCE(55, "shader %s writes float colour %d into an integer target (nvn_mtl.m fs_uint): the values are undefined", fs->key, i);
        ben[i] = en; wm[i] = m;
        beq[i] = (VkColorBlendEquationEXT){vkp_blend_factor(bl.src_rgb), vkp_blend_factor(bl.dst_rgb), vkp_blend_op(bl.eq_rgb), vkp_blend_factor(bl.src_a), vkp_blend_factor(bl.dst_a), vkp_blend_op(bl.eq_a)};
        k.att[i] = (vkp_att){(uint8_t)(g_vkp_dyn.blend_enable ? 0 : en), (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].srcColorBlendFactor), (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].dstColorBlendFactor),
                             (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].srcAlphaBlendFactor), (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].dstAlphaBlendFactor),
                             (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].colorBlendOp), (uint8_t)(g_vkp_dyn.blend_eq ? 0 : beq[i].alphaBlendOp), (uint8_t)(g_vkp_dyn.write_mask ? 0 : m)};
    }
    int logic = S->color.logic_op != NVN_LOGIC_OP_COPY && g_vk.f10.logicOp;
    if ((fsv & VKSH_FS_ALPHA) && atest) { static int n; if (!n++) VKLOG("alpha test: function %u, reference %g (shader %s's alpha test variant)", S->color.alpha_func, S->alpha_ref, fs->key); }
    if (!g_vkp_dyn.logic_op_enable) k.logic_en = (uint8_t)logic;
    if (!g_vkp_dyn.logic_op) k.logic_op = (uint8_t)(logic ? vkp_logic_op(S->color.logic_op) : VK_LOGIC_OP_COPY);
    if (!g_vkp_dyn.a2c) k.a2c = S->ms.a2c != 0;
    if (!g_vkp_dyn.polygon_mode) k.poly = (uint8_t)S->poly.mode;
    if (!g_vkp_dyn.depth_clamp) k.depth_clamp = S->depth_clamp != 0 && g_vk.f10.depthClamp;
    uint32_t used; int use_zero;
    set_vertex_input(cb, sh[0], &k, &used, &use_zero);
    VkPipelineLayout layout;
    VkPipeline pipe = vkp_graphics(&k, &layout);
    if (!pipe) { g_st.skip_no_pipeline++; g_st.draws_skipped++; return; }
    if (pipe != g_last_pipe) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe); g_last_pipe = pipe; }
    if (atest) { uint32_t pc[2] = {S->color.alpha_func, 0}; memcpy(&pc[1], &S->alpha_ref, 4); vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, VKP_ALPHA_PC_SIZE, pc); }
    // dynamic state, only what changed since the last draw recorded into this command buffer (DC)
    if (g_dc_on < 0) { const char *e = getenv("NXRT_VK_STATE_CACHE"); g_dc_on = !(e && *e == '0'); }
    if (cb != DC.cb || !g_dc_on) { dc_reset(); DC.cb = cb; }
    if (dc_chg(&DC.vp, &vp, sizeof vp)) vkCmdSetViewportWithCount(cb, 1, &vp);
    if (dc_chg(&DC.sc, &sc, sizeof sc)) vkCmdSetScissorWithCount(cb, 1, &sc);
    { VkBool32 one = 1; if (dc_chg(&DC.fixed, &one, sizeof one)) { vkCmdSetLineWidth(cb, 1.0f); vkCmdSetDepthBoundsTestEnable(cb, VK_FALSE); } }
    { VkCullModeFlags v = vkp_cull(S->poly.cull); if (dc_chg(&DC.cull, &v, sizeof v)) vkCmdSetCullMode(cb, v); }
    { VkFrontFace v = vkp_front_face(S->poly.front, g_queue_origin_ll); if (dc_chg(&DC.ff, &v, sizeof v)) vkCmdSetFrontFace(cb, v); }
    if (dc_chg(&DC.topo, &topo, sizeof topo)) vkCmdSetPrimitiveTopology(cb, topo);
    int cls = vkp_topology_class(topo), pm = S->poly.mode;
    int bias = (S->poly.offset_en & (cls == 0 || pm == NVN_POLYGON_MODE_POINT ? 1 : cls == 1 || pm == NVN_POLYGON_MODE_LINE ? 2 : 4)) != 0;
    { VkBool32 v = bias; if (dc_chg(&DC.bias_en, &v, sizeof v)) vkCmdSetDepthBiasEnable(cb, v); }
    // F-5: a depth slope per pixel is smaller in a pass drawn larger: the slope factor grows with it, so the offset stays the game's
    { float v[3] = {S->po.units, g_vk.f10.depthBiasClamp ? S->po.clamp : 0.0f, P.any && scaled_im(P.ref) ? S->po.factor * P.rx : S->po.factor};
      if (dc_chg(DC.bias, v, sizeof v)) vkCmdSetDepthBias(cb, v[0], v[1], v[2]); }
    if (dc_chg(DC.bc, S->blend_color, sizeof DC.bc)) vkCmdSetBlendConstants(cb, S->blend_color);
    { VkBool32 v = P.hd && S->ds.dt; if (dc_chg(&DC.dt, &v, sizeof v)) vkCmdSetDepthTestEnable(cb, v); }
    { VkBool32 v = P.hd && S->ds.dw; if (dc_chg(&DC.dw, &v, sizeof v)) vkCmdSetDepthWriteEnable(cb, v); }
    { VkCompareOp v = vkp_compare(S->ds.df); if (dc_chg(&DC.dcmp, &v, sizeof v)) vkCmdSetDepthCompareOp(cb, v); }
    { VkBool32 v = P.hs && S->ds.st; if (dc_chg(&DC.st_en, &v, sizeof v)) vkCmdSetStencilTestEnable(cb, v); }
    { VkStencilOp v[3] = {vkp_stencil_op(S->ds.fail_f), vkp_stencil_op(S->ds.zpass_f), vkp_stencil_op(S->ds.zfail_f)}; VkCompareOp c = vkp_compare(S->ds.sf_f);
      int ch = dc_chg(DC.sop[0], v, sizeof v); ch |= dc_chg(&DC.scmp[0], &c, sizeof c); if (ch) vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_BIT, v[0], v[1], v[2], c); }
    { VkStencilOp v[3] = {vkp_stencil_op(S->ds.fail_b), vkp_stencil_op(S->ds.zpass_b), vkp_stencil_op(S->ds.zfail_b)}; VkCompareOp c = vkp_compare(S->ds.sf_b);
      int ch = dc_chg(DC.sop[1], v, sizeof v); ch |= dc_chg(&DC.scmp[1], &c, sizeof c); if (ch) vkCmdSetStencilOp(cb, VK_STENCIL_FACE_BACK_BIT, v[0], v[1], v[2], c); }
    for (int f = 0; f < 2; f++) {
        VkStencilFaceFlags fl = f ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;
        uint32_t cm = S->st_vmask[f] & 0xff, wmk = S->st_wmask[f] & 0xff, rf = S->st_ref[f] & 0xff;
        if (dc_chg(&DC.scm[f], &cm, sizeof cm)) vkCmdSetStencilCompareMask(cb, fl, cm);
        if (dc_chg(&DC.swm[f], &wmk, sizeof wmk)) vkCmdSetStencilWriteMask(cb, fl, wmk);
        if (dc_chg(&DC.sref[f], &rf, sizeof rf)) vkCmdSetStencilReference(cb, fl, rf);
    }
    { VkBool32 v = S->rast_discard != 0; if (dc_chg(&DC.rd, &v, sizeof v)) vkCmdSetRasterizerDiscardEnable(cb, v); }
    int restart = S->restart.on && di;
    if (restart && ((di->itype == NVN_INDEX_TYPE_UNSIGNED_BYTE && S->restart.index != 0xff) || (di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT && S->restart.index != 0xffff) || (di->itype == NVN_INDEX_TYPE_UNSIGNED_INT && S->restart.index != 0xffffffffu)))
        RED_ONCE(57, "primitive restart index %#x is not the index type's largest value: Vulkan restarts only there", S->restart.index);
    if (restart && (cls != 1 && cls != 2 ? 1 : topo == VK_PRIMITIVE_TOPOLOGY_LINE_LIST || topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || topo == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) && !g_vk.has.prim_list_restart) restart = 0;
    { VkBool32 v = restart; if (dc_chg(&DC.pr, &v, sizeof v)) vkCmdSetPrimitiveRestartEnable(cb, v); }
    if (P.ncolor) {
        uint32_t nc = (uint32_t)P.ncolor; int same = DC.ncol == nc;
        if (g_vkp_dyn.blend_enable && (!same || memcmp(DC.ben, ben, nc * sizeof *ben))) { memcpy(DC.ben, ben, nc * sizeof *ben); vkCmdSetColorBlendEnableEXT(cb, 0, nc, ben); }
        if (g_vkp_dyn.blend_eq && (!same || memcmp(DC.beq, beq, nc * sizeof *beq))) { memcpy(DC.beq, beq, nc * sizeof *beq); vkCmdSetColorBlendEquationEXT(cb, 0, nc, beq); }
        if (g_vkp_dyn.write_mask && (!same || memcmp(DC.wm, wm, nc * sizeof *wm))) { memcpy(DC.wm, wm, nc * sizeof *wm); vkCmdSetColorWriteMaskEXT(cb, 0, nc, wm); }
        DC.ncol = nc;
    }
    if (g_vkp_dyn.logic_op_enable) { VkBool32 v = logic; if (dc_chg(&DC.logic_en, &v, sizeof v)) vkCmdSetLogicOpEnableEXT(cb, v); }
    if (g_vkp_dyn.logic_op) { VkLogicOp v = logic ? vkp_logic_op(S->color.logic_op) : VK_LOGIC_OP_COPY; if (dc_chg(&DC.lop, &v, sizeof v)) vkCmdSetLogicOpEXT(cb, v); }
    if (g_vkp_dyn.a2c) { VkBool32 v = S->ms.a2c != 0; if (dc_chg(&DC.a2c, &v, sizeof v)) vkCmdSetAlphaToCoverageEnableEXT(cb, v); }
    if (g_vkp_dyn.polygon_mode) { VkPolygonMode v = vkp_polygon_mode(S->poly.mode); if (dc_chg(&DC.pm, &v, sizeof v)) vkCmdSetPolygonModeEXT(cb, v); }
    if (g_vkp_dyn.depth_clamp) { VkBool32 v = S->depth_clamp != 0 && g_vk.f10.depthClamp; if (dc_chg(&DC.dclamp, &v, sizeof v)) vkCmdSetDepthClampEnableEXT(cb, v); }
    bind_vertex_buffers(cb, used, use_zero);
    // the draw's base instance for c0 + 0x644 (an indirect draw's from its record in guest memory)
    g_draw_binst = (uint32_t)(dw ? dw->base_instance : di->base_instance);
    if (ind) { uint32_t bi; if (read_u32(ind + (dw ? 12 : 16), &bi)) g_draw_binst = bi; }
    int swrites = 0;
    for (int s = 0; s < 5; s++) if (sh[s]) {
        VkDescriptorSet ds = write_set(sh[s], s, &swrites);
        if (!ds) { g_st.draws_skipped++; return; }
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)sh[s]->set, 1, &ds, 0, NULL);
    }
    if (g_pd) {
        char b[400]; int o = 0;
        for (int i = 0; i < P.ncolor; i++) o += snprintf(b + o, sizeof b - o, " a%d{en%d wm%x %d,%d,%d/%d,%d,%d raw%#llx}", i, (int)ben[i], (unsigned)wm[i], beq[i].srcColorBlendFactor, beq[i].dstColorBlendFactor, beq[i].colorBlendOp,
                                                         beq[i].srcAlphaBlendFactor, beq[i].dstAlphaBlendFactor, beq[i].alphaBlendOp, (unsigned long long)*(const uint64_t *)&S->blend[i]);
        PD("draw vs=%s fs=%s prim=%d %s count=%d inst=%u first=%d base_v=%d ind=%#llx | depth t%d w%d f%d stencil %d cull %d front %d | vp %g,%g %gx%g sc %d,%d %ux%u |%s",
           sh[0]->key, sh[1] ? sh[1]->key : "-", prim, di ? (di->itype == 0 ? "u8" : di->itype == 1 ? "u16" : "u32") : "-", dw ? dw->count : di->count,
           (unsigned)((dw ? dw->instances : di->instances) > 0 ? (dw ? dw->instances : di->instances) : 1), dw ? dw->first : 0, di ? di->base_vertex : 0, (unsigned long long)ind,
           P.hd && S->ds.dt, P.hd && S->ds.dw, (int)S->ds.df, P.hs && S->ds.st, (int)S->poly.cull, (int)S->poly.front, vp.x, vp.y, vp.width, vp.height, sc.offset.x, sc.offset.y, sc.extent.width, sc.extent.height, b);
        for (int s = 0; s < 5; s++) if (sh[s]) for (int i = 0; i < sh[s]->nb; i++) {
            const vks_bind *bb = &sh[s]->b[i];
            if (bb->kind == VKB_TEX || bb->kind == VKB_TBUF || bb->kind == VKB_IMG) { uint64_t h = bb->kind == VKB_IMG ? S->img[s][bb->reg] : tex_slot_h(s, (unsigned)bb->reg); vkt_reg r; int tid = (int)(h & 0xfffff);
                int ok = tid > 0 && vkt_reg_copy(tid, &r); vkt_image *im = ok ? vkt_by_serial(r.t.serial) : NULL;
                samp_par sp = {0}; int sok = vkt_samp_copy((int)((h >> 20) & 0xfff), &sp);
                if (sok) PD("    sampler min%d mag%d wrap%d,%d,%d lod %g..%g bias %g aniso %g cmp %d/%d red %d", sp.minf, sp.magf, sp.ws, sp.wt, sp.wr, sp.lod_min, sp.lod_max, sp.lod_bias, sp.aniso, sp.cmp_mode, sp.cmp_func, sp.reduction);
                PD("  st%d b%u %s slot%d tid=%d sid=%d %s ser%llu fmt%#x %ux%ux%u tgt%d lv%u view%d/%d %s", s, bb->binding, bb->kind == VKB_TEX ? "tex" : bb->kind == VKB_TBUF ? "tbuf" : "img", bb->reg, tid, (int)((h >> 20) & 0xfff),
                   ok ? "" : "(none)", ok ? (unsigned long long)r.t.serial : 0ull, ok ? r.t.d.format : 0, ok ? r.t.d.width : 0, ok ? r.t.d.height : 0, ok ? r.t.d.depth : 0, ok ? r.t.d.target : 0, ok ? r.t.d.levels : 0,
                   (int)bb->vtype, ok && r.has_view ? (int)r.v.target : -1, im ? (im->cls == VKT_STORAGE ? "storage" : "gpu") : "-");
                if (ok) PD("    storage pool %p +%#llx mem %p flags %#x layout %d size %#llx image %p", (const void *)r.t.pool, (unsigned long long)r.t.off, (const void *)r.t.mem, r.t.d.flags, r.t.has_layout, (unsigned long long)r.t.L.size, (void *)im); }
            else if (bb->kind == VKB_CBUF || bb->kind == VKB_SBUF) { const p_bind_buf *u = bb->kind == VKB_CBUF ? &S->ubo[s][bb->reg] : &S->ssbo[s][bb->reg];
                float f[8] = {0}; if (u->addr && u->size >= 32) memcpy(f, (const void *)(uintptr_t)u->addr, 32); else if (u->addr && u->size >= 16) memcpy(f, (const void *)(uintptr_t)u->addr, 16);
                PD("  st%d b%u %s slot%d addr=%#llx size=%llu first %g,%g,%g,%g %g,%g,%g,%g", s, bb->binding, bb->kind == VKB_CBUF ? "cbuf" : "sbuf", bb->reg, (unsigned long long)u->addr, (unsigned long long)u->size, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]); }
        }
        if (di && di->count > 0 && di->count <= 12) { char t[200]; int o = 0; int isz = di->itype == 0 ? 1 : di->itype == 1 ? 2 : 4;
            for (int k = 0; k < di->count; k++) { uint32_t x = 0; memcpy(&x, (const uint8_t *)(uintptr_t)di->ibuf + (size_t)k * isz, (size_t)isz); o += snprintf(t + o, sizeof t - o, " %u", x); }
            PD("  indices at %#llx:%s", (unsigned long long)di->ibuf, t); }
        if (sh[0] && S->nattr && S->attr[0].fmt == 0x22) { int st = S->attr[0].stream; const uint8_t *vb = (const uint8_t *)(uintptr_t)S->vbuf[st].address; char t[300]; int o = 0;
            for (int k = 0; k < 4 && vb && (uint64_t)(k * S->stream[st].stride + 12) <= S->vbuf[st].size; k++) { float f[3]; memcpy(f, vb + k * S->stream[st].stride + S->attr[0].offset, 12); o += snprintf(t + o, sizeof t - o, " (%g,%g,%g)", f[0], f[1], f[2]); }
            PD("  positions:%s", t); }
        if (sh[0]) for (int i = 0; i < sh[0]->nin; i++) { int a = sh[0]->in[i].loc; if (a >= S->nattr) continue;
            int st = S->attr[a].stream & 15; uint32_t v0 = 0; char hx[40] = "-";   // the first vertex's bytes of the attribute
            if (di && di->ibuf && !di->ind && di->count > 0) { int isz = di->itype == 0 ? 1 : di->itype == 1 ? 2 : 4; memcpy(&v0, (const uint8_t *)(uintptr_t)di->ibuf, (size_t)isz); v0 += (uint32_t)di->base_vertex; }
            else if (dw) v0 = (uint32_t)dw->first;
            uint64_t at = (uint64_t)v0 * S->stream[st].stride + S->attr[a].offset;
            if (S->vbuf[st].address && at + 16 <= S->vbuf[st].size) { const uint8_t *p = (const uint8_t *)(uintptr_t)S->vbuf[st].address + at; for (int k = 0; k < 16; k++) snprintf(hx + 2 * k, 3, "%02x", p[k]); }
            PD("  in%d fmt%#x off%u stream%u stride%u div%u vb=%#llx/%llu ct%d v%u %s", a, S->attr[a].fmt, S->attr[a].offset, S->attr[a].stream,
            S->stream[st].stride, S->stream[st].divisor, (unsigned long long)S->vbuf[st].address, (unsigned long long)S->vbuf[st].size, sh[0]->in[i].ct, v0, hx); }
    }
    if (S->vis_on && P.query < 0) { int q = q_alloc(); if (q >= 0) { vkCmdBeginQuery(cb, R0.qp, (uint32_t)q, 0); P.query = q; g_st.queries++; } }
    // the draw
    uint32_t inst = (uint32_t)((dw ? dw->instances : di->instances) > 0 ? (dw ? dw->instances : di->instances) : 1);
    vk_checkpoint(cb, 'd', sh[1] ? sh[1]->key : sh[0]->key, inst, 0, 0, ind != 0, NULL);
    if (dw) {
        if (ind && expand == VKP_EXPAND_QUADS) { vkCmdBindIndexBuffer(cb, R0.quads, 0, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexedIndirect(cb, qab, qao, 1, 20); }   // quad_args
        else if (ind) { VkBuffer b; VkDeviceSize o;
            if (!vkm_use(ind, 16, &b, &o)) { RED_ONCE(59, "indirect draw arguments at %#llx are in no memory pool", (unsigned long long)ind); return; }
            vkCmdDrawIndirect(cb, b, o, 1, 16); }
        else if (expand == VKP_EXPAND_QUADS) { if (dw->count >= 4) { vkCmdBindIndexBuffer(cb, R0.quads, 0, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, (uint32_t)(dw->count / 4) * 6, inst, 0, dw->first, (uint32_t)dw->base_instance); } }
        else if (dw->count > 0) vkCmdDraw(cb, (uint32_t)dw->count, inst, (uint32_t)dw->first, (uint32_t)dw->base_instance);
    } else {
        int isz = di->itype == NVN_INDEX_TYPE_UNSIGNED_BYTE ? 1 : di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT ? 2 : 4;
        VkIndexType it = isz == 1 ? VK_INDEX_TYPE_UINT8_EXT : isz == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        if (isz == 1 && !g_vk.has.index_u8) { RED_ONCE(60, "8-bit indices without VK_EXT_index_type_uint8: not drawn"); return; }
        VkBuffer b; VkDeviceSize o;
        if (expand == VKP_EXPAND_QUADS) {   // indexed quads: expanded from guest memory into a triangle list (nvn_mtl.m nvn_expand_primitive)
            if (ind || di->count < 4) { if (ind) RED_ONCE(61, "indirect indexed quads are not drawn"); return; }
            uint32_t nq = (uint32_t)di->count / 4; uint32_t *d = arena((VkDeviceSize)nq * 24, 16, &b, &o); if (!d) return;
            const uint8_t *src = (const uint8_t *)(uintptr_t)di->ibuf;
            for (uint32_t q = 0; q < nq; q++) { uint32_t v[4]; for (int j = 0; j < 4; j++) { uint32_t x = 0; memcpy(&x, src + (size_t)(4 * q + j) * isz, (size_t)isz); v[j] = x; }
                d[6 * q] = v[0]; d[6 * q + 1] = v[1]; d[6 * q + 2] = v[2]; d[6 * q + 3] = v[0]; d[6 * q + 4] = v[2]; d[6 * q + 5] = v[3]; }
            vkCmdBindIndexBuffer(cb, b, o, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, nq * 6, inst, 0, di->base_vertex, (uint32_t)di->base_instance);
        } else {
            uint32_t icount = (uint32_t)(di->count > 0 ? di->count : 1);
            if (ind && !read_u32(ind, &icount)) icount = 4096;   // an indirect draw's index count, as guest memory holds it now
            uint64_t n = (uint64_t)(icount ? icount : 1) * isz;
            if (!vkm_use(di->ibuf, n, &b, &o) && !vkm_use(di->ibuf, (uint64_t)isz, &b, &o)) { RED_ONCE(62, "index buffer at %#llx is in no memory pool", (unsigned long long)di->ibuf); return; }
            if (o % isz) { RED_ONCE(63, "index buffer at an address not aligned to its index size: not drawn"); return; }
            vkCmdBindIndexBuffer(cb, b, o, it);
            if (ind) { VkBuffer ab; VkDeviceSize ao; if (!vkm_use(ind, 20, &ab, &ao)) { RED_ONCE(59, "indirect draw arguments are in no memory pool"); return; } vkCmdDrawIndexedIndirect(cb, ab, ao, 1, 20); }
            else if (di->count > 0) vkCmdDrawIndexed(cb, (uint32_t)di->count, inst, 0, di->base_vertex, (uint32_t)di->base_instance);
        }
    }
    g_st.draws++;
    (void)swrites;
    if (g_nwb >= 240) pass_end();   // the write-back list is nearly full: record the copies now
}

// ---------- Compute (nvn_mtl.m dispatch) ----------
static void dispatch(const p_dispatch *dp) {
    const vks_shader *cs = S->cprog ? prog_stage(S->cprog, VKS_CS) : NULL;
    if (!cs) { g_st.skip_no_shader++; return; }
    if (skip_these(&cs, 1)) return;
    VkPipelineLayout layout; VkPipeline pipe = vkp_compute(cs, &layout);
    if (!pipe) return;
    pass_end();
    VkCommandBuffer cb = vks_cb();
    g_dispatch_dim[0] = (uint32_t)dp->x; g_dispatch_dim[1] = (uint32_t)dp->y; g_dispatch_dim[2] = (uint32_t)dp->z;
    if (dp->ind) for (int i = 0; i < 3; i++) read_u32(dp->ind + 4 * i, &g_dispatch_dim[i]);   // for c0's dispatch words: the record as guest memory holds it now
    int swrites = 0;
    VkDescriptorSet ds = write_set(cs, 5, &swrites);
    if (!ds) return;
    barrier_all(cb);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe); g_last_pipe = VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &ds, 0, NULL);
    if (g_pd) {
        PD("dispatch cs=%s %ux%ux%u ind=%#llx local %ux%ux%u", cs->key, g_dispatch_dim[0], g_dispatch_dim[1], g_dispatch_dim[2], (unsigned long long)dp->ind, cs->local[0], cs->local[1], cs->local[2]);
        for (int i = 0; i < cs->nb; i++) { const vks_bind *bb = &cs->b[i];
            if (bb->kind == VKB_SBUF || bb->kind == VKB_CBUF) { const p_bind_buf *u = bb->kind == VKB_CBUF ? &S->ubo[5][bb->reg] : &S->ssbo[5][bb->reg]; PD("  b%u %s slot%d addr=%#llx size=%llu rw%d", bb->binding, bb->kind == VKB_CBUF ? "cbuf" : "sbuf", bb->reg, (unsigned long long)u->addr, (unsigned long long)u->size, bb->rw); }
            else if (bb->kind == VKB_TEX || bb->kind == VKB_IMG || bb->kind == VKB_TBUF) { uint64_t h = bb->kind == VKB_IMG ? S->img[5][bb->reg] : tex_slot_h(5, (unsigned)bb->reg); vkt_reg r; int tid = (int)(h & 0xfffff); int ok = tid > 0 && vkt_reg_copy(tid, &r);
                PD("  b%u %s slot%d tid=%d ser%llu fmt%#x %ux%u tgt%d", bb->binding, bb->kind == VKB_TEX ? "tex" : bb->kind == VKB_IMG ? "img" : "tbuf", bb->reg, tid, ok ? (unsigned long long)r.t.serial : 0ull, ok ? r.t.d.format : 0, ok ? r.t.d.width : 0, ok ? r.t.d.height : 0, ok ? r.t.d.target : 0); }
            else if (bb->kind == VKB_DRV) PD("  b%u drv c%d read %d (s2 %d ssbo %u tex %u n %u)", bb->binding, bb->bank, cs->drv_read[bb->bank], cs->s2cs, cs->s2_ssbo_base, cs->s2_tex_base, cs->s2_ntex);
        }
    }
    uint32_t ckw[16], *ckp = NULL;   // a lost device's report shows the dispatch's first constants as recorded (loop counts: D-1's word 0, D-11's words 12 and 13)
    if (g_vk.has.checkpoints || g_vk.has.buffer_marker) for (int i = 0; i < cs->nb && !ckp; i++) { const p_bind_buf *u = cs->b[i].kind == VKB_CBUF && cs->b[i].reg >= 0 && cs->b[i].reg < 16 ? &S->ubo[5][cs->b[i].reg] : NULL;
        if (u && u->addr && u->size >= 32) { memset(ckw, 0, sizeof ckw); memcpy(ckw, (const void *)(uintptr_t)u->addr, u->size < sizeof ckw ? (size_t)u->size : sizeof ckw); ckp = ckw; } }
    vk_checkpoint(cb, 'c', cs->key, g_dispatch_dim[0], g_dispatch_dim[1], g_dispatch_dim[2], dp->ind != 0, ckp);
    if (dp->ind) { VkBuffer b; VkDeviceSize o; if (!vkm_use(dp->ind, 12, &b, &o)) { RED_ONCE(70, "indirect dispatch arguments are in no memory pool"); return; } vkCmdDispatchIndirect(cb, b, o); }
    else if (dp->x > 0 && dp->y > 0 && dp->z > 0) vkCmdDispatch(cb, (uint32_t)dp->x, (uint32_t)dp->y, (uint32_t)dp->z);
    g_st.dispatches++;
    if (g_nbnc) {   // written bounced storage buffers back into their pools, before the write-back to guest memory below reads them
        barrier_all(cb);
        for (int i = 0; i < g_nbnc; i++) { VkBufferCopy c = {g_bnc[i].boff, g_bnc[i].poff, g_bnc[i].n}; vkCmdCopyBuffer(cb, R0.bounce, g_bnc[i].pool, 1, &c); }
        g_nbnc = 0;
        if (!g_nwb) barrier_all(cb);
    }
    if (g_nwb) { barrier_all(cb); wb_flush(); }
}

// ---------- Buffer operations in GPU order (the null backend's and track B's effects, now inside the replay's ordering) ----------
void vkr_copy(uint64_t src, uint64_t dst, uint64_t n) {
    g_st.copies++;
    VkBuffer sb, db; VkDeviceSize so, dof;
    VkCommandBuffer cb = vks_cb();
    if (!cb || !n || !vkm_use(src, n, &sb, &so) || !vkm_use(dst, n, &db, &dof)) {   // not two pool ranges: on the CPU, as the null backend does it
        if (vkm_addr(src, n, NULL, NULL) && vkm_addr(dst, n, NULL, NULL)) { dm_guest_write_prepare((void *)(uintptr_t)dst, (size_t)n); memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, (size_t)n); }
        return;
    }
    pass_end(); barrier_all(cb);
    VkBufferCopy c = {so, dof, n};
    vkCmdCopyBuffer(cb, sb, db, 1, &c);
    vkm_gpu_wrote(dst, n);
}
void vkr_fill(uint64_t dst, uint64_t n, uint32_t value) {
    g_st.fills++;
    VkBuffer db; VkDeviceSize dof; VkCommandBuffer cb = vks_cb();
    if (!n) return;
    if (!cb || (dst & 3) || (n & 3) || !vkm_use(dst, n, &db, &dof)) {
        if (!vkm_addr(dst, n, NULL, NULL)) return;
        dm_guest_write_prepare((void *)(uintptr_t)dst, (size_t)n);
        uint8_t *p = (uint8_t *)(uintptr_t)dst; for (uint64_t i = 0; i < n; i++) p[i] = (uint8_t)(value >> 8 * (i & 3));
        return;
    }
    pass_end(); barrier_all(cb);
    vkCmdFillBuffer(cb, db, dof, n, value);
    vkm_cpu_also(dst, n, NULL, 1, value);
}
void vkr_update(uint64_t d, uint64_t n, const uint8_t *data) {
    g_st.updates++;
    VkBuffer db; VkDeviceSize dof; VkCommandBuffer cb = vks_cb();
    if (!n) return;
    if (!cb || !vkm_use(d, n, &db, &dof)) {
        if (vkm_addr(d, n, NULL, NULL)) { dm_guest_write_prepare((void *)(uintptr_t)d, (size_t)n); memcpy((void *)(uintptr_t)d, data, (size_t)n); }
        return;
    }
    pass_end(); barrier_all(cb);
    if (!(dof & 3) && !(n & 3) && n <= 65536) vkCmdUpdateBuffer(cb, db, dof, n, data);   // inline in the command buffer
    else { VkBuffer sb; VkDeviceSize so; uint8_t *p = vkm_staging(n, 16, &sb, &so); if (p) { memcpy(p, data, (size_t)n); VkBufferCopy c = {so, dof, n}; vkCmdCopyBuffer(cb, sb, db, 1, &c); } }
    vkm_cpu_also(d, n, data, 0, 0);
}

// ---------- Texture copies and clears (nvn_mtl.m copy_b2t, copy_t2b, copy_t2t, clear_tex) ----------
static const tex_t *snap(const tex_t *t) { return t && t->magic == MAGIC('T', 'E', 'X', 0) && t->serial ? t : NULL; }
// A transfer on an image: GENERAL images stay so; a sampled-only image goes to the transfer layout and back to its resting one
static VkImageLayout xfer_in(VkCommandBuffer cb, vkt_image *im, int dst) {
    if (im->rest == VK_IMAGE_LAYOUT_GENERAL) return VK_IMAGE_LAYOUT_GENERAL;
    VkImageLayout l = dst ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vk_image_barrier(cb, im->img, im->aspect, im->rest, l, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                     dst ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT);
    return l;
}
static void xfer_out(VkCommandBuffer cb, vkt_image *im, VkImageLayout l) {
    if (l == im->rest) return;
    vk_image_barrier(cb, im->img, im->aspect, l, im->rest, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}
static VkImageAspectFlags copy_aspect(vkt_image *im) { return im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : im->aspect; }
// F-5: a transfer image at the game's size between a buffer and a render target drawn larger (it goes behind the GPU), and whether a format blits
typedef struct { VkImage img; vk_alloc mem; } tmp_img;
static void tmp_free(void *p) { tmp_img *t = p; vkDestroyImage(g_vk.dev, t->img, NULL); vk_free_mem(&t->mem); free(t); }
static VkImage tmp_image(const vkt_image *like, uint32_t w, uint32_t h, uint32_t layers) {
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, like->fi->vk, {w, h, 1}, 1, layers, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    tmp_img *t = calloc(1, sizeof *t);
    if (vkCreateImage(g_vk.dev, &ci, NULL, &t->img) != VK_SUCCESS) { free(t); return VK_NULL_HANDLE; }
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_vk.dev, t->img, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &t->mem)) { vkDestroyImage(g_vk.dev, t->img, NULL); free(t); return VK_NULL_HANDLE; }
    vkBindImageMemory(g_vk.dev, t->img, t->mem.mem, t->mem.off);
    vk_retire(tmp_free, t);
    return t->img;
}
static int blits(const vkt_image *im) { return im->samples == 1 && (fmt_features(im->fi->vk, 0) & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) == (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT); }
static VkFilter blit_filter(const vkt_image *im) {
    return !(im->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) && !(im->fi->flags & VKF_INT) && (fmt_features(im->fi->vk, 0) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}
static VkOffset3D px_at(const vkt_image *im, uint32_t lv, int64_t x, int64_t y, int32_t z) { return (VkOffset3D){vkt_sx(im, lv, x), vkt_sy(im, lv, y), z}; }
// The buffer side of a copy: SetCopyRowStride / SetCopyImageStride, else tightly packed (nvn_mtl.m copy_b2t)
static int copy_layout(const vkt_image *im, const NVNcopyRegion *r, size_t *row, size_t *img, uint32_t *row_texels, uint32_t *img_rows) {
    const vkf_info *fi = im->fi;
    size_t rows = (size_t)(r->height + fi->bh - 1) / fi->bh;
    *row = S->copy_stride ? (size_t)S->copy_stride : (size_t)((r->width + fi->bw - 1) / fi->bw) * fi->bpb;
    *img = S->copy_img_stride ? (size_t)S->copy_img_stride : *row * rows;
    if (*row % fi->bpb || (*row && *img % *row)) return 0;
    *row_texels = (uint32_t)(*row / fi->bpb * fi->bw); *img_rows = (uint32_t)(*row ? *img / *row * fi->bh : 0);
    return 1;
}
static void copy_b2t(const p_copy_b2t *c) {
    const tex_t *t = snap(c->tex); if (!t) return;
    PD("copy buffer %#llx -> ser%llu fmt%#x region %d,%d,%d %dx%dx%d", (unsigned long long)c->src, (unsigned long long)t->serial, t->d.format, c->r.xoffset, c->r.yoffset, c->r.zoffset, c->r.width, c->r.height, c->r.depth);
    vkt_image *im = vkt_resolve(t); if (!im) return;
    g_st.tex_copies++;
    if (im->fi->conv != VKF_COPY) { RED_ONCE(80, "buffer→texture copy into a format the device stores converted (%s): not copied", im->fi->name); return; }
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    if (level >= im->levels) return;
    size_t row, img; uint32_t rt, ir;
    if (!copy_layout(im, &c->r, &row, &img, &rt, &ir)) { RED_ONCE(81, "buffer→texture copy with strides Vulkan cannot express"); return; }
    int is3d = t->d.target == NVN_TEXTURE_TARGET_3D, nz = c->r.depth > 0 ? c->r.depth : 1;
    VkBuffer b; VkDeviceSize o;
    if (!vkm_use(c->src, img * nz, &b, &o)) { RED_ONCE(82, "buffer→texture copy source is in no memory pool"); return; }
    // a storage-backed texture gets the bytes in its storage too (hle_nvn.c nvn_tex_mirror_b2t), or the next upload from there would drop them
    if (im->cls == VKT_STORAGE && !is3d && nvn_tex_mirror_b2t(t, level, layer0, &c->r, (const uint8_t *)(uintptr_t)c->src, row, img)) RED_ONCE(83, "a buffer→texture copy into a storage-backed texture could not be written into its storage");
    pass_end();
    VkCommandBuffer cb = vks_cb(); barrier_all(cb);
    if (scaled_im(im)) {   // F-5: into a transfer image at the game's size, then blitted up into the render target drawn larger
        VkImage tmp = !is3d && blits(im) && nz <= 64 ? tmp_image(im, (uint32_t)c->r.width, (uint32_t)c->r.height, (uint32_t)nz) : VK_NULL_HANDLE;
        if (!tmp) { RED_ONCE(87, "a buffer→texture copy into a render target drawn larger (format %s) cannot be scaled up: not copied", im->fi->name); return; }
        VkImageAspectFlags a = copy_aspect(im);
        vk_image_barrier(cb, tmp, im->aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy rg[64]; VkImageBlit bl[64]; int n = 0;
        for (int z = 0; z < nz; z++) {
            rg[n] = (VkBufferImageCopy){o + (VkDeviceSize)z * img, rt, ir, {a, 0, (uint32_t)z, 1}, {0, 0, 0}, {(uint32_t)c->r.width, (uint32_t)c->r.height, 1}};
            bl[n++] = (VkImageBlit){{a, 0, (uint32_t)z, 1}, {{0, 0, 0}, {c->r.width, c->r.height, 1}}, {a, level, layer0 + c->r.zoffset + z, 1},
                                    {px_at(im, level, c->r.xoffset, c->r.yoffset, 0), px_at(im, level, (int64_t)c->r.xoffset + c->r.width, (int64_t)c->r.yoffset + c->r.height, 1)}};
        }
        vkCmdCopyBufferToImage(cb, b, tmp, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)n, rg);
        vk_image_barrier(cb, tmp, im->aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkImageLayout l = xfer_in(cb, im, 1);
        vkCmdBlitImage(cb, tmp, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, im->img, l, (uint32_t)n, bl, blit_filter(im));
        xfer_out(cb, im, l);
        im->content = 1;
        return;
    }
    VkImageLayout l = xfer_in(cb, im, 1);
    VkBufferImageCopy rg[64]; int n = 0;
    for (int z = 0; z < (is3d ? 1 : nz) && n < 64; z++)
        rg[n++] = (VkBufferImageCopy){o + (VkDeviceSize)z * img, rt, ir, {copy_aspect(im), level, is3d ? 0 : layer0 + c->r.zoffset + z, 1},
                                      {c->r.xoffset, c->r.yoffset, is3d ? c->r.zoffset : 0}, {(uint32_t)c->r.width, (uint32_t)c->r.height, is3d ? (uint32_t)nz : 1}};
    vkCmdCopyBufferToImage(cb, b, im->img, l, (uint32_t)n, rg);
    xfer_out(cb, im, l);
    im->content = 1;
}
static void copy_t2b(const p_copy_b2t *c) {
    const tex_t *t = snap(c->tex); if (!t) return;
    PD("copy ser%llu %dx%d at %d,%d -> buffer %#llx", (unsigned long long)t->serial, c->r.width, c->r.height, c->r.xoffset, c->r.yoffset, (unsigned long long)c->src);
    vkt_image *im = vkt_resolve(t); if (!im) return;
    g_st.tex_copies++;
    if (im->fi->conv != VKF_COPY) { RED_ONCE(84, "texture→buffer copy from a format the device stores converted (%s): not copied", im->fi->name); return; }
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    if (level >= im->levels) return;
    size_t row, img; uint32_t rt, ir;
    if (!copy_layout(im, &c->r, &row, &img, &rt, &ir)) { RED_ONCE(85, "texture→buffer copy with strides Vulkan cannot express"); return; }
    int is3d = t->d.target == NVN_TEXTURE_TARGET_3D, nz = c->r.depth > 0 ? c->r.depth : 1;
    VkBuffer b; VkDeviceSize o;
    if (!vkm_use(c->src, img * nz, &b, &o)) { RED_ONCE(86, "texture→buffer copy destination is in no memory pool"); return; }
    pass_end();
    VkCommandBuffer cb = vks_cb(); barrier_all(cb);
    if (scaled_im(im)) {   // F-5: a render target drawn larger is blitted down into a transfer image at the game's size, which is copied out
        VkImage tmp = !is3d && blits(im) && nz <= 64 ? tmp_image(im, (uint32_t)c->r.width, (uint32_t)c->r.height, (uint32_t)nz) : VK_NULL_HANDLE;
        if (!tmp) { RED_ONCE(88, "a texture→buffer copy from a render target drawn larger (format %s) cannot be scaled down: not copied", im->fi->name); return; }
        VkImageAspectFlags a = copy_aspect(im);
        vk_image_barrier(cb, tmp, im->aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy rg[64]; VkImageBlit bl[64]; int n = 0;
        int32_t ux0, ux1; int un = wide_unsqueeze(im, level, &c->r, &ux0, &ux1);
        for (int z = 0; z < nz; z++) {
            bl[n] = (VkImageBlit){{a, level, layer0 + c->r.zoffset + z, 1}, {px_at(im, level, c->r.xoffset, c->r.yoffset, 0), px_at(im, level, (int64_t)c->r.xoffset + c->r.width, (int64_t)c->r.yoffset + c->r.height, 1)},
                                  {a, 0, (uint32_t)z, 1}, {{0, 0, 0}, {c->r.width, c->r.height, 1}}};
            if (un) { bl[n].srcOffsets[0].x = ux0; bl[n].srcOffsets[1].x = ux1; }
            rg[n++] = (VkBufferImageCopy){o + (VkDeviceSize)z * img, rt, ir, {a, 0, (uint32_t)z, 1}, {0, 0, 0}, {(uint32_t)c->r.width, (uint32_t)c->r.height, 1}};
        }
        VkImageLayout l = xfer_in(cb, im, 0);
        vkCmdBlitImage(cb, im->img, l, tmp, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)n, bl, blit_filter(im));
        xfer_out(cb, im, l);
        vk_image_barrier(cb, tmp, im->aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        vkCmdCopyImageToBuffer(cb, tmp, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b, (uint32_t)n, rg);
        vkm_gpu_wrote(c->src, img * nz);
        return;
    }
    VkImageLayout l = xfer_in(cb, im, 0);
    VkBufferImageCopy rg[64]; int n = 0;
    for (int z = 0; z < (is3d ? 1 : nz) && n < 64; z++)
        rg[n++] = (VkBufferImageCopy){o + (VkDeviceSize)z * img, rt, ir, {copy_aspect(im), level, is3d ? 0 : layer0 + c->r.zoffset + z, 1},
                                      {c->r.xoffset, c->r.yoffset, is3d ? c->r.zoffset : 0}, {(uint32_t)c->r.width, (uint32_t)c->r.height, is3d ? (uint32_t)nz : 1}};
    vkCmdCopyImageToBuffer(cb, im->img, l, b, (uint32_t)n, rg);
    xfer_out(cb, im, l);
    vkm_gpu_wrote(c->src, img * nz);   // NVN pools are coherent with the CPU: the bytes reach guest memory when the submission completes
}
// M6 phase B: a GPU copy into a texture whose content comes from its storage (VKT_STORAGE), from a source whose content is the GPU's: the destination region is read back
// after the copy into the submission's arena and written into the storage when the submission completes (nvn_mtl.m mirror_readback / mirror_rb_land), so the next upload
// from there keeps the copy. The texture record is copied (the game may finalize it once it has waited)
typedef struct { tex_t t; unsigned level, layer0; NVNcopyRegion r; size_t row, img; const uint8_t *buf; } mirror_rb;
static void mirror_land(void *p) {
    mirror_rb *m = p;
    if (nvn_tex_mirror_b2t(&m->t, m->level, m->layer0, &m->r, m->buf, m->row, m->img)) RED_ONCE(96, "a GPU copy read back into the storage of texture ser=%llu could not be written there", (unsigned long long)m->t.serial);
    else { static int n; if (!n++) VKLOG("a GPU copy into a storage-backed texture (ser=%llu %dx%d): read back into its storage", (unsigned long long)m->t.serial, m->r.width, m->r.height); }
    free(m);
}
static void mirror_readback(VkCommandBuffer cb, vkt_image *im, const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r) {
    const vkf_info *fi = im->fi; int nz = r->depth > 0 ? r->depth : 1;
    if (fi->conv != VKF_COPY || im->samples > 1 || (im->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) || t->d.target == NVN_TEXTURE_TARGET_3D || r->width <= 0 || r->height <= 0) {
        RED_ONCE(95, "a GPU copy into a storage-backed texture (format %s) cannot be read back into its storage: the next upload from there drops it", fi->name); return; }
    size_t row = (size_t)((r->width + fi->bw - 1) / fi->bw) * fi->bpb, img = row * (size_t)((r->height + fi->bh - 1) / fi->bh);
    VkBuffer b; VkDeviceSize o; uint8_t *p = arena(img * nz, 16, &b, &o);
    if (!p) return;
    barrier_all(cb);   // the copy's writes first
    VkImageLayout l = xfer_in(cb, im, 0);
    VkBufferImageCopy rg[64]; int n = 0;
    for (int z = 0; z < nz && n < 64; z++)
        rg[n++] = (VkBufferImageCopy){o + (VkDeviceSize)z * img, 0, 0, {copy_aspect(im), level, layer0 + r->zoffset + z, 1}, {r->xoffset, r->yoffset, 0}, {(uint32_t)r->width, (uint32_t)r->height, 1}};
    vkCmdCopyImageToBuffer(cb, im->img, l, b, (uint32_t)n, rg);
    xfer_out(cb, im, l);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    mirror_rb *m = malloc(sizeof *m); *m = (mirror_rb){*t, level, layer0, *r, row, img, p};
    vks_post_fn(mirror_land, m);   // before the arena chunk is returned (subres_done is posted at the end of the submission)
    g_st.tex_copies++;
}
static void copy_t2t(const p_copy_t2t *c) {
    const tex_t *st = snap(c->src), *dt = snap(c->dst); if (!st || !dt) return;
    PD("copy ser%llu %dx%d -> ser%llu %dx%d flags %#x", (unsigned long long)st->serial, c->sr.width, c->sr.height, (unsigned long long)dt->serial, c->dr.width, c->dr.height, c->flags);
    vkt_image *s = vkt_resolve(st), *d = vkt_resolve(dt); if (!s || !d) return;
    g_st.tex_copies++;
    unsigned sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
    unsigned sz0 = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, dz0 = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
    if (sl >= s->levels || dl >= d->levels) return;
    int s3 = st->d.target == NVN_TEXTURE_TARGET_3D, d3 = dt->d.target == NVN_TEXTURE_TARGET_3D, nz = c->sr.depth > 0 ? c->sr.depth : 1;
    int mirror = (c->flags & (NVN_COPY_FLAGS_MIRROR_X_BIT | NVN_COPY_FLAGS_MIRROR_Y_BIT)) != 0, linear = (c->flags & NVN_COPY_FLAGS_LINEAR_FILTER_BIT) != 0;
    int same = c->sr.width == c->dr.width && c->sr.height == c->dr.height && !mirror && s->fi->vk == d->fi->vk, rb = 0;
    // the copy's bytes in the destination's storage when it is storage-backed (hle_nvn.c nvn_tex_mirror_t2t, from the source's storage when that holds its content)
    if (d->cls == VKT_STORAGE && !d3 && same && s->cls == VKT_STORAGE && !s3 && nvn_tex_mirror_t2t(st, sl, sz0, &c->sr, dt, dl, dz0, &c->dr))
        RED_ONCE(90, "a texture copy into a storage-backed texture could not be written into its storage");
    else if (d->cls == VKT_STORAGE) rb = 1;   // the source's content is the GPU's: read back after the copy (nvn_mtl.m mirror_readback)
    pass_end();
    VkCommandBuffer cb = vks_cb(); barrier_all(cb);
    VkImageLayout ls = xfer_in(cb, s, 0), ld = s == d ? ls : xfer_in(cb, d, 1);
    if (s == d && ls != VK_IMAGE_LAYOUT_GENERAL) { RED_ONCE(92, "a texture copied into itself outside the GENERAL layout: not copied"); xfer_out(cb, s, ls); return; }
    VkImageAspectFlags sa = copy_aspect(s), da = copy_aspect(d);
    // F-5: both rectangles in their image's own pixels (as they are when neither is drawn larger); a copy whose two sides differ there becomes a blit
    VkOffset3D s0 = px_at(s, sl, c->sr.xoffset, c->sr.yoffset, 0), s1 = px_at(s, sl, (int64_t)c->sr.xoffset + c->sr.width, (int64_t)c->sr.yoffset + c->sr.height, 1);
    VkOffset3D d0 = px_at(d, dl, c->dr.xoffset, c->dr.yoffset, 0), d1 = px_at(d, dl, (int64_t)c->dr.xoffset + c->dr.width, (int64_t)c->dr.yoffset + c->dr.height, 1);
    int32_t ux0, ux1;
    if (!s3 && !d3 && !scaled_im(d) && wide_unsqueeze(s, sl, &c->sr, &ux0, &ux1)) { s0.x = ux0; s1.x = ux1; }   // a blit below
    int fits =s1.x - s0.x == d1.x - d0.x && s1.y - s0.y == d1.y - d0.y;
    if ((c->flags & DM_COPY_RESOLVE) && s->samples > 1 && d->samples == 1 && s->fi->vk == d->fi->vk && !(s->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
        uint32_t rw = vkt_vlw(s, sl) < vkt_vlw(d, dl) ? vkt_vlw(s, sl) : vkt_vlw(d, dl), rh = vkt_vlh(s, sl) < vkt_vlh(d, dl) ? vkt_vlh(s, sl) : vkt_vlh(d, dl);
        VkImageResolve r = {{sa, sl, sz0, 1}, {0, 0, 0}, {da, dl, dz0, 1}, {0, 0, 0}, {rw, rh, 1}};
        vkCmdResolveImage(cb, s->img, ls, d->img, ld, 1, &r);
    } else if (same && fits && s->samples == d->samples) {
        VkImageCopy r[64]; int n = 0;
        if (s3 || d3) r[n++] = (VkImageCopy){{sa, sl, s3 ? 0 : sz0 + c->sr.zoffset, s3 ? 1 : (uint32_t)nz}, {c->sr.xoffset, c->sr.yoffset, s3 ? c->sr.zoffset : 0},
                                             {da, dl, d3 ? 0 : dz0 + c->dr.zoffset, d3 ? 1 : (uint32_t)nz}, {c->dr.xoffset, c->dr.yoffset, d3 ? c->dr.zoffset : 0}, {(uint32_t)c->sr.width, (uint32_t)c->sr.height, s3 && d3 ? (uint32_t)nz : 1}};
        else r[n++] = (VkImageCopy){{sa, sl, sz0 + c->sr.zoffset, (uint32_t)nz}, {s0.x, s0.y, 0}, {da, dl, dz0 + c->dr.zoffset, (uint32_t)nz}, {d0.x, d0.y, 0},
                                    {(uint32_t)(s1.x - s0.x), (uint32_t)(s1.y - s0.y), 1}};
        vkCmdCopyImage(cb, s->img, ls, d->img, ld, (uint32_t)n, r);
    } else if (!(s->fi->flags & VKF_COMPRESSED) && !(d->fi->flags & VKF_COMPRESSED) && s->samples == 1 && d->samples == 1 && !s3 && !d3 && (sa == da)) {
        // scaling, mirroring or a format change: a blit (nvn_mtl.m draws these: copy_t2t_draw)
        int x0 = d0.x, y0 = d0.y, x1 = d1.x, y1 = d1.y;
        if (c->flags & NVN_COPY_FLAGS_MIRROR_X_BIT) { int t = x0; x0 = x1; x1 = t; }
        if (c->flags & NVN_COPY_FLAGS_MIRROR_Y_BIT) { int t = y0; y0 = y1; y1 = t; }
        int depth = (sa & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
        int filt = (linear || (same && !mirror)) && !depth && (fmt_features(s->fi->vk, 0) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);   // a plain copy across scales: filtered
        VkImageBlit r[64]; int n = 0;
        for (int z = 0; z < nz && n < 64; z++)
            r[n++] = (VkImageBlit){{sa, sl, sz0 + c->sr.zoffset + z, 1}, {{s0.x, s0.y, 0}, {s1.x, s1.y, 1}}, {da, dl, dz0 + c->dr.zoffset + z, 1}, {{x0, y0, 0}, {x1, y1, 1}}};
        if (depth && s->fi->vk != d->fi->vk) RED_ONCE(93, "a depth copy between different formats: not copied");
        else if ((scaled_im(s) || scaled_im(d)) && (!blits(s) || !blits(d))) RED_ONCE(97, "a copy between a render target drawn larger and another (format %s): the device cannot blit it, not copied", s->fi->name);
        else vkCmdBlitImage(cb, s->img, ls, d->img, ld, (uint32_t)n, r, filt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    } else RED_ONCE(94, "texture→texture copy case not replayed: %dx%d→%dx%d flags %#x %s→%s samples %u→%u 3D %d→%d", c->sr.width, c->sr.height, c->dr.width, c->dr.height, c->flags,
                    s->fi->name, d->fi->name, (unsigned)s->samples, (unsigned)d->samples, s3, d3);
    if (s != d) xfer_out(cb, d, ld);
    xfer_out(cb, s, ls);
    d->content = 1;
    if (rb) mirror_readback(cb, d, dt, dl, dz0, &c->dr);
}
static void clear_tex(const p_clear_tex *c) {
    const tex_t *t = snap(c->tex); if (!t) return;
    vkt_image *im = vkt_resolve(t); if (!im) return;
    if (im->fi->flags & VKF_COMPRESSED) { RED_ONCE(100, "a clear of a compressed texture: not cleared"); return; }
    unsigned lv = c->has_view && c->view.set_levels ? c->view.base_level : 0, l0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    if (lv >= im->levels) return;
    uint32_t w = mipd(im->w, lv), h = mipd(im->h, lv);
    int is3d = im->vtype == VK_IMAGE_VIEW_TYPE_3D, nz = c->r.depth > 0 ? c->r.depth : 1;
    VkFormat vf = vkf_view_format(t->d.format, c->has_view && c->view.set_format ? c->view.format : t->d.format);
    int depth = (im->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
    unsigned ch = depth ? 0xf : fmt_channels(vf);
    if ((c->mask & ch) != ch && !depth) RED_ONCE(101, "texture clear with channel mask %#x: cleared whole (a partial mask needs a clear draw)", c->mask);
    int whole = !c->r.xoffset && !c->r.yoffset && (uint32_t)c->r.width >= w && (uint32_t)c->r.height >= h;
    g_st.clears++;
    if (!whole && (im->usage & (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) && !is3d) {   // a region: a pass with that render area
        VkImageView v = vkt_view_raw_(im, depth ? im->fi->vk : vf, VK_IMAGE_VIEW_TYPE_2D_ARRAY, lv, 1, l0 + c->r.zoffset, (uint32_t)nz, depth ? im->aspect : VK_IMAGE_ASPECT_COLOR_BIT, NULL);
        VkRect2D r = px_rect(im, lv, (VkRect2D){{c->r.xoffset, c->r.yoffset}, {(uint32_t)c->r.width, (uint32_t)c->r.height}}, vkt_vlw(im, lv), vkt_vlh(im, lv));
        VkClearValue val; if (depth) { val.depthStencil.depth = c->c.f[0]; val.depthStencil.stencil = c->c.u[1]; } else val = clear_color_value(vf, c->c.f, c->c.u, c->is_uint);
        if (v) clear_pass(v, depth ? im->fi->vk : vf, depth, 1, 1, r, (uint32_t)nz, val);
        im->content = 1;
        return;
    }
    if (!whole) RED_ONCE(102, "a region clear of a texture that cannot be an attachment: the whole level is cleared");
    pass_end();
    VkCommandBuffer cb = vks_cb(); barrier_all(cb);
    VkImageLayout l = xfer_in(cb, im, 1);
    VkImageSubresourceRange rr = {im->aspect, lv, 1, is3d ? 0 : l0 + c->r.zoffset, is3d ? 1 : (uint32_t)nz};
    if (depth) { VkClearDepthStencilValue v = {c->c.f[0], c->c.u[1]}; vkCmdClearDepthStencilImage(cb, im->img, l, &v, 1, &rr); }
    else { VkClearValue v = clear_color_value(im->fi->vk, c->c.f, c->c.u, c->is_uint); vkCmdClearColorImage(cb, im->img, l, &v.color, 1, &rr); }
    xfer_out(cb, im, l);
    im->content = 1;
}

// ---------- Counters ----------
static void report_counter(uint64_t type, uint64_t addr) {
    if (type == NVN_COUNTER_TYPE_SAMPLES_PASSED) {
        if (P.query >= 0) { vkCmdEndQuery(vks_cb(), R0.qp, (uint32_t)P.query); q_post(0, (uint32_t)P.query, 0); P.query = -1; }
        q_post(2, 0, addr); return;
    }
    if (type != NVN_COUNTER_TYPE_TIMESTAMP && type != NVN_COUNTER_TYPE_TIMESTAMP_TOP) { static uint32_t seen; if (!(seen >> (type & 31) & 1)) { seen |= 1u << (type & 31); VKRED("counter type %llu is reported as 0 (only timestamps and samples passed are counted)", (unsigned long long)type); } }
    vks_post_counter(addr, (uint32_t)type);
}
static void reset_counter(uint64_t type) {
    if (type != NVN_COUNTER_TYPE_SAMPLES_PASSED) return;
    if (P.query >= 0) { vkCmdEndQuery(vks_cb(), R0.qp, (uint32_t)P.query); q_post(0, (uint32_t)P.query, 0); P.query = -1; }
    q_post(1, 0, 0); S->vis_on = 1;
}

// ---------- State records (nvn_mtl.m state_op) ----------
static void bind_handles_fresh(const p_bind_handles *b) {   // BindTexturesDeferred: the handles at src as they are now (nvn_mtl.m bh_fresh)
    if (b->defer && b->src && b->n > 0 && b->n <= 32) memcpy((void *)b->h, (const void *)(uintptr_t)b->src, 8 * (size_t)b->n);
}
static void state_op(uint32_t op, const void *a) {
    switch (op) {
    case OP_SCISSOR: S->sc = *(const p_scissor *)a; S->has_sc = 1; break;
    case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL: { int tex = op == OP_SET_TEX_POOL, b = nvn_dpool_base(((const p_set_pool *)a)->pool, tex); if (tex) S->tex_base = b; else S->samp_base = b; break; }
    case OP_BIND_PROGRAM: { const p_bind_program *bp = a;
        if (bp->stages & 0x20) S->cprog = bp->prog;
        for (int st = 0; st < 5; st++) if (bp->stages >> st & 1) S->bst[st] = bp->prog;
        break; }
    case OP_BIND_STATE: {
        const p_bind_state *b = a; blend_st bs;
        switch (b->kind) {
        case 0: memcpy(&bs, b->bytes, sizeof bs); if (bs.target < 8) S->blend[bs.target] = bs; break;
        case 1: memcpy(&S->color, b->bytes, sizeof S->color); break;
        case 2: memcpy(&S->chmask, b->bytes, sizeof S->chmask); break;
        case 3: memcpy(&S->ms, b->bytes, sizeof S->ms); break;
        case 4: memcpy(&S->poly, b->bytes, sizeof S->poly); break;
        case 5: memcpy(&S->ds, b->bytes, sizeof S->ds); break;
        }
        break; }
    case OP_BIND_VATTR: { const p_bind_vattr *b = a; if (b->n >= 0 && b->n <= 16) { memcpy(S->attr, b->st, 4 * (size_t)b->n); S->nattr = b->n; } break; }
    case OP_BIND_VSTREAM: { const p_bind_vstream *b = a; if (b->n >= 0 && b->n <= 16) { memcpy(S->stream, b->st, 8 * (size_t)b->n); S->nstream = b->n; } break; }
    case OP_BIND_VBUF: case OP_BIND_VBUF1: { p_bind_vbuf tmp; const p_bind_vbuf *b = vbuf_rec(op, a, &tmp); if (b->first >= 0 && b->first + b->n <= 16) memcpy(&S->vbuf[b->first], b->r, sizeof *b->r * (size_t)b->n); break; }
    case OP_BIND_UBO: case OP_BIND_UBO_SMALL: { p_bind_buf tmp; const p_bind_buf *b = bind_buf_rec(op, a, &tmp); if (b->stage >= 0 && b->stage < 6 && b->index >= 0 && b->index < 16) S->ubo[b->stage][b->index] = *b; break; }
    case OP_BIND_SSBO: { const p_bind_buf *b = a; if (b->stage >= 0 && b->stage < 6 && b->index >= 0 && b->index < 16) S->ssbo[b->stage][b->index] = *b; break; }
    case OP_BIND_TEX: case OP_BIND_TEX1: { p_bind_handles t1; const p_bind_handles *b = bind_rec(op, a, &t1);
        if (b->n < 0 || b->n > 32) { RED_ONCE(110, "a bind record holding count %d: overwritten after it was recorded", b->n); break; }
        bind_handles_fresh(b);
        if (b->stage >= 0 && b->stage < 6 && b->first >= 0 && b->first + b->n <= NTEX) memcpy(&S->tex[b->stage][b->first], b->h, 8 * (size_t)b->n);
        break; }
    case OP_BIND_SAMP: { const p_bind_handles *b = a; bind_handles_fresh(b); if (b->stage >= 0 && b->stage < 6 && b->first >= 0 && b->first + b->n <= 128) memcpy(&S->samp[b->stage][b->first], b->h, 8 * (size_t)b->n); break; }
    case OP_BIND_IMG: { const p_bind_handles *b = a; bind_handles_fresh(b); if (b->stage >= 0 && b->stage < 6 && b->first >= 0 && b->first + b->n <= 32) memcpy(&S->img[b->stage][b->first], b->h, 8 * (size_t)b->n); break; }
    case OP_STENCIL: { const p_stencil *b = a; for (int f = 0; f < 2; f++) if (b->face & (1 << f)) { if (b->which == 0) S->st_vmask[f] = (uint32_t)b->value_mask; else if (b->which == 1) S->st_wmask[f] = (uint32_t)b->mask; else S->st_ref[f] = (uint32_t)b->ref; } break; }
    case OP_POLY_OFFSET: S->po = *(const p_poly_offset *)a; break;
    case OP_VIEWPORTS: { const p_viewports *b = a; for (int i = 0; i < b->n && b->first + i < 16; i++) memcpy(S->vp[b->first + i], b->v[i], 16); S->has_vp = 1; break; }
    case OP_DEPTH_RANGE: { const p_depth_range *b = a; S->dr[0] = b->n; S->dr[1] = b->f; break; }
    case OP_DEPTH_CLAMP: S->depth_clamp = ((const p_depth_clamp *)a)->on; break;
    case OP_VP_SWIZZLE: { const p_vp_swizzle *b = a; if (b->first >= 0 && b->first + b->n <= 16) memcpy(S->vp_sw[b->first], b->sw, 4 * (size_t)b->n); break; }
    case OP_BLEND_COLOR: memcpy(S->blend_color, ((const p_blend_color *)a)->rgba, 16); break;
    case OP_ALPHA_REF: S->alpha_ref = ((const p_alpha_ref *)a)->ref; break;
    case OP_COPY_STRIDE: { const p_copy_stride *cs = a; if (cs->image) S->copy_img_stride = cs->stride; else S->copy_stride = cs->stride; break; }
    case OP_TF_STATE: { const p_tf_state *b = a; S->tf_on = b->what == 0 || b->what == 3; if (S->tf_on) RED_ONCE(111, "transform feedback is not captured yet"); break; }
    case OP_PRIM_RESTART: S->restart = *(const p_prim_restart *)a; break;
    case OP_RAST_DISCARD: S->rast_discard = ((const p_rast_discard *)a)->on; break;
    default: break;
    }
}
static void set_rt(const p_set_rt *r) {
    pass_end(); P.valid = 0;
    S->nrt = r->n > 8 ? 8 : r->n < 0 ? 0 : r->n;
    memset(S->has_ct, 0, sizeof S->has_ct);
    for (int i = 0; i < S->nrt; i++) {
        const tex_t *t = snap(r->color[i]);
        if (t) { S->ct[i] = *t; S->has_ct[i] = 1; }
        S->cview[i] = r->cview[i]; S->has_cview[i] = r->has_cview[i];
    }
    const tex_t *d = snap(r->depth);
    S->has_dt = d != NULL; if (d) S->dt = *d;
    S->dview = r->dview; S->has_dview = r->has_dview;
}
static void render_cond(const p_render_cond *rc) {   // nvn_mtl.m OP_RENDER_COND: the counters as guest memory holds them at replay
    if (rc->mode >= 2) { S->render_skip = rc->mode == 3; return; }
    uint64_t x = 0, y = 0; uint32_t lo = 0, hi = 0; int known = 1;
    known &= read_u32(rc->addr, &lo) && read_u32(rc->addr + 4, &hi); x = (uint64_t)hi << 32 | lo;
    known &= read_u32(rc->addr + 16, &lo) && read_u32(rc->addr + 20, &hi); y = (uint64_t)hi << 32 | lo;
    S->render_skip = known && (rc->mode ? x == y : x != y);
}

// ---------- The record walk (nvn_mtl.m mtl_exec; the framing checks of track B's walk) ----------
static void bad_record(const uint8_t *p, op_hdr h, const char *why) {
    if (g_st.bad++ < 8) VKRED("%s at %p (op %u, size %u): the rest of this command list is not replayed", why, (const void *)p, h.op, h.size);
}
static int walk(const uint8_t *p, int depth, uint64_t *budget) {
    for (;;) {
        op_hdr h; memcpy(&h, p, sizeof h);
        if (!(*budget)--) { bad_record(p, h, "more than 16M records in one list (a JUMP loop?)"); return -1; }
        if (h.op >= OP_COUNT_ || h.size < 8 || (h.size & 7) || (h.op != OP_END && h.size > (64u << 20))) { bad_record(p, h, "a malformed record"); return -1; }
        const uint8_t *a = p + 8;
        g_st.records++;
        switch (h.op) {
        case OP_END: return 0;
        case OP_BEGIN: break;
        case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); if (!t) { bad_record(p, h, "a JUMP to 0"); return -1; } p = t; continue; }
        case OP_CALL: { uint64_t t; memcpy(&t, a, 8); if (depth >= 8 || !t) { bad_record(p, h, depth >= 8 ? "CALLs nested deeper than 8" : "a CALL of handle 0"); return -1; }
            if (walk((const uint8_t *)(uintptr_t)t, depth + 1, budget)) return -1; break; }
        case OP_SIGNAL_EVENT: { volatile uint32_t *val; uint32_t m, v; memcpy(&val, a, 8); memcpy(&m, a + 8, 4); memcpy(&v, a + 12, 4); if (val) vks_post_event(val, m == NVN_EVENT_SIGNAL_MODE_ADD ? 1 : 0, v); break; }
        case OP_REPORT_COUNTER: { uint64_t t, ad; memcpy(&t, a, 8); memcpy(&ad, a + 8, 8); report_counter(t, ad); break; }
        case OP_RESET_COUNTER: { uint64_t t; memcpy(&t, a, 8); reset_counter(t); break; }
        case OP_FENCE_SYNC: { sync_t *s; memcpy(&s, a, 8); if (s) vk_note_fence_sync(s); break; }
        case OP_COPY_B2B: { uint64_t s_, d, n; memcpy(&s_, a, 8); memcpy(&d, a + 8, 8); memcpy(&n, a + 16, 8); vkr_copy(s_, d, n); break; }
        case OP_UPDATE_UB: { uint64_t d, n; memcpy(&d, a, 8); memcpy(&n, a + 8, 8);
            if (24 + n > h.size) { bad_record(p, h, "a uniform update longer than its record"); return -1; }
            vkr_update(d, n, a + 16); break; }
        case OP_CLEAR_BUF: { const p_clear_buf *cb = (const p_clear_buf *)a; vkr_fill(cb->addr, cb->size, cb->value); break; }
        case OP_SET_RT: set_rt((const p_set_rt *)a); break;
        case OP_CLEAR_COLOR: clear_color((const p_clear_color *)a); break;
        case OP_CLEAR_DS: clear_ds((const p_clear_ds *)a); break;
        case OP_DRAW: { p_draw t; draw(draw_rec(a, h.size, &t), NULL); break; }
        case OP_DRAW_IDX: { p_draw_idx t; draw(NULL, draw_idx_rec(a, h.size, &t)); break; }
        case OP_DISPATCH: dispatch((const p_dispatch *)a); break;
        case OP_BARRIER: pass_end(); g_st.barriers++; break;
        case OP_COPY_B2T: copy_b2t((const p_copy_b2t *)a); break;
        case OP_COPY_T2B: copy_t2b((const p_copy_b2t *)a); break;
        case OP_COPY_T2T: copy_t2t((const p_copy_t2t *)a); break;
        case OP_CLEAR_TEX: clear_tex((const p_clear_tex *)a); break;
        case OP_DRAW_TEX: RED_ONCE(120, "DrawTexture is not replayed yet"); break;
        case OP_RENDER_COND: render_cond((const p_render_cond *)a); break;
        case OP_TF_BIND: break;
        default: state_op(h.op, a); break;
        }
        p += h.size;
    }
}
void vkr_exec(const uint8_t *handle) {
    g_st.handles++;
    if (!handle) { op_hdr z = {0, 0}; bad_record(handle, z, "a null command handle"); return; }
    if (!S) S = rs_get(0);
    uint64_t budget = 1u << 24;
    walk(handle, 0, &budget);
}

// ---------- Submissions, queues ----------
void vkr_sub_begin(void) {
    memset(&g_sub, 0, sizeof g_sub); g_sub_no++;
    P.valid = 0; P.open = 0; P.query = -1; g_last_pipe = VK_NULL_HANDLE; g_nwb = 0; dc_reset();
    if (!S) S = rs_get(0);
    vkt_apply_scale(vks_cb());   // F-5: a resolution the in-game menu asked for
    // the slot's reset is recorded (VUID-vkResetQueryPool-firstQuery-02741: a host reset may meet the slot's last use still running while loading floods the queue)
    if (g_tqp) { uint32_t q = g_tq_next++ % NTQ; vkCmdResetQueryPool(vks_cb(), g_tqp, 2 * q, 2); vkCmdWriteTimestamp2(vks_cb(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_tqp, 2 * q); g_sub.tq = (int)q + 1; }
}
void vkr_sub_end(void) {
    pass_end();
    if (g_nwb) { barrier_all(vks_cb()); wb_flush(); }
    if (g_sub.tq) vkCmdWriteTimestamp2(vks_cb(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_tqp, 2 * (uint32_t)(g_sub.tq - 1) + 1);
    subres *r = malloc(sizeof *r); *r = g_sub; memset(&g_sub, 0, sizeof g_sub);
    vks_post_fn(subres_done, r);
    P.valid = 0;
}
void vkr_queue_activate(uint64_t serial) { S = rs_get(serial); P.valid = 0; }
void vkr_queue_forget(uint64_t serial) {
    for (rs **pp = &g_states; *pp; pp = &(*pp)->next) if ((*pp)->serial == serial) { rs *s = *pp; *pp = s->next; if (S == s) S = NULL; free(s); break; }
}
void vkr_set_origin_lower_left(int on) { if (g_origin_ll != on) VKLOG("window origin: %s", on ? "lower left (the viewport as given)" : "upper left (the viewport flipped)"); g_origin_ll = on; }
void vkr_set_queue_origin_lower_left(int on) { g_queue_origin_ll = on; }
void vkr_set_depth_zero(int on) { g_depth_zero = on; }

void vkr_stats(vkr_stats_t *s) { *s = g_st; }
static _Atomic long g_pd_req; static _Atomic int g_td_req;
void vkr_passdump_next(long n) { atomic_store(&g_pd_req, n > 0 ? n : 1); }
void vkr_texdump_next(void) { atomic_store(&g_td_req, 1); }
void vkr_frame(uint64_t frame) {
    static vkr_stats_t prev; static uint64_t pf;
    skip_poll();
    long pr = atomic_exchange(&g_pd_req, 0); if (pr > 0) { g_pd_frame = frame; g_pd_last = frame + (uint64_t)pr - 1; }   // DM_CTL passdump / texdump: from this frame on
    if (atomic_exchange(&g_td_req, 0)) g_td_frame = frame;
    g_pd = g_pd_frame && frame >= g_pd_frame && frame <= g_pd_last;
    if (g_td_frame && frame == g_td_frame + 1 && g_ntd) {   // the dump frame's images, now that its work is done
        vks_finish();
        for (int i = 0; i < g_ntd; i++) {
            vkt_image *im = g_td[i]; char path[1200]; const char *lp = nxrt_log_path();
            for (unsigned z = 0; z < (im->vtype == VK_IMAGE_VIEW_TYPE_3D ? (im->depth > 4 ? 4 : im->depth) : 1); z++) {
                snprintf(path, sizeof path, "%s.tex-%llu-ser%llu-%s-%ux%u-z%u.png", lp && *lp ? lp : "nxrt", (unsigned long long)g_td_frame, (unsigned long long)im->serial, im->fi->name, im->w, im->h, z);
                VKLOG("texture dump: ser%llu %s %ux%ux%u %s", (unsigned long long)im->serial, im->fi->name, im->w, im->h, im->depth, vks_dump_image(im, z, path) ? "not dumped" : path);
                if (im->w * im->h <= 16 && im->fi->vbpb <= 16 && !(im->fi->flags & VKF_COMPRESSED) && !(im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) {   // tiny ones: the values themselves
                    uint8_t raw[256]; NVNcopyRegion rr = {0, 0, 0, (int)im->w, (int)im->h, 1}; char t[600]; int o = 0;
                    if (!vkt_readback_region(im, 0, 0, &rr, raw, (size_t)im->w * im->h * im->fi->vbpb))
                        for (uint32_t k = 0; k < im->w * im->h * im->fi->vbpb / 4 && o < 560; k++) { float f; uint32_t u; memcpy(&f, raw + 4 * k, 4); memcpy(&u, raw + 4 * k, 4); o += snprintf(t + o, sizeof t - o, " %g(%#x)", f, u); }
                    VKLOG("texture dump: ser%llu values:%s", (unsigned long long)im->serial, t);
                }
            }
        }
        g_ntd = 0;
    }
    g_td_on = g_td_frame && frame == g_td_frame;
    if (g_pd) PD("---- the frame after present %llu ----", (unsigned long long)frame);

    if (frame % 300 || frame == pf) return;
    static uint64_t pgn, pgs; uint64_t gn = atomic_load(&g_gpu_ns), gs = atomic_load(&g_gpu_subs);
    if (gs > pgs) VKLOG("GPU time up to frame %llu: %.2f ms per frame (%.2f ms per submission, %.1f submissions per frame; timestamps around each submission's replay)",
                        (unsigned long long)frame, (gn - pgn) / 1e6 / (double)(frame - pf), (gn - pgn) / 1e6 / (double)(gs - pgs), (double)(gs - pgs) / (double)(frame - pf));
    pgn = gn; pgs = gs;
    vkr_stats_t s = g_st; uint64_t df = frame - pf; vkp_stats_t ps; vkp_stats(&ps); uint64_t shl, shf; vksh_stats(&shl, &shf);
    VKLOG("replay up to frame %llu, per frame: %.1f draws (%.1f skipped: no shader %llu, no pipeline %llu, no target %llu, NXRT_VK_SKIP %llu in all), %.1f passes, %.1f dispatches, %.1f clears, "
          "%.1f texture copies, %.1f buffer copies / updates / fills, %.1f descriptor sets (%.1f with render scales, %.1f more reused), %.1f write-backs; %llu graphics + %llu compute pipelines (%.1f ms creating, worst %.1f ms), %llu shaders (%llu unusable)",
          (unsigned long long)frame, (double)(s.draws - prev.draws) / df, (double)(s.draws_skipped - prev.draws_skipped) / df, (unsigned long long)s.skip_no_shader,
          (unsigned long long)s.skip_no_pipeline, (unsigned long long)s.skip_no_target, (unsigned long long)s.skip_named, (double)(s.passes - prev.passes) / df, (double)(s.dispatches - prev.dispatches) / df,
          (double)(s.clears - prev.clears) / df, (double)(s.tex_copies - prev.tex_copies) / df, (double)(s.copies + s.updates + s.fills - prev.copies - prev.updates - prev.fills) / df,
          (double)(s.desc_sets - prev.desc_sets) / df, (double)(s.scaled_sets - prev.scaled_sets) / df, (double)(s.sets_reused - prev.sets_reused) / df, (double)(s.writebacks - prev.writebacks) / df, (unsigned long long)ps.graphics, (unsigned long long)ps.compute,
          ps.create_ns / 1e6, ps.max_ns / 1e6, (unsigned long long)shl, (unsigned long long)shf);
    if (ps.prewarmed) VKLOG("pipelines made ahead by the prewarm up to frame %llu: %llu (%.1f ms on its threads)", (unsigned long long)frame, (unsigned long long)ps.prewarmed, ps.prewarm_ns / 1e6);
    if (g_vk.has.buffer_marker) { uint64_t fin = 0, st = vk_markers_now(&fin); VKLOG("AMD buffer markers at frame %llu: started #%llu, everything before #%llu finished", (unsigned long long)frame, (unsigned long long)st, (unsigned long long)fin); }
    prev = s; pf = frame;
}

int vkr_init(void) {
    if (vksh_init() || vkpl_init()) return -1;
    if (!g_vk.has.null_descriptor) VKRED("the device has no null descriptors (VK_EXT_robustness2): unbound textures and images are not replayed correctly");
    VkBufferUsageFlags bu = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (mkbuf(ZERO_SIZE, bu, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &R0.zero, &R0.zero_m, "replay zeros") || mkbuf(ZERO_SIZE, bu, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &R0.sink, &R0.sink_m, "replay write sink") ||
        mkbuf(2048, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &R0.support, &R0.support_m, "support buffer") ||
        mkbuf(65536 * 24, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &R0.quads, &R0.quads_m, "quad indices") ||
        mkbuf(BOUNCE_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &R0.bounce, &R0.bounce_m, "storage bounce")) return -1;
    // the support buffer in Ryujinx's layout (nvn_mtl.m support_buf): render scale 1.0 in each of the 73 entries from byte 192, the rest zero
    memset(R0.support_m.map, 0, 2048);
    for (int i = 0; i < 73; i++) { float one = 1.0f; memcpy((uint8_t *)R0.support_m.map + 192 + 16 * i, &one, 4); }
    uint32_t *q = R0.quads_m.map;
    for (uint32_t i = 0; i < 65536; i++) { uint32_t b = 4 * i; q[6 * i] = b; q[6 * i + 1] = b + 1; q[6 * i + 2] = b + 2; q[6 * i + 3] = b; q[6 * i + 4] = b + 2; q[6 * i + 5] = b + 3; }
    VkCommandBuffer cb = vk_once_begin();
    vkCmdFillBuffer(cb, R0.zero, 0, ZERO_SIZE, 0); vkCmdFillBuffer(cb, R0.sink, 0, ZERO_SIZE, 0);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
    if (vk_once_end(cb)) return -1;
    VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0, VK_FALSE, 1, VK_FALSE, VK_COMPARE_OP_NEVER, 0, VK_LOD_CLAMP_NONE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE};
    if (vkCreateSampler(g_vk.dev, &sci, NULL, &R0.samp) != VK_SUCCESS) return -1;
    VkQueryPoolCreateInfo qci = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_OCCLUSION, NQ, 0};
    if (vkCreateQueryPool(g_vk.dev, &qci, NULL, &R0.qp) != VK_SUCCESS) return -1;
    vkResetQueryPool(g_vk.dev, R0.qp, 0, NQ);
    VkQueryPoolCreateInfo tci = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_TIMESTAMP, 2 * NTQ, 0};
    if (g_vk.props.limits.timestampComputeAndGraphics && vkCreateQueryPool(g_vk.dev, &tci, NULL, &g_tqp) == VK_SUCCESS) vkResetQueryPool(g_vk.dev, g_tqp, 0, 2 * NTQ);
    S = rs_get(0);
    const char *e = getenv("NXRT_VK_PASSDUMP"); if (e) { char *end; g_pd_frame = strtoull(e, &end, 0); g_pd_last = *end == '-' ? strtoull(end + 1, NULL, 0) : g_pd_frame; }
    if ((e = getenv("NXRT_VK_TEXDUMP"))) g_td_frame = strtoull(e, NULL, 0);
    vkpl_prewarm_start();   // M6 phase B: pipelines listed before, made ahead in the background
    return 0;
}
void vkr_shutdown(void) {
    while (g_states) { rs *s = g_states; g_states = s->next; free(s); }
    S = NULL;
    for (uint32_t i = 0; i < g_progc.cap; i++) free(g_progc.vals[i]);
    vk_hm_free(&g_progc);
    while (g_free_chunks) { rchunk *c = g_free_chunks; g_free_chunks = c->next; vkDestroyBuffer(g_vk.dev, c->buf, NULL); vk_free_mem(&c->mem); free(c); }
    while (g_free_pools) { rdpool *p = g_free_pools; g_free_pools = p->next; vkDestroyDescriptorPool(g_vk.dev, p->pool, NULL); free(p); }
    if (R0.qp) vkDestroyQueryPool(g_vk.dev, R0.qp, NULL);
    if (g_tqp) vkDestroyQueryPool(g_vk.dev, g_tqp, NULL);
    g_tqp = VK_NULL_HANDLE;
    if (R0.samp) vkDestroySampler(g_vk.dev, R0.samp, NULL);
    VkBuffer bs[5] = {R0.zero, R0.sink, R0.support, R0.quads, R0.bounce}; vk_alloc *ms[5] = {&R0.zero_m, &R0.sink_m, &R0.support_m, &R0.quads_m, &R0.bounce_m};
    for (int i = 0; i < 5; i++) if (bs[i]) { vkDestroyBuffer(g_vk.dev, bs[i], NULL); vk_free_mem(ms[i]); }
    memset(&R0, 0, sizeof R0);
    for (int i = 0; i < g_ndct; i++) { for (int j = 0; j < 4; j++) if (g_dct[i].view[j]) vkDestroyImageView(g_vk.dev, g_dct[i].view[j], NULL); vkDestroyImage(g_vk.dev, g_dct[i].img, NULL); vk_free_mem(&g_dct[i].mem); }
    g_ndct = 0; vk_hm_free(&g_dct_of);
    for (uint32_t i = 0; i < g_cpipe.cap; i++) if (g_cpipe.vals[i]) vkDestroyPipeline(g_vk.dev, (VkPipeline)g_cpipe.vals[i], NULL);
    vk_hm_free(&g_cpipe);
    for (int i = 0; i < 4; i++) if (g_cmod[i]) { vkDestroyShaderModule(g_vk.dev, g_cmod[i], NULL); g_cmod[i] = VK_NULL_HANDLE; }
    if (g_cpl) { vkDestroyPipelineLayout(g_vk.dev, g_cpl, NULL); g_cpl = VK_NULL_HANDLE; }
    if (g_qap) { vkDestroyPipeline(g_vk.dev, g_qap, NULL); g_qap = VK_NULL_HANDLE; }
    if (g_qapl) { vkDestroyPipelineLayout(g_vk.dev, g_qapl, NULL); g_qapl = VK_NULL_HANDLE; }
    if (g_qadl) { vkDestroyDescriptorSetLayout(g_vk.dev, g_qadl, NULL); g_qadl = VK_NULL_HANDLE; }
    vkpl_shutdown(); vksh_shutdown();
}
