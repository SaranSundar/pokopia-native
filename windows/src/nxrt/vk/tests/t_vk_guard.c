/* vkspv_limit_count (D-1 on 1.3.0, shader 634565ab77c61463): a loop whose count is a constant buffer's first word walks no more items than its texel table
   holds. test_guard.comp has the shader's shape; the edit is checked on the words and, on a GPU, with a sane count (unchanged) and a garbage one (the table's
   items, at once) for a table of one row and of two. The GPU part is skipped without a device. */
#include "vk_test.h"
#include "vk_shader.h"
#include "test_guard_comp.h"

NXRT_EARLY_RESERVE()

static uint32_t *copy_words(const uint32_t *w, size_t n) { uint32_t *c = malloc(n * 4); memcpy(c, w, n * 4); return c; }

// One dispatch of 64 lanes with the count's bits in cp_c3's first word and a w x h table; how many items lane 0 walked (all lanes the same), or ~0u
static uint32_t walk(const uint32_t *words, size_t n, int32_t count, uint32_t w, uint32_t h) {
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
    memset(mem[0].map, 0, 64); memcpy(mem[0].map, &count, 4); memset(mem[1].map, 0xff, 64 * 4);
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

int main(void) {
    crash_install();
    size_t n = sizeof test_guard_comp_spv / 4; uint32_t *w = copy_words(test_guard_comp_spv, n); int nl = -1;
    size_t gn = vkspv_limit_count(&w, n, "cp_c3", "cp_t_tcb_10", 5, &nl);
    CHECK(nl == 2 && gn > n, "the count's two comparisons limited (%d, %zu -> %zu words)", nl, n, gn);
    // NXRT_TEST_SPV=<a translated .spv> (634565ab77c61463 from a shader cache) is limited instead of the test shader for the file NXRT_TEST_SPV_OUT names
    const char *tp = getenv("NXRT_TEST_SPV"), *op = getenv("NXRT_TEST_SPV_OUT");
    uint32_t *tw = NULL; size_t tn = 0; int tl = 0;
    if (tp && *tp) {
        FILE *f = fopen(tp, "rb"); long sz = 0;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); tw = malloc(sz > 0 ? sz : 4); if (fread(tw, 1, sz, f) != (size_t)sz) sz = 0; fclose(f); }
        if (tw && sz > 20) tn = vkspv_limit_count(&tw, (size_t)sz / 4, "cp_c3", "cp_t_tcb_10", 5, &tl);
        printf("%s: %d comparison%s limited, %ld -> %zu words\n", tp, tl, tl == 1 ? "" : "s", sz / 4, tn);
        CHECK(tl >= 1, "the translated shader's count is limited");
    }
    if (op && *op) { FILE *g = fopen(op, "wb"); if (g) { if (tw) fwrite(tw, 4, tn, g); else fwrite(w, 4, gn, g); fclose(g); } }
    free(tw);
    uint32_t *o = copy_words(test_guard_comp_spv, n); int ol = -1;
    CHECK(vkspv_limit_count(&o, n, "cp_c4", "cp_t_tcb_10", 5, &ol) == n && ol == 0 && !memcmp(o, test_guard_comp_spv, n * 4), "another constant buffer's name: left alone");
    free(o);
    CHECK(vkspv_loop_guard_wanted(0x10de, NULL) && !vkspv_loop_guard_wanted(0x1002, NULL) && !vkspv_loop_guard_wanted(0x8086, NULL) && !vkspv_loop_guard_wanted(0x10005, NULL),
          "the guard by default: NVIDIA only (AMD's Windows driver crashed making it)");
    CHECK(vkspv_loop_guard_wanted(0x1002, "1") && !vkspv_loop_guard_wanted(0x10de, "0") && !vkspv_loop_guard_wanted(0x1002, "0"), "NXRT_VK_LOOP_GUARD: 1 on, 0 off, on any GPU");
    int rc = vt_up(0, 1);
    if (!rc) {
        CHECK(walk(test_guard_comp_spv, n, 3, 40, 1) == 3, "the original walks 3 items of 3");
        CHECK(walk(w, gn, 3, 40, 1) == 3, "limited: a sane count is unchanged (3 of a 40-texel table's 8)");
        CHECK(walk(w, gn, 8, 40, 1) == 8, "limited: the table's every item (8)");
        uint64_t t0 = now_ns(); uint32_t g = walk(w, gn, 1000000000, 40, 1);
        CHECK(g == 8, "limited: a count of a billion walks the table's 8 items (%u, %.1f ms)", g, (now_ns() - t0) / 1e6);
        CHECK(walk(w, gn, 1000000000, 16384, 2) == (16384 + 16384 + 4) / 5, "limited: two rows of 16384 texels hold %u items", (16384u + 16384u + 4u) / 5);
        CHECK(walk(w, gn, -5, 40, 1) == 0, "limited: a count below 1 walks none, as before");
        uint32_t verr = atomic_load(&g_vk.val_errors);
        vt_down(0);
        CHECK(verr == 0, "no validation errors (%u)", verr);
    }
    free(w);
    if (rc == 77) printf("GPU part skipped (no device)\n");
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
