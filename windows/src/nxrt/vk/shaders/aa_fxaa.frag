#version 450
// FXAA (vk_aa.c): our own implementation of the FXAA 3.11 "quality" algorithm (Timothy Lottes, NVIDIA, 2011), run over the game's finished 3D picture
// before its HUD. Luma contrast finds the pixels on an edge; the edge's direction is taken from the second differences around the pixel, its two ends are
// searched along it, and the pixel is moved toward its neighbour across the edge by how near it is to the nearer end, or by a sub-pixel blend for thin
// features. Pixels on no edge are left as they are. Luma is read from the copy as the target stores it (gamma-encoded for sRGB: as perceived); the move
// reads it in its own format (linear light for sRGB), as SMAA's blending does
layout(set = 0, binding = 0) uniform sampler2D src;   // a copy of the picture as stored, bilinear, clamped
layout(set = 0, binding = 1) uniform sampler2D lin;   // the same copy in the target's format
layout(location = 0) out vec4 o;
layout(push_constant) uniform PC { vec4 rcp; } pc;   // rcp.xy: one pixel in uv; rcp.zw: the picture's rectangle from 0,0 (reads stay inside it)

const float SUBPIX = 0.75, EDGE = 0.125, EDGE_MIN = 0.0625;   // FXAA's "high quality" thresholds
const int STEPS = 12;
const float STEP[STEPS] = float[](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

vec2 LO, HI;   // the rectangle's first and last texel centres
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float at(vec2 uv) { return lum(textureLod(src, clamp(uv, LO, HI), 0.0).rgb); }
#define AT(dx, dy) at(uv + vec2(dx, dy) * r)

void main() {
    vec2 r = pc.rcp.xy, uv = gl_FragCoord.xy * r;
    LO = 0.5 * r; HI = (pc.rcp.zw - 0.5) * r;
    float lM = at(uv);
    float lN = AT(0, -1), lS = AT(0, 1), lW = AT(-1, 0), lE = AT(1, 0);
    float hi = max(max(max(lN, lS), max(lW, lE)), lM), lo = min(min(min(lN, lS), min(lW, lE)), lM), range = hi - lo;
    if (range < max(EDGE_MIN, hi * EDGE)) discard;
    float lNW = AT(-1, -1), lNE = AT(1, -1), lSW = AT(-1, 1), lSE = AT(1, 1);
    // horizontal: a step between rows (second differences down the three columns), vertical: between columns
    float eH = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    float eV = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    bool horz = eH >= eV;
    // the sub-pixel blend: how much the pixel stands out from the average around it
    float sub = clamp(abs(((lN + lS + lW + lE) * 2.0 + (lNW + lNE + lSW + lSE)) / 12.0 - lM) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub; sub = sub * sub * SUBPIX;
    // the neighbour across the edge: the side with the larger step
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = l1 - lM, g2 = l2 - lM;
    bool side1 = abs(g1) >= abs(g2);
    float grad = max(abs(g1), abs(g2)) * 0.25;
    float stp = horz ? r.y : r.x; if (side1) stp = -stp;
    float lPair = (side1 ? l1 : l2) * 0.5 + lM * 0.5;   // the luma halfway across the edge
    // search along the edge, from the boundary half a pixel toward the neighbour, both ways, until the luma leaves the edge's pair
    vec2 pos = uv; if (horz) pos.y += stp * 0.5; else pos.x += stp * 0.5;
    vec2 dir = horz ? vec2(r.x, 0.0) : vec2(0.0, r.y);
    vec2 pN = pos - dir * STEP[0], pP = pos + dir * STEP[0];
    float eN = at(pN) - lPair, eP = at(pP) - lPair;
    bool doneN = abs(eN) >= grad, doneP = abs(eP) >= grad;
    for (int i = 1; i < STEPS && !(doneN && doneP); i++) {
        if (!doneN) { pN -= dir * STEP[i]; eN = at(pN) - lPair; doneN = abs(eN) >= grad; }
        if (!doneP) { pP += dir * STEP[i]; eP = at(pP) - lPair; doneP = abs(eP) >= grad; }
    }
    float dN = horz ? uv.x - pN.x : uv.y - pN.y, dP = horz ? pP.x - uv.x : pP.y - uv.y;
    bool nearN = dN < dP; float d = min(dN, dP);
    // the nearer end: a real end of this edge when the luma there went the other way from the middle's
    bool below = lM - lPair < 0.0;
    bool good = ((nearN ? eN : eP) < 0.0) != below;
    float off = good ? 0.5 - d / (dN + dP) : 0.0;
    off = max(off, sub);
    if (horz) uv.y += off * stp; else uv.x += off * stp;
    o = textureLod(lin, clamp(uv, LO, HI), 0.0);
}
