// The in-game menu's drawing (front/menu.h lays it out): rounded panels and signed-distance text, blended over the picture in the present pass
#pragma once
#include "vk_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// The present thread, per present: before its rendering (the menu laid out for the window, its quads into the slot's buffer, the font uploaded the first
// time); returns the quads to draw (0 while the menu is closed). Slot: the present's frame, whose fence covers the slot's buffer
int vkmn_prepare(VkCommandBuffer cb, VkExtent2D ext, int slot);
void vkmn_draw(VkCommandBuffer cb, VkExtent2D ext, VkFormat fmt, int slot);   // inside the rendering, after the picture
void vkmn_shutdown(void);   // once the queue is idle

#ifdef __cplusplus
}
#endif
