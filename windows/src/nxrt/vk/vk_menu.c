// The in-game menu's drawing (vk_menu.h): the quads front/menu.c lays out (the menu, the FPS counter), one instance each, from a host-visible buffer per
// present slot; the font atlas uploaded in the first present that shows either. Nothing is made before then.
#include "vk_menu.h"
#include "menu.h"
#include "menu_vert.h"
#include "menu_frag.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 3, MAXQ = 16384 };
static struct {
    int ready, failed, uploaded;
    VkPipeline pipe; VkFormat pipe_fmt; VkPipelineLayout pl; VkDescriptorSetLayout dsl; VkDescriptorPool dp; VkDescriptorSet ds; VkSampler samp;
    VkImage atlas; VkImageView view; vk_alloc atlas_mem; VkBuffer stage; vk_alloc stage_mem;
    VkBuffer vb[SLOTS]; vk_alloc vb_mem[SLOTS]; int n[SLOTS];
    mn_list list;
} g_m;

static VkBuffer host_buffer(VkDeviceSize size, VkBufferUsageFlags use, vk_alloc *mem) {
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, use, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    VkBuffer b = VK_NULL_HANDLE; if (vkCreateBuffer(g_vk.dev, &bci, NULL, &b) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, b, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, mem) || !mem->map) { vkDestroyBuffer(g_vk.dev, b, NULL); return VK_NULL_HANDLE; }
    vkBindBufferMemory(g_vk.dev, b, mem->mem, mem->off);
    return b;
}

static int setup(void) {
    const uint8_t *px; int aw, ah; menu_font_atlas(&px, &aw, &ah);
    VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, {(uint32_t)aw, (uint32_t)ah, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(g_vk.dev, &ic, NULL, &g_m.atlas) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_vk.dev, g_m.atlas, &mr);
    if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &g_m.atlas_mem)) return -1;
    vkBindImageMemory(g_vk.dev, g_m.atlas, g_m.atlas_mem.mem, g_m.atlas_mem.off);
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, g_m.atlas, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8_UNORM, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(g_vk.dev, &vci, NULL, &g_m.view) != VK_SUCCESS) return -1;
    if (!(g_m.stage = host_buffer((VkDeviceSize)aw * ah, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_m.stage_mem))) return -1;
    memcpy(g_m.stage_mem.map, px, (size_t)aw * ah);
    for (int i = 0; i < SLOTS; i++) if (!(g_m.vb[i] = host_buffer((VkDeviceSize)MAXQ * sizeof(mn_quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &g_m.vb_mem[i]))) return -1;
    VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, NULL, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0, VK_FALSE, 1, VK_FALSE,
                               VK_COMPARE_OP_NEVER, 0, 0, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE};
    if (vkCreateSampler(g_vk.dev, &sci, NULL, &g_m.samp) != VK_SUCCESS) return -1;
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &b};
    if (vkCreateDescriptorSetLayout(g_vk.dev, &dl, NULL, &g_m.dsl) != VK_SUCCESS) return -1;
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_m.dsl, 1, &pcr};
    if (vkCreatePipelineLayout(g_vk.dev, &pl, NULL, &g_m.pl) != VK_SUCCESS) return -1;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &ps};
    if (vkCreateDescriptorPool(g_vk.dev, &dp, NULL, &g_m.dp) != VK_SUCCESS) return -1;
    VkDescriptorSetAllocateInfo da = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_m.dp, 1, &g_m.dsl};
    if (vkAllocateDescriptorSets(g_vk.dev, &da, &g_m.ds) != VK_SUCCESS) return -1;
    VkDescriptorImageInfo dii = {g_m.samp, g_m.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, g_m.ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dii, NULL, NULL};
    vkUpdateDescriptorSets(g_vk.dev, 1, &w, 0, NULL);
    return 0;
}

static VkShaderModule shader(const uint32_t *code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, code};
    VkShaderModule m = VK_NULL_HANDLE; vkCreateShaderModule(g_vk.dev, &ci, NULL, &m); return m;
}
static int make_pipeline(VkFormat fmt) {
    if (g_m.pipe && g_m.pipe_fmt == fmt) return 0;
    if (g_m.pipe) { vkDestroyPipeline(g_vk.dev, g_m.pipe, NULL); g_m.pipe = VK_NULL_HANDLE; }
    VkShaderModule vs = shader(menu_vert_spv, sizeof menu_vert_spv), fs = shader(menu_frag_spv, sizeof menu_frag_spv);
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL}};
    VkVertexInputBindingDescription vb = {0, sizeof(mn_quad), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription va[5] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mn_quad, x0)}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mn_quad, u0)},
                                               {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(mn_quad, r)}, {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(mn_quad, rgba)},
                                               {4, 0, VK_FORMAT_R32_SINT, offsetof(mn_quad, kind)}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 1, &vb, 5, va};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL,
                                                 VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba = {VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                                              VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, 0xf};
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, &ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &fmt, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    VkGraphicsPipelineCreateInfo pci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri, 0, 2, st, &vi, &ia, NULL, &vp, &rs, &ms, NULL, &cb, &ds, g_m.pl,
                                        VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1};
    VkResult r = vkCreateGraphicsPipelines(g_vk.dev, g_vk.pcache, 1, &pci, NULL, &g_m.pipe);
    vkDestroyShaderModule(g_vk.dev, vs, NULL); vkDestroyShaderModule(g_vk.dev, fs, NULL);
    if (r != VK_SUCCESS) { VKRED("cannot create the in-game menu's pipeline (%d)", r); g_m.pipe = VK_NULL_HANDLE; return -1; }
    g_m.pipe_fmt = fmt;
    return 0;
}

int vkmn_prepare(VkCommandBuffer cb, VkExtent2D ext, int slot) {
    if (slot < 0 || slot >= SLOTS) return 0;
    g_m.n[slot] = 0;
    if (!menu_active() || g_m.failed) return 0;
    if (!g_m.ready) {
        if (setup()) { VKRED("the in-game menu cannot be drawn (no memory for its font or buffers)"); g_m.failed = 1; return 0; }
        g_m.ready = 1;
    }
    menu_build(&g_m.list, (int)ext.width, (int)ext.height);
    if (!g_m.list.n) return 0;
    if (!g_m.uploaded) {   // the font, once
        int aw, ah; const uint8_t *px; menu_font_atlas(&px, &aw, &ah);
        vk_image_barrier(cb, g_m.atlas, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy c = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {(uint32_t)aw, (uint32_t)ah, 1}};
        vkCmdCopyBufferToImage(cb, g_m.stage, g_m.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        vk_image_barrier(cb, g_m.atlas, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        g_m.uploaded = 1;
    }
    int n = g_m.list.n < MAXQ ? g_m.list.n : MAXQ;
    memcpy(g_m.vb_mem[slot].map, g_m.list.q, (size_t)n * sizeof(mn_quad));
    return g_m.n[slot] = n;
}

void vkmn_draw(VkCommandBuffer cb, VkExtent2D ext, VkFormat fmt, int slot) {
    if (slot < 0 || slot >= SLOTS || !g_m.n[slot] || make_pipeline(fmt)) return;
    VkViewport vp = {0, 0, (float)ext.width, (float)ext.height, 0, 1};
    VkRect2D sc = {{0, 0}, ext};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_m.pipe);
    vkCmdSetViewport(cb, 0, 1, &vp); vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_m.pl, 0, 1, &g_m.ds, 0, NULL);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers2(cb, 0, 1, &g_m.vb[slot], &off, NULL, NULL);
    float pc[2] = {(float)ext.width, (float)ext.height};
    vkCmdPushConstants(cb, g_m.pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof pc, pc);
    vkCmdDraw(cb, 6, (uint32_t)g_m.n[slot], 0, 0);
}

void vkmn_shutdown(void) {
    if (g_m.pipe) vkDestroyPipeline(g_vk.dev, g_m.pipe, NULL);
    if (g_m.ready) {
        vkDestroyPipelineLayout(g_vk.dev, g_m.pl, NULL); vkDestroyDescriptorPool(g_vk.dev, g_m.dp, NULL); vkDestroyDescriptorSetLayout(g_vk.dev, g_m.dsl, NULL);
        vkDestroySampler(g_vk.dev, g_m.samp, NULL); vkDestroyImageView(g_vk.dev, g_m.view, NULL); vkDestroyImage(g_vk.dev, g_m.atlas, NULL); vk_free_mem(&g_m.atlas_mem);
        vkDestroyBuffer(g_vk.dev, g_m.stage, NULL); vk_free_mem(&g_m.stage_mem);
        for (int i = 0; i < SLOTS; i++) { vkDestroyBuffer(g_vk.dev, g_m.vb[i], NULL); vk_free_mem(&g_m.vb_mem[i]); }
    }
    free(g_m.list.q);
    memset(&g_m, 0, sizeof g_m);
}
