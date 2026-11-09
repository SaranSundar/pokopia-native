// Anti-aliasing of the game's 3D picture (vk_aa.c): FXAA or SMAA over a presented texture right after the game's full-screen composite draws into it,
// before the HUD. NXRT_VK_AA=off|fxaa|smaa at the start (display.json "antialiasing", scripts/play.ps1), the in-game menu while playing.
#pragma once
#include "vk_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { VKAA_OFF = 0, VKAA_FXAA = 1, VKAA_SMAA = 2 };
void vkaa_set_mode(int mode);                     // any thread; the next composite uses it
int vkaa_mode(void);
int vkaa_mode_from(const char *s);                // "off" / "fxaa" / "smaa" (else SMAA, the default)
// The pass over level `level`, layer `layer` of img (created as img_fmt; w x h of its own pixels, in GENERAL layout), on the picture in its rectangle
// from 0,0 to rw x rh (all of it, or the part a game draws at a lower resolution: Pokopia's handheld 1600x900); view is the composite's attachment view
// of it (format view_fmt). Records into cb outside any render pass and leaves pipeline, descriptor and dynamic state of its own bound.
// 1 = recorded, 0 = skipped (off, a format or size it does not take, or a failure, logged)
int vkaa_run(VkCommandBuffer cb, VkImage img, VkFormat img_fmt, uint32_t level, uint32_t layer, uint32_t w, uint32_t h, uint32_t rw, uint32_t rh,
             VkImageView view, VkFormat view_fmt);
void vkaa_shutdown(void);

#ifdef __cplusplus
}
#endif
