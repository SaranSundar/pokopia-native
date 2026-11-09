#version 450
// SMAA, pass 3 of 3 (vk_aa.c): neighbourhood blending. Each pixel takes its own weights toward the pixels above and to its left and its right and
// lower neighbours' weights toward it, keeps the stronger direction, and mixes with those neighbours by one bilinear read each. The copy is read
// through its sRGB view where the target is sRGB, so the mixing happens in linear light. Pixels with no weight are left as they are
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1) uniform sampler2D wts;
layout(location = 0) out vec4 o;
layout(push_constant) uniform PC { vec4 rcp; } pc;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy), sz = textureSize(wts, 0);
    vec4 here = texelFetch(wts, p, 0);
    vec4 a = vec4(p.x + 1 < sz.x ? texelFetch(wts, p + ivec2(1, 0), 0).a : 0.0,   // toward the right
                  p.y + 1 < sz.y ? texelFetch(wts, p + ivec2(0, 1), 0).g : 0.0,   // toward the one below
                  here.b, here.r);                                                // toward the left, toward the one above
    if (dot(a, vec4(1.0)) < 1e-5) discard;
    vec2 uv = gl_FragCoord.xy * pc.rcp.xy;
    bool horz = max(a.x, a.z) > max(a.y, a.w);
    vec2 w = horz ? a.xz : a.yw;
    vec2 o1 = horz ? vec2(a.x, 0.0) : vec2(0.0, a.y), o2 = horz ? vec2(-a.z, 0.0) : vec2(0.0, -a.w);
    w /= w.x + w.y;
    o = w.x * textureLod(src, uv + o1 * pc.rcp.xy, 0.0) + w.y * textureLod(src, uv + o2 * pc.rcp.xy, 0.0);
}
