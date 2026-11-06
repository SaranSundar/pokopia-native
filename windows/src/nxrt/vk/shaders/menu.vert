#version 450
// The in-game menu (vk_menu.c): one quad per instance in window pixels, a glyph of the font atlas or a rounded rectangle
layout(location = 0) in vec4 rect;     // x0 y0 x1 y1
layout(location = 1) in vec4 uvr;      // the glyph's place in the atlas: u0 v0 u1 v1
layout(location = 2) in float radius;
layout(location = 3) in vec4 color;
layout(location = 4) in int kind;      // 0 glyph, 1 rectangle (front/menu.h)
layout(push_constant) uniform PC { vec2 size; } pc;   // the window in pixels
layout(location = 0) out vec2 uv;
layout(location = 1) out vec2 local;   // pixels from the rectangle's centre
layout(location = 2) flat out vec4 shape;   // half width, half height, radius, kind
layout(location = 3) flat out vec4 col;
void main() {
    const vec2 c[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    vec2 t = c[gl_VertexIndex], p = mix(rect.xy, rect.zw, t), h = (rect.zw - rect.xy) * 0.5;
    uv = mix(uvr.xy, uvr.zw, t);
    local = p - rect.xy - h;
    shape = vec4(h, min(radius, min(h.x, h.y)), float(kind));
    col = color;
    gl_Position = vec4(p / pc.size * 2.0 - 1.0, 0.0, 1.0);
}
