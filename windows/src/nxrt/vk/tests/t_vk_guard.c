/* vkspv_limit_count (D-1 on 1.3.0, shader 634565ab77c61463; D-11 on 1.3.1, shader 560a0262fe47818a): a loop whose count is a constant buffer word walks no more
   items than its texel table holds. test_guard.comp has 634565ab's shape (word 0, signed, 5 texels an item), test_guard_u.comp 560a0262's (word 13, unsigned, one
   texel an item); the edits are checked on the words and, on a GPU, with a sane count (unchanged) and a garbage one (the table's items, at once) for a table of
   one row and of two. vkspv_cap_bound (Styphix's hang on 1.3.3, shader 2cb5df5cb96b324e): test_guard_v.comp keeps its count in a variable, `bound`, capped
   at 100. The GPU part is skipped without a device. */
#include "vk_test.h"
#include "vk_shader.h"
#include "vk_pipe.h"
#include "test_guard_comp.h"
#include "test_guard_u_comp.h"
#include "test_guard_v_comp.h"
#include "test_guard_b_comp.h"

NXRT_EARLY_RESERVE()

static uint32_t *copy_words(const uint32_t *w, size_t n) { uint32_t *c = malloc(n * 4); memcpy(c, w, n * 4); return c; }
static uint32_t named(const uint32_t *w, size_t n, const char *name) {   // the id an OpName gives that name, 0 none
    for (size_t i = 5; i < n && (w[i] >> 16); i += w[i] >> 16) if ((w[i] & 0xffff) == 5 && !strcmp((const char *)(w + i + 2), name)) return w[i + 1];
    return 0;
}

// One dispatch of 64 lanes with the count's bits in cp_c3's word `word` and a w x h table; how many items lane 0 walked (all lanes the same), or ~0u
static uint32_t walk(const uint32_t *words, size_t n, uint32_t word, int32_t count, uint32_t w, uint32_t h) {
    uint32_t got = ~0u;
    VkBuffer buf[2] = {0}; vk_alloc mem[2] = {0}; VkDeviceSize size[2] = {64, 64 * 4};
    VkBufferUsageFlags use[2] = {VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    for (int k = 0; k < 2; k++) {
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size[k], use[k], VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
        if (vkCreateBuffer(g_vk.dev, &bci, NULL, &buf[k])) return got;
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, buf[k], &mr);
        if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &mem[k]) || !mem[k].map) return got;
        vkBindBufferMemory(g_vk.dev, buf[k], mem[k].mem, mem[k].off);
    }
    memset(mem[0].map, 0, 64); memcpy((uint8_t *)mem[0].map + word * 4, &count, 4); memset(mem[1].map, 0xff, 64 * 4);
    // the table: only its size matters to the count; its texels are read and summed, never compared
    VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
                             VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage img; vk_alloc imem = {0}; VkImageView view; VkSampler samp;
    if (vkCreateImage(g_vk.dev, &ici, NULL, &img)) return got;
    VkMemoryRequirements imr; vkGetImageMemoryRequirements(g_vk.dev, img, &imr);
    if (vk_alloc_mem(&imr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &imem)) return got;
    vkBindImageMemory(g_vk.dev, img, imem.mem, imem.off);
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32_SFLOAT, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCreateImageView(g_vk.dev, &vci, NULL, &view);
    VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    vkCreateSampler(g_vk.dev, &sci, NULL, &samp);
    VkDescriptorSetLayoutBinding b[3] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, b};
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkShaderModule m; VkPipeline p; VkDescriptorPool dp; VkDescriptorSet ds;
    vkCreateDescriptorSetLayout(g_vk.dev, &dl, NULL, &dsl);
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 0, NULL};
    vkCreatePipelineLayout(g_vk.dev, &pli, NULL, &pl);
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, n * 4, words};
    if (vkCreateShaderModule(g_vk.dev, &mi, NULL, &m)) return got;
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                                      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", NULL}, pl, VK_NULL_HANDLE, -1};
    if (vkCreateComputePipelines(g_vk.dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)) return got;
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 3, ps};
    vkCreateDescriptorPool(g_vk.dev, &dpi, NULL, &dp);
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dp, 1, &dsl};
    vkAllocateDescriptorSets(g_vk.dev, &ai, &ds);
    VkDescriptorBufferInfo bi[2] = {{buf[0], 0, 64}, {buf[1], 0, 64 * 4}}; VkDescriptorImageInfo ii = {samp, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi[0], NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[1], NULL}};
    vkUpdateDescriptorSets(g_vk.dev, 3, wr, 0, NULL);
    VkCommandBuffer cb = vk_once_begin();
    vk_image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                     VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue zero = {{0}}; VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &rr);
    vk_image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    if (!vk_once_end(cb)) { const uint32_t *o = mem[1].map; got = o[0]; for (int i = 1; i < 64; i++) if (o[i] != got) got = ~1u; }
    vkDestroyPipeline(g_vk.dev, p, NULL); vkDestroyShaderModule(g_vk.dev, m, NULL); vkDestroyDescriptorPool(g_vk.dev, dp, NULL);
    vkDestroyPipelineLayout(g_vk.dev, pl, NULL); vkDestroyDescriptorSetLayout(g_vk.dev, dsl, NULL);
    vkDestroySampler(g_vk.dev, samp, NULL); vkDestroyImageView(g_vk.dev, view, NULL); vkDestroyImage(g_vk.dev, img, NULL); vk_free_mem(&imem);
    for (int k = 0; k < 2; k++) { vkDestroyBuffer(g_vk.dev, buf[k], NULL); vk_free_mem(&mem[k]); }
    return got;
}

// walk() over a texel buffer table of `texels` texels (test_guard_b.comp)
static uint32_t walk_buf(const uint32_t *words, size_t n, uint32_t word, int32_t count, uint32_t texels) {
    uint32_t got = ~0u;
    VkBuffer buf[3] = {0}; vk_alloc mem[3] = {0}; VkDeviceSize size[3] = {64, 64 * 4, (VkDeviceSize)texels * 4};
    VkBufferUsageFlags use[3] = {VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT};
    for (int k = 0; k < 3; k++) {
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size[k], use[k], VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
        if (vkCreateBuffer(g_vk.dev, &bci, NULL, &buf[k])) return got;
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, buf[k], &mr);
        if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &mem[k]) || !mem[k].map) return got;
        vkBindBufferMemory(g_vk.dev, buf[k], mem[k].mem, mem[k].off);
    }
    memset(mem[0].map, 0, 64); memcpy((uint8_t *)mem[0].map + word * 4, &count, 4); memset(mem[1].map, 0xff, 64 * 4); memset(mem[2].map, 0, (size_t)size[2]);
    VkBufferViewCreateInfo bvi = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL, 0, buf[2], VK_FORMAT_R32_SFLOAT, 0, VK_WHOLE_SIZE};
    VkBufferView bv; if (vkCreateBufferView(g_vk.dev, &bvi, NULL, &bv)) return got;
    VkDescriptorSetLayoutBinding b[3] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, b};
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkShaderModule m; VkPipeline p; VkDescriptorPool dp; VkDescriptorSet ds;
    vkCreateDescriptorSetLayout(g_vk.dev, &dl, NULL, &dsl);
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 0, NULL};
    vkCreatePipelineLayout(g_vk.dev, &pli, NULL, &pl);
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, n * 4, words};
    if (vkCreateShaderModule(g_vk.dev, &mi, NULL, &m)) return got;
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                                      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", NULL}, pl, VK_NULL_HANDLE, -1};
    if (vkCreateComputePipelines(g_vk.dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)) return got;
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 3, ps};
    vkCreateDescriptorPool(g_vk.dev, &dpi, NULL, &dp);
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dp, 1, &dsl};
    vkAllocateDescriptorSets(g_vk.dev, &ai, &ds);
    VkDescriptorBufferInfo bi[2] = {{buf[0], 0, 64}, {buf[1], 0, 64 * 4}};
    VkWriteDescriptorSet wr[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi[0], NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, NULL, NULL, &bv},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[1], NULL}};
    vkUpdateDescriptorSets(g_vk.dev, 3, wr, 0, NULL);
    VkCommandBuffer cb = vk_once_begin();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    if (!vk_once_end(cb)) { const uint32_t *o = mem[1].map; got = o[0]; for (int i = 1; i < 64; i++) if (o[i] != got) got = ~1u; }
    vkDestroyPipeline(g_vk.dev, p, NULL); vkDestroyShaderModule(g_vk.dev, m, NULL); vkDestroyDescriptorPool(g_vk.dev, dp, NULL);
    vkDestroyPipelineLayout(g_vk.dev, pl, NULL); vkDestroyDescriptorSetLayout(g_vk.dev, dsl, NULL); vkDestroyBufferView(g_vk.dev, bv, NULL);
    for (int k = 0; k < 3; k++) { vkDestroyBuffer(g_vk.dev, buf[k], NULL); vk_free_mem(&mem[k]); }
    return got;
}

// The corpus part (NXRT_TEST_CORPUS, nxrt.vk_guard_corpus_*): each guarded shader of each shader cache through vksh_make, with every rewrite this process's
// GPU and switches give it in the runtime's order, then made into a pipeline with validation on. Returns the caches found
static int corpus(const char *list) {
    static const struct { const char *key; int want; } g[] = {{"634565ab77c61463", 2}, {"560a0262fe47818a", 4}, {"2cb5df5cb96b324e", 1}};
    char dirs[4096]; snprintf(dirs, sizeof dirs, "%s", list); int found = 0;
    for (char *d = strtok(dirs, "|"); d; d = strtok(NULL, "|")) {
        int here = 0;
        for (int k = 0; k < 3; k++) {
            char ps[1200], pj[1200]; snprintf(ps, sizeof ps, "%s/vk/%s.spv", d, g[k].key); snprintf(pj, sizeof pj, "%s/vk/%s.json", d, g[k].key);
            FILE *fs = fopen(ps, "rb"), *fj = fopen(pj, "rb");
            if (!fs || !fj) { if (fs) fclose(fs); if (fj) fclose(fj); continue; }
            fseek(fs, 0, SEEK_END); long ns = ftell(fs); fseek(fs, 0, SEEK_SET); fseek(fj, 0, SEEK_END); long nj = ftell(fj); fseek(fj, 0, SEEK_SET);
            uint8_t *spv = malloc(ns > 0 ? ns : 4); char *js = malloc(nj > 0 ? nj : 4);
            int ok = fread(spv, 1, ns, fs) == (size_t)ns && fread(js, 1, nj, fj) == (size_t)nj; fclose(fs); fclose(fj);
            if (ok && ns > 20) {
                here = 1;
                // the translation's own validation messages (a module made from the file as it is), then the guarded one's
                uint32_t e0 = atomic_load(&g_vk.val_errors); VkShaderModule raw = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, (size_t)ns, (const uint32_t *)spv};
                vkCreateShaderModule(g_vk.dev, &mi, NULL, &raw); if (raw) vkDestroyShaderModule(g_vk.dev, raw, NULL);
                uint32_t e1 = atomic_load(&g_vk.val_errors); const char *why = NULL; VkPipelineLayout pl = VK_NULL_HANDLE;
                vks_shader *s = vksh_make(g[k].key, spv, (size_t)ns, js, (size_t)nj, &why);
                VkPipeline p = s ? vkp_compute(s, &pl) : VK_NULL_HANDLE;
                uint32_t e2 = atomic_load(&g_vk.val_errors);
                printf("corpus %s: %s: %u guarded comparisons or caps, %s, %u validation errors (%u from the translation as it is)\n", d, g[k].key,
                       s ? s->guard_cmp : 0, p ? "pipeline made" : "no pipeline", e2 - e1, e1 - e0);
                CHECK(s && s->guard_cmp == g[k].want, "corpus %s: %s guarded (%u of %d)", d, g[k].key, s ? s->guard_cmp : 0, g[k].want);
                CHECK(p != VK_NULL_HANDLE && e2 == e1, "corpus %s: %s's guarded pipeline made, no validation errors of its own (%u)", d, g[k].key, e2 - e1);
                (void)p;   // kept, as the runtime keeps its shaders and pipelines: the pipeline maps hold their handles
            }
            free(spv); free(js);
        }
        found += here;
    }
    return found;
}

static int corpus_none;   // a corpus was asked for and none was found: skipped (77)
int main(void) {
    crash_install();
    size_t n = sizeof test_guard_comp_spv / 4; uint32_t *w = copy_words(test_guard_comp_spv, n); int nl = -1;
    size_t gn = vkspv_limit_count(&w, n, "cp_c3", 0, "cp_t_tcb_10", 5, &nl);
    CHECK(nl == 2 && gn > n, "the count's two comparisons limited (%d, %zu -> %zu words)", nl, n, gn);
    size_t un = sizeof test_guard_u_comp_spv / 4; uint32_t *u = copy_words(test_guard_u_comp_spv, un); int ul = -1;
    size_t ugn = vkspv_limit_count(&u, un, "cp_c3", 13, "cp_t_tcb_12", 1, &ul);
    CHECK(ul == 2 && ugn > un, "word 13, unsigned: the count's two comparisons limited (%d, %zu -> %zu words)", ul, un, ugn);
    // NXRT_TEST_SPV=<a translated .spv> (634565ab77c61463 or 560a0262fe47818a, by its file name, from a shader cache) is limited instead of the test shader for the file NXRT_TEST_SPV_OUT names
    const char *tp = getenv("NXRT_TEST_SPV"), *op = getenv("NXRT_TEST_SPV_OUT");
    uint32_t *tw = NULL; size_t tn = 0; int tl = 0;
    if (tp && *tp) {
        FILE *f = fopen(tp, "rb"); long sz = 0;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); tw = malloc(sz > 0 ? sz : 4); if (fread(tw, 1, sz, f) != (size_t)sz) sz = 0; fclose(f); }
        if (tw && sz > 20) {
            tn = (size_t)sz / 4;
            if (strstr(tp, "560a0262")) { int a = 0, b = 0; tn = vkspv_limit_count(&tw, tn, "cp_c3", 12, "cp_t_tcb_10", 1, &a); tn = vkspv_limit_count(&tw, tn, "cp_c3", 13, "cp_t_tcb_12", 1, &b); tl = a && b ? a + b : 0; }
            else if (strstr(tp, "2cb5df5c")) tn = vkspv_cap_bound(&tw, tn, 0, 16384, &tl);
            else tn = vkspv_limit_count(&tw, tn, "cp_c3", 0, "cp_t_tcb_10", 5, &tl);
        }
        printf("%s: %d comparison%s limited, %ld -> %zu words\n", tp, tl, tl == 1 ? "" : "s", sz / 4, tn);
        CHECK(tl >= 1, "the translated shader's counts are limited");
    }
    if (op && *op) { FILE *g = fopen(op, "wb"); if (g) { if (tw) fwrite(tw, 4, tn, g); else fwrite(w, 4, gn, g); fclose(g); } }
    free(tw);
    uint32_t *o = copy_words(test_guard_comp_spv, n); int ol = -1;
    CHECK(vkspv_limit_count(&o, n, "cp_c4", 0, "cp_t_tcb_10", 5, &ol) == n && ol == 0 && !memcmp(o, test_guard_comp_spv, n * 4), "another constant buffer's name: left alone");
    free(o);
    o = copy_words(test_guard_u_comp_spv, un); ol = -1;
    CHECK(vkspv_limit_count(&o, un, "cp_c3", 12, "cp_t_tcb_12", 1, &ol) == un && ol == 0 && !memcmp(o, test_guard_u_comp_spv, un * 4), "another word of the buffer: left alone");
    free(o);
    size_t vn = sizeof test_guard_v_comp_spv / 4; uint32_t *v = copy_words(test_guard_v_comp_spv, vn); int vc = -1;
    uint32_t vb = named(v, vn, "bound"), vnn = named(v, vn, "n");
    size_t vgn = vkspv_cap_bound(&v, vn, vb, 100, &vc);
    CHECK(vb && vc == 1 && vgn == vn + 11, "a bound kept in a variable: its one store capped (%zu -> %zu words)", vn, vgn);
    o = copy_words(test_guard_v_comp_spv, vn); ol = -1;
    CHECK(vkspv_cap_bound(&o, vn, vnn, 100, &ol) == vn && ol == 0 && !memcmp(o, test_guard_v_comp_spv, vn * 4), "a variable stored twice and never a bound: left alone");
    CHECK(vkspv_cap_bound(&o, vn, 0x7fffffff, 100, &ol) == vn && ol == 0, "a variable past the ids: left alone");
    uint32_t *fo = copy_words(test_guard_v_comp_spv, vn); int fc = -1; size_t fgn = vkspv_cap_bound(&fo, vn, 0, 100, &fc);
    CHECK(fc == 1 && fgn == vgn && !memcmp(fo, v, vgn * 4), "the bound found by its shape (no id): the same edit as by its id");
    free(fo);
    // A texel buffer table (Dim Buffer, as the kit's corpus has 634565ab's and 560a0262's): the size query is the buffer's texel count
    size_t bn = sizeof test_guard_b_comp_spv / 4; uint32_t *bw = copy_words(test_guard_b_comp_spv, bn); int bl = -1;
    size_t bgn = vkspv_limit_count(&bw, bn, "cp_c3", 13, "cp_t_tcb_12", 1, &bl);
    int lod = 0, size_q = 0;
    for (size_t i = 5; i < bgn && bw[i] >> 16; i += bw[i] >> 16) { lod += (bw[i] & 0xffff) == 103; size_q += (bw[i] & 0xffff) == 104; }
    CHECK(bl == 2 && bgn > bn && !lod && size_q == 2, "a texel buffer table: both comparisons limited by its texel count, no size-with-level query (%d, %d, %d)", bl, lod, size_q);
    free(o);
    CHECK(vkspv_loop_guard_wanted(0x10de, NULL) == 1 && vkspv_loop_guard_wanted(0x1002, NULL) == 2 && vkspv_loop_guard_wanted(0x8086, NULL) == 2 && vkspv_loop_guard_wanted(0x10005, NULL) == 2,
          "the guards by default: the table-size ones on NVIDIA, constant caps elsewhere (AMD's Windows driver crashed making the table-size one)");
    CHECK(vkspv_loop_guard_wanted(0x1002, "1") == 1 && vkspv_loop_guard_wanted(0x10de, "cap") == 2 && !vkspv_loop_guard_wanted(0x10de, "0") && !vkspv_loop_guard_wanted(0x1002, "0"),
          "NXRT_VK_LOOP_GUARD: 1 the table-size guards, cap the caps, 0 none, on any GPU");
    // The constant cap (no tex_var): the same comparisons limited, without the size query AMD's compiler crashed on
    uint32_t *wc = copy_words(test_guard_comp_spv, n), *uc = copy_words(test_guard_u_comp_spv, un); int cl = -1, ucl = -1;
    size_t cgn = vkspv_limit_count(&wc, n, "cp_c3", 0, NULL, 1, &cl), ucgn = vkspv_limit_count(&uc, un, "cp_c3", 13, NULL, 1, &ucl);
    int queries = 0;
    for (size_t i = 5; i < cgn && wc[i] >> 16; i += wc[i] >> 16) queries += (wc[i] & 0xffff) == 103;
    for (size_t i = 5; i < ucgn && uc[i] >> 16; i += uc[i] >> 16) queries += (uc[i] & 0xffff) == 103;
    CHECK(cl == 2 && ucl == 2 && cgn > n && ucgn > un && !queries, "capped at 16384: both counts' comparisons limited, no size query (%d, %d)", cl, ucl);
    _putenv("NXRT_VK_CHECKPOINTS=markers");   // AMD's buffer markers instead of NVIDIA's checkpoints (checked below)
    // The GPU part gives the edited shaders huge counts: an edit that did not apply would hang the GPU, so it runs only when every edit applied
    int edited = nl == 2 && ul == 2 && vc == 1 && cl == 2 && ucl == 2 && bl == 2;
    CHECK(edited, "every edit applied (otherwise the GPU part is left out: its huge counts would hang the GPU)");
    int rc = edited ? vt_up(0, 1) : 77;
    if (!rc) {
        CHECK(walk(test_guard_comp_spv, n, 0, 3, 40, 1) == 3, "the original walks 3 items of 3");
        CHECK(walk(w, gn, 0, 3, 40, 1) == 3, "limited: a sane count is unchanged (3 of a 40-texel table's 8)");
        CHECK(walk(w, gn, 0, 8, 40, 1) == 8, "limited: the table's every item (8)");
        uint64_t t0 = now_ns(); uint32_t g = walk(w, gn, 0, 1000000000, 40, 1);
        CHECK(g == 8, "limited: a count of a billion walks the table's 8 items (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        CHECK(walk(w, gn, 0, 1000000000, 16384, 2) == (16384 + 16384 + 4) / 5, "limited: two rows of 16384 texels hold %u items", (16384u + 16384u + 4u) / 5);
        CHECK(walk(w, gn, 0, -5, 40, 1) == 0, "limited: a count below 1 walks none, as before");
        CHECK(walk(test_guard_u_comp_spv, un, 13, 3, 40, 1) == 3, "unsigned: the original walks 3 items of 3");
        CHECK(walk(u, ugn, 13, 3, 40, 1) == 3 && walk(u, ugn, 13, 0, 40, 1) == 0, "unsigned, limited: a sane count and none unchanged");
        t0 = now_ns(); g = walk(u, ugn, 13, -1, 40, 1);
        CHECK(g == 40, "unsigned, limited: a count of 0xffffffff walks the table's 40 items (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        CHECK(walk(u, ugn, 13, 0x3f800000, 16384, 2) == 32768, "unsigned, limited: 1.0f read as a count walks two rows' 32768 items");
        CHECK(walk(test_guard_v_comp_spv, vn, 13, 3, 16, 1) == 3, "a bound in a variable: the original walks 3 entries of 3");
        CHECK(walk(v, vgn, 13, 3, 16, 1) == 3 && walk(v, vgn, 13, 0, 16, 1) == 0 && walk(v, vgn, 13, 100, 16, 1) == 100, "capped: 3, none and 100 unchanged");
        t0 = now_ns(); g = walk(v, vgn, 13, -1, 16, 1);
        CHECK(g == 100, "capped: a count of 0xffffffff walks 100 entries (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        CHECK(walk(wc, cgn, 0, 3, 40, 1) == 3 && walk(wc, cgn, 0, -5, 40, 1) == 0, "capped at 16384: a sane count and one below 1 unchanged");
        CHECK(walk(uc, ucgn, 13, 3, 40, 1) == 3 && walk(uc, ucgn, 13, 0, 40, 1) == 0, "capped at 16384, unsigned: a sane count and none unchanged");
        t0 = now_ns(); g = walk(uc, ucgn, 13, -1, 16384, 2);
        CHECK(g == 16384, "capped at 16384, unsigned: a count of 0xffffffff walks 16384 items (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        CHECK(walk(uc, ucgn, 13, 0x3f800000, 16384, 2) == 16384, "capped at 16384, unsigned: 1.0f read as a count walks 16384 items");
        CHECK(walk_buf(test_guard_b_comp_spv, bn, 13, 3, 40) == 3, "a texel buffer table: the original walks 3 items of 3");
        CHECK(walk_buf(bw, bgn, 13, 3, 40) == 3 && walk_buf(bw, bgn, 13, 0, 40) == 0, "a texel buffer table, limited: a sane count and none unchanged");
        t0 = now_ns(); g = walk_buf(bw, bgn, 13, -1, 40);
        CHECK(g == 40, "a texel buffer table, limited: a count of 0xffffffff walks the buffer's 40 texels (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        uint32_t bt = g_vk.props.limits.maxTexelBufferElements < 70000 ? g_vk.props.limits.maxTexelBufferElements : 70000;   // past 16 bits where the device allows it
        g = walk_buf(bw, bgn, 13, 0x3f800000, bt);
        if (g_vk.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && g == 65536 && bt > 65536)   // lavapipe: its size query of a texel buffer stops at 16 bits
            printf("note: on this CPU device a %u-texel buffer table walks %u items: its size query stops at 65536 (the device allows %u); not checked here\n",
                   bt, g, g_vk.props.limits.maxTexelBufferElements);
        else CHECK(g == bt, "a texel buffer table, limited: 1.0f read as a count walks the buffer's %u texels (%u; the device allows %u)", bt, g, g_vk.props.limits.maxTexelBufferElements);
        // A driver that crashes making a pipeline (vk_pipe.c), staged by NXRT_VK_TEST_PIPE_FAULT: 634565ab's key on test_guard.comp (its shape) is made
        // again from the module as translated; a shader without one is left out. Either way the test goes on
        static const char jc[] = "{\"stage\":\"cs\",\"local_size\":[64,1,1],\"bindings\":["
            "{\"set\":0,\"binding\":0,\"descriptor\":\"uniform_buffer\",\"count\":1,\"nvn\":{\"kind\":\"cbuf\",\"reg\":3}},"
            "{\"set\":0,\"binding\":1,\"descriptor\":\"combined_image_sampler\",\"count\":1,\"nvn\":{\"kind\":\"tex\",\"reg\":0,\"sampler\":{\"kind\":\"samp_tex\",\"reg\":0}}},"
            "{\"set\":0,\"binding\":2,\"descriptor\":\"storage_buffer\",\"count\":1,\"nvn\":{\"kind\":\"sbuf\",\"reg\":0}}],\"inputs\":[],\"outputs\":[]}";
        const char *why = NULL; VkPipelineLayout pl = VK_NULL_HANDLE;
        _putenv("NXRT_VK_MANIFEST=0");
        vks_shader *gs = vksh_make("634565ab77c61463", (const uint8_t *)test_guard_comp_spv, sizeof test_guard_comp_spv, jc, sizeof jc - 1, &why);
        CHECK(gs && gs->mod && gs->mod_plain, "634565ab's shape made with its guard, the module as translated kept beside it (%s)", why ? why : "ok");
        vks_shader *ps = vksh_make("0123456789abcdef", (const uint8_t *)test_guard_comp_spv, sizeof test_guard_comp_spv, jc, sizeof jc - 1, &why);
        CHECK(ps && ps->mod && !ps->mod_plain, "another key: no guard, no second module (%s)", why ? why : "ok");
        _putenv("NXRT_VK_TEST_PIPE_FAULT=634565ab77c61463");
        VkPipeline gp = gs ? vkp_compute(gs, &pl) : VK_NULL_HANDLE;
        _putenv("NXRT_VK_TEST_PIPE_FAULT=0123456789abcdef");
        VkPipeline pp = ps ? vkp_compute(ps, &pl) : (VkPipeline)1;
        _putenv("NXRT_VK_TEST_PIPE_FAULT=");
        CHECK(gp != VK_NULL_HANDLE, "a crash making the guarded pipeline is caught, and the pipeline made as translated");
        CHECK(pp == VK_NULL_HANDLE && ps && vkp_compute(ps, &pl) == VK_NULL_HANDLE, "a crash with nothing to fall back to: the pipeline left out, and stays so");
        // AMD's buffer markers (VK_AMD_buffer_marker; NVIDIA has it too), taken instead of checkpoints by NXRT_VK_CHECKPOINTS=markers (set before the device)
        if (g_vk.has.buffer_marker) {
            uint64_t f0 = 0, s0 = vk_markers_now(&f0);
            VkCommandBuffer cb = vk_once_begin();
            vk_checkpoint(cb, 'c', "markertest", 1, 1, 1, 0, NULL);
            int bad = vk_once_end(cb);
            uint64_t f1 = 0, s1 = vk_markers_now(&f1);
            CHECK(!bad && s1 > s0 && f1 == s1, "AMD buffer markers: a checkpoint's number written as started and finished (%llu %llu -> %llu %llu)",
                  (unsigned long long)s0, (unsigned long long)f0, (unsigned long long)s1, (unsigned long long)f1);
        } else printf("no VK_AMD_buffer_marker on this GPU: markers not checked\n");
        (void)gp;   // the shaders and pipeline stay, as the runtime's do (its pipeline maps hold their handles; freed with the device)
        const char *cl = getenv("NXRT_TEST_CORPUS");   // nxrt.vk_guard_corpus_*: the guarded shaders of real shader caches
        if (cl && *cl) { int nc = corpus(cl); printf("corpus: %d shader cache%s checked\n", nc, nc == 1 ? "" : "s"); if (!nc) corpus_none = 1; }
        uint32_t verr = atomic_load(&g_vk.val_errors);
        vt_down(0);
        CHECK(verr == 0, "no validation errors (%u)", verr);
    }
    free(w); free(u); free(v); free(wc); free(uc); free(bw);
    if (rc == 77) printf("GPU part skipped (no device)\n");
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    if (corpus_none && !g_fail) { printf("no shader cache of NXRT_TEST_CORPUS found: skipped\n"); return 77; }
    return g_fail ? 1 : 0;
}
