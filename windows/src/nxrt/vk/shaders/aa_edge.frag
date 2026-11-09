#version 450
// SMAA, pass 1 of 3 (vk_aa.c): luma edges, as in "SMAA: Enhanced Subpixel Morphological Antialiasing" (Jimenez, Echevarria, Sousa, Gutierrez, 2012).
// Our own implementation. r: an edge on this pixel's left, g: on its top. An edge needs a luma step of the threshold, and of at least half the largest
// step around it (local contrast adaptation: a weak edge beside a strong one is left alone). The copy is read gamma-encoded, as perceived
layout(set = 0, binding = 0) uniform sampler2D src;
layout(location = 0) out vec4 o;
layout(push_constant) uniform PC { vec4 rcp; } pc;   // rcp.zw: the picture's rectangle from 0,0 (the copy beyond it is stale)

const float THRESHOLD = 0.1, ADAPT = 2.0;

float lum(ivec2 p) { return dot(texelFetch(src, clamp(p, ivec2(0), ivec2(pc.rcp.zw) - 1), 0).rgb, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float L = lum(p), Ll = lum(p + ivec2(-1, 0)), Lt = lum(p + ivec2(0, -1));
    vec2 d = abs(L - vec2(Ll, Lt));
    vec2 e = step(THRESHOLD, d);
    if (e.x + e.y == 0.0) discard;
    float Lr = lum(p + ivec2(1, 0)), Lb = lum(p + ivec2(0, 1));
    vec2 m = max(d, abs(L - vec2(Lr, Lb)));
    float Lll = lum(p + ivec2(-2, 0)), Ltt = lum(p + ivec2(0, -2));
    m = max(m, abs(vec2(Ll, Lt) - vec2(Lll, Ltt)));
    float top = max(m.x, m.y);
    e *= step(top, ADAPT * d);
    o = vec4(e, 0.0, 0.0);
}
