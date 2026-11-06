/* shaders/quad_args.comp (F-22, #17, #20): an indirect quad draw's NVN arguments become the indexed indirect draw vk_replay.c records over the quad index
   buffer: 6 indices per quad, at most 65536 quads and 65536 instances, the first vertex as the vertex offset, the base instance kept; read at any 4-byte
   offset. The GPU part is skipped without a device. */
#include "vk_test.h"
#define quad_args_comp_spv t_quad_args_spv   // vk_replay.c defines the header's array too
#include "quad_args_comp.h"
#undef quad_args_comp_spv
#define quad_args_comp_spv t_quad_args_spv

NXRT_EARLY_RESERVE()

// The shader over src from word at; out gets its 5 words (~0 each when it could not run)
static int run(const uint32_t *src, uint32_t nsrc, uint32_t at, uint32_t out[5]) {
    for (int i = 0; i < 5; i++) out[i] = ~0u;
    VkBuffer buf[2] = {0}; vk_alloc mem[2] = {0}; VkDeviceSize size[2] = {(VkDeviceSize)nsrc * 4, 20};
    for (int k = 0; k < 2; k++) {
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size[k], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
        if (vkCreateBuffer(g_vk.dev, &bci, NULL, &buf[k])) return 0;
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, buf[k], &mr);
        if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &mem[k]) || !mem[k].map) return 0;
        vkBindBufferMemory(g_vk.dev, buf[k], mem[k].mem, mem[k].off);
    }
    memcpy(mem[0].map, src, (size_t)nsrc * 4); memset(mem[1].map, 0xff, 20);
    VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, b};
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkShaderModule m; VkPipeline p; VkDescriptorPool dp; VkDescriptorSet ds;
    vkCreateDescriptorSetLayout(g_vk.dev, &dl, NULL, &dsl);
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 1, &pr};
    vkCreatePipelineLayout(g_vk.dev, &pli, NULL, &pl);
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof quad_args_comp_spv, quad_args_comp_spv};
    if (vkCreateShaderModule(g_vk.dev, &mi, NULL, &m)) return 0;
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
                                      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", NULL}, pl, VK_NULL_HANDLE, -1};
    if (vkCreateComputePipelines(g_vk.dev, VK_NULL_HANDLE, 1, &ci, NULL, &p)) return 0;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &ps};
    vkCreateDescriptorPool(g_vk.dev, &dpi, NULL, &dp);
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dp, 1, &dsl};
    vkAllocateDescriptorSets(g_vk.dev, &ai, &ds);
    VkDescriptorBufferInfo bi[2] = {{buf[0], 0, size[0]}, {buf[1], 0, 20}};
    VkWriteDescriptorSet wr[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[0], NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &bi[1], NULL}};
    vkUpdateDescriptorSets(g_vk.dev, 2, wr, 0, NULL);
    VkCommandBuffer cb = vk_once_begin();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
    vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &at);
    vkCmdDispatch(cb, 1, 1, 1);
    vk_mem_barrier(cb, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    int ok = !vk_once_end(cb);
    if (ok) memcpy(out, mem[1].map, 20);
    vkDestroyPipeline(g_vk.dev, p, NULL); vkDestroyShaderModule(g_vk.dev, m, NULL); vkDestroyDescriptorPool(g_vk.dev, dp, NULL);
    vkDestroyPipelineLayout(g_vk.dev, pl, NULL); vkDestroyDescriptorSetLayout(g_vk.dev, dsl, NULL);
    for (int k = 0; k < 2; k++) { vkDestroyBuffer(g_vk.dev, buf[k], NULL); vk_free_mem(&mem[k]); }
    return ok;
}

int main(void) {
    crash_install();
    int rc = vt_up(0, 1);
    if (!rc) {
        // arguments at word 0 and, after five words of something else, at word 5 (an offset that is not 16-byte aligned)
        static const struct { uint32_t in[4], want[5]; const char *what; } c[] = {
            {{8, 3, 5, 2}, {12, 3, 0, 5, 2}, "two quads, three instances, first vertex 5, base instance 2"},
            {{4, 0x3f800000u, 0, 0}, {6, 65536, 0, 0, 0}, "a count of 1.0f's bits (#17's freeze) is held to 65536 instances"},
            {{0, 0, 0, 0}, {0, 0, 0, 0, 0}, "nothing written yet: an empty draw"},
            {{127, 127, 1, 0}, {186, 127, 0, 1, 0}, "127 vertices are 31 whole quads"},
            {{0xffffffffu, 1, 0, 7}, {65536 * 6, 1, 0, 0, 7}, "at most 65536 quads"},
        };
        for (unsigned i = 0; i < sizeof c / sizeof *c; i++) for (uint32_t at = 0; at <= 5; at += 5) {
            uint32_t src[9] = {0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef}, out[5];
            memcpy(src + at, c[i].in, 16);
            int ok = run(src, 9, at, out);
            CHECK(ok && !memcmp(out, c[i].want, 20), "%s, at word %u: %u %u %u %d %u", c[i].what, at, out[0], out[1], out[2], (int32_t)out[3], out[4]);
        }
        uint32_t verr = atomic_load(&g_vk.val_errors);
        vt_down(0);
        CHECK(verr == 0, "no validation errors (%u)", verr);
    }
    if (rc == 77) printf("GPU part skipped (no device)\n");
    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
