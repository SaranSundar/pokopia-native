#version 450
// SMAA, pass 2 of 3 (vk_aa.c): blending weights. For each edge of a pixel the edge's two ends are searched (up to MAXS pixels each way), the crossing
// edges at the ends give its shape (an L, a Z or a U), and the line through the shape's midpoints gives how much of this pixel and of its neighbour across
// the edge lies on the other side. SMAA reads those areas from a precomputed texture; here they are computed, with the same rules as its orthogonal
// patterns (no diagonal or corner patterns). rg: the top edge (r this pixel toward the one above, g that one toward this), ba: the left edge (b this pixel
// toward the left one, a that one toward this). The search steps two pixels at a time: two gathers read the crossings and the edge's continuation for both
// (the sampler's border is zero: no edges outside the picture)
layout(set = 0, binding = 0) uniform sampler2D edges;
layout(location = 0) out vec4 o;

const int MAXS = 32;
const float SMOOTH_MAX = 32.0;   // U shapes shorter than this blend a little more (SMAA's smoothing of short U patterns)

ivec2 SZ; vec2 RCP;
vec2 E(ivec2 p) { return any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, SZ)) ? vec2(0.0) : texelFetch(edges, p, 0).rg; }
// one channel of the 2 x 2 texels around the corner (x, y): .x (x-1, y), .y (x, y), .z (x, y-1), .w (x-1, y-1)
vec4 R4(int x, int y) { return textureGather(edges, vec2(x, y) * RCP, 0); }
vec4 G4(int x, int y) { return textureGather(edges, vec2(x, y) * RCP, 1); }

// The part of the pixel [x, x + 1] between the edge (y = 0) and the line p1 -> p2: .x on this pixel's side (y < 0), .y on the other
vec2 area_line(vec2 p1, vec2 p2, float x) {
    float x1 = x, x2 = x + 1.0;
    if (!((x1 >= p1.x && x1 < p2.x) || (x2 > p1.x && x2 <= p2.x))) return vec2(0.0);
    vec2 d = p2 - p1;
    float y1 = p1.y + d.y * (x1 - p1.x) / d.x, y2 = p1.y + d.y * (x2 - p1.x) / d.x;
    if ((y1 >= 0.0) == (y2 >= 0.0) || abs(y1) < 1e-4 || abs(y2) < 1e-4) { float a = (y1 + y2) * 0.5; return a < 0.0 ? vec2(-a, 0.0) : vec2(0.0, a); }
    float xc = p1.x - p1.y * d.x / d.y, f = fract(xc);   // the line crosses the edge inside the pixel: a triangle each side
    float a1 = xc > p1.x ? y1 * f * 0.5 : 0.0, a2 = xc < p2.x ? y2 * (1.0 - f) * 0.5 : 0.0;
    return vec2(max(-a1, 0.0) + max(-a2, 0.0), max(a1, 0.0) + max(a2, 0.0));
}
vec2 smooth_u(float d, vec2 a1, vec2 a2) {
    float k = clamp(d / SMOOTH_MAX, 0.0, 1.0);
    return mix(sqrt(a1 * 2.0) * 0.5, a1, k) + mix(sqrt(a2 * 2.0) * 0.5, a2, k);
}
// An edge running from `left` pixels before this one to `right` after it; e1, e2 the crossing edges at its ends: 0 none, 1 on the other side, 3 on this
// pixel's side, 4 both. The line starts half a pixel toward a crossing edge's side and ends at the middle (L) or across at the other end (Z, U)
vec2 area_ortho(float left, float right, int e1, int e2) {
    float d = left + right + 1.0;
    float h1 = e1 == 3 ? -0.5 : 0.5, h2 = e2 == 3 ? -0.5 : 0.5;
    bool s1 = e1 == 1 || e1 == 3, s2 = e2 == 1 || e2 == 3;
    if (s1 && s2) {
        if (e1 == e2) return smooth_u(d, area_line(vec2(0.0, h1), vec2(d * 0.5, 0.0), left), area_line(vec2(d * 0.5, 0.0), vec2(d, h2), left));   // U
        return area_line(vec2(0.0, h1), vec2(d, h2), left);   // Z
    }
    if (s1 && e2 == 4) return area_line(vec2(0.0, h1), vec2(d, -h1), left);
    if (s2 && e1 == 4) return area_line(vec2(0.0, -h2), vec2(d, h2), left);
    if (s1 && e2 == 0) return left <= right ? area_line(vec2(0.0, h1), vec2(d * 0.5, 0.0), left) : vec2(0.0);   // L: the half near its corner
    if (s2 && e1 == 0) return left >= right ? area_line(vec2(d * 0.5, 0.0), vec2(d, h2), left) : vec2(0.0);
    return vec2(0.0);
}

void main() {
    SZ = textureSize(edges, 0); RCP = 1.0 / vec2(SZ);
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 e = texelFetch(edges, p, 0).rg;
    if (e.x + e.y == 0.0) discard;
    vec4 w = vec4(0.0);
    if (e.g > 0.0) {   // the top edge: along the row; crossings are left edges here (this side) or in the row above
        int l = 0, r = 0;
        while (l < MAXS) {   // q = p.x - l, the leftmost pixel found: the crossings at its left and at q-1's, the edge on q-1 and q-2
            int q = p.x - l; vec4 c = R4(q, p.y), t = G4(q - 1, p.y);
            if (c.y + c.z > 0.0 || t.y == 0.0) break; l++;
            if (c.x + c.w > 0.0 || t.x == 0.0) break; l++;
        }
        while (r < MAXS) {   // q = p.x + r, the rightmost: the crossings right of it and of q+1, the edge on q+1 and q+2
            int q = p.x + r; vec4 c = R4(q + 2, p.y), t = G4(q + 2, p.y);
            if (c.x + c.w > 0.0 || t.x == 0.0) break; r++;
            if (c.y + c.z > 0.0 || t.y == 0.0) break; r++;
        }
        l = min(l, MAXS); r = min(r, MAXS);
        ivec2 a = p - ivec2(l, 0), b = p + ivec2(r + 1, 0);
        w.rg = area_ortho(float(l), float(r), int(E(a).r) * 3 + int(E(a + ivec2(0, -1)).r), int(E(b).r) * 3 + int(E(b + ivec2(0, -1)).r));
    }
    if (e.r > 0.0) {   // the left edge: down the column; crossings are top edges here (this side) or in the column to the left
        int u = 0, d = 0;
        while (u < MAXS) {   // q = p.y - u, the topmost: the crossings above it and above q-1, the edge on q-1 and q-2
            int q = p.y - u; vec4 c = G4(p.x, q), t = R4(p.x, q - 1);
            if (c.x + c.y > 0.0 || t.y == 0.0) break; u++;
            if (c.z + c.w > 0.0 || t.z == 0.0) break; u++;
        }
        while (d < MAXS) {   // q = p.y + d, the lowest: the crossings below it and below q+1, the edge on q+1 and q+2
            int q = p.y + d; vec4 c = G4(p.x, q + 2), t = R4(p.x, q + 2);
            if (c.z + c.w > 0.0 || t.z == 0.0) break; d++;
            if (c.x + c.y > 0.0 || t.y == 0.0) break; d++;
        }
        u = min(u, MAXS); d = min(d, MAXS);
        ivec2 a = p - ivec2(0, u), b = p + ivec2(0, d + 1);
        w.ba = area_ortho(float(u), float(d), int(E(a).g) * 3 + int(E(a + ivec2(-1, 0)).g), int(E(b).g) * 3 + int(E(b + ivec2(-1, 0)).g));
    }
    o = w;
}
