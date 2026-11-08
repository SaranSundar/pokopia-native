// Shaders for the replay (vk_shader.h). The binding kinds and their meaning are nxnative's (nvn_refl.m, nvn_mtl.m encode_args), as nxshader's reflection
// names them (dmsh_abi.h DMSH_KIND_*); the SPIR-V side is read from the module itself (the SPIR-V specification's physical layout).
#include "vk_shader.h"
#include "vk_json.h"
#include "vk_hmap.h"
#include "nxs_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vk_hmap g_sh = {17};   // key → vks_shader (a failed one stays, marked, so it is not retried)
static SRWLOCK g_shl = SRWLOCK_INIT;   // M6 phase B: the table, the variants and the c1 bank, shared by the replay and the pipeline prewarm (vk_pipe.c)
static VkDescriptorSetLayout g_empty;
static uint64_t g_loaded, g_failed;
static struct { VkBuffer buf; vk_alloc mem; VkDeviceSize used, size; } g_c1;   // the c1 bank (host-visible, written once per shader)
static vks_shader g_failed_mark;

int vksh_set_of(int stage) { return stage == VKS_CS ? 0 : stage == VKS_VS ? 0 : stage == VKS_FS ? 1 : stage == VKS_GS ? 2 : stage == VKS_TCS ? 3 : 4; }
VkShaderStageFlagBits vksh_vk_stage(int stage) {
    switch (stage) {
    case VKS_VS: return VK_SHADER_STAGE_VERTEX_BIT; case VKS_FS: return VK_SHADER_STAGE_FRAGMENT_BIT; case VKS_GS: return VK_SHADER_STAGE_GEOMETRY_BIT;
    case VKS_TCS: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; case VKS_TES: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; default: return VK_SHADER_STAGE_COMPUTE_BIT;
    }
}
VkDescriptorSetLayout vksh_empty_dsl(void) { return g_empty; }
void vksh_stats(uint64_t *loaded, uint64_t *failed) { *loaded = g_loaded; *failed = g_failed; }

// ---------- SPIR-V ----------
enum { OP_NAME = 5, OP_TYPE_INT = 21, OP_TYPE_FLOAT = 22, OP_TYPE_IMAGE = 25, OP_TYPE_SAMPLED_IMAGE = 27, OP_TYPE_ARRAY = 28, OP_TYPE_RUNTIME_ARRAY = 29,
       OP_TYPE_POINTER = 32, OP_VARIABLE = 59, OP_DECORATE = 71, DEC_BINDING = 33, DEC_SET = 34 };
static int spv_ok(const uint32_t *w, size_t n) { return n >= 5 && w[0] == 0x07230203u && w[3] > 0 && w[3] < (1u << 22); }

int vkspv_set_descriptor_set(uint32_t *w, size_t n, uint32_t set) {
    if (!spv_ok(w, n)) return -1;
    int changed = 0;
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) return -1;
        if (op == OP_DECORATE && wc >= 4 && w[i + 2] == DEC_SET) { w[i + 3] = set; changed++; }
        i += wc;
    }
    return changed;
}

// gl_PerVertex declares ClipDistance[8] and CullDistance[1] (Maxwell has 8 clip distances): more than a device with maxCombinedClipAndCullDistances 8 takes.
// The cull member goes when nothing reaches it (Pokopia never culls by distance): the block's last member, no access chain names it, the block is never
// loaded, stored or built whole. Returns the new word count (*w may be replaced), n when nothing changed, 0 when it cannot be done
size_t vkspv_trim_cull(uint32_t **pw, size_t n, uint32_t limit) {
    enum { OP_MEMBER_NAME = 6, OP_TYPE_STRUCT = 30, OP_CONSTANT = 43, OP_LOAD = 61, OP_STORE = 62, OP_COPY_MEMORY = 63, OP_ACCESS_CHAIN = 65, OP_IN_BOUNDS_ACCESS_CHAIN = 66,
           OP_MEMBER_DECORATE = 72, OP_COMPOSITE_CONSTRUCT = 80, OP_COPY_OBJECT = 83, DEC_BUILTIN = 11, BI_CLIP = 3, BI_CULL = 4 };
    uint32_t *w = *pw;
    if (!spv_ok(w, n)) return 0;
    uint32_t bound = w[3], st = 0, clip_m = ~0u, cull_m = ~0u;
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) return 0;
        if (op == OP_MEMBER_DECORATE && wc >= 5 && w[i + 3] == DEC_BUILTIN && (w[i + 4] == BI_CLIP || w[i + 4] == BI_CULL)) {
            if (st && st != w[i + 1]) return n;   // more than one block declares them: left alone
            st = w[i + 1]; if (w[i + 4] == BI_CLIP) clip_m = w[i + 2]; else cull_m = w[i + 2];
        }
        i += wc;
    }
    if (!st || clip_m == ~0u || cull_m == ~0u || st >= bound) return n;
    uint32_t *def = calloc(bound, sizeof *def), *cval = calloc(bound, sizeof *cval); uint8_t *isc = calloc(bound, 1);
    size_t si = 0; size_t out = 0;
    for (size_t i = 5; i < n; i += w[i] >> 16) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (op >= OP_TYPE_INT && op <= OP_TYPE_POINTER && wc >= 2 && w[i + 1] < bound) def[w[i + 1]] = (uint32_t)i;
        if (op == OP_TYPE_STRUCT && w[i + 1] == st) si = i;
        if (op == OP_CONSTANT && wc >= 4 && w[i + 2] < bound) { cval[w[i + 2]] = w[i + 3]; isc[w[i + 2]] = 1; }
    }
    uint32_t nmem = si ? (w[si] >> 16) - 2 : 0;
    // the members' array lengths
    uint32_t len[2] = {0, 0}, mem[2] = {clip_m, cull_m};
    for (int k = 0; k < 2 && si; k++) if (mem[k] < nmem) { uint32_t t = w[si + 2 + mem[k]];
        if (t < bound && def[t] && (w[def[t]] & 0xffff) == OP_TYPE_ARRAY && w[def[t] + 3] < bound && isc[w[def[t] + 3]]) len[k] = cval[w[def[t] + 3]]; }
    int ok = si && cull_m == nmem - 1 && len[0] && len[1] && len[0] + len[1] > limit && len[0] <= limit;
    // what refers to the block: pointers to it or to arrays of it, the variables of those
    uint8_t *blk = calloc(bound, 1), *bptr = calloc(bound, 1), *bvar = calloc(bound, 1);   // blk: the block or an array of it
    if (ok) {
        blk[st] = 1;
        for (size_t i = 5; i < n; i += w[i] >> 16) { uint32_t op = w[i] & 0xffff, wc = w[i] >> 16;
            if ((op == OP_TYPE_ARRAY || op == OP_TYPE_RUNTIME_ARRAY) && wc >= 3 && w[i + 2] < bound && blk[w[i + 2]] && w[i + 1] < bound) blk[w[i + 1]] = 2;
            if (op == OP_TYPE_POINTER && wc >= 4 && w[i + 3] < bound && blk[w[i + 3]] && w[i + 1] < bound) bptr[w[i + 1]] = blk[w[i + 3]];
            if (op == OP_VARIABLE && wc >= 4 && w[i + 1] < bound && bptr[w[i + 1]] && w[i + 2] < bound) bvar[w[i + 2]] = bptr[w[i + 1]]; }
        for (size_t i = 5; ok && i < n; i += w[i] >> 16) { uint32_t op = w[i] & 0xffff, wc = w[i] >> 16;
            if ((op == OP_ACCESS_CHAIN || op == OP_IN_BOUNDS_ACCESS_CHAIN) && wc >= 5 && w[i + 3] < bound && bvar[w[i + 3]]) {
                uint32_t k = bvar[w[i + 3]] == 2 ? 5 : 4;   // the member index follows the vertex index of an arrayed block
                if (k < wc && (w[i + k] >= bound || !isc[w[i + k]] || cval[w[i + k]] == cull_m)) ok = 0;
            }
            if ((op == OP_LOAD || op == OP_COMPOSITE_CONSTRUCT || op == OP_COPY_OBJECT) && wc >= 3 && w[i + 1] < bound && blk[w[i + 1]]) ok = 0;
            if ((op == OP_STORE || op == OP_COPY_MEMORY) && wc >= 3 && ((w[i + 1] < bound && bvar[w[i + 1]]) || (w[i + 2] < bound && bvar[w[i + 2]]))) ok = 0;
        }
    }
    if (ok) {   // the struct loses its last word; the member's name and decorations go
        uint32_t *nw = malloc(n * 4);
        memcpy(nw, w, 20); out = 5;
        for (size_t i = 5; i < n; i += w[i] >> 16) {
            uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
            if ((op == OP_MEMBER_DECORATE || op == OP_MEMBER_NAME) && wc >= 3 && w[i + 1] == st && w[i + 2] == cull_m) continue;
            if (i == si) { nw[out] = ((wc - 1) << 16) | op; memcpy(nw + out + 1, w + i + 1, (wc - 2) * 4); out += wc - 1; continue; }
            memcpy(nw + out, w + i, wc * 4); out += wc;
        }
        free(w); *pw = nw;
    }
    free(def); free(cval); free(isc); free(blk); free(bptr); free(bvar);
    return ok ? out : (si && len[0] + len[1] > limit ? 0 : n);
}

typedef struct { uint32_t *w; size_t n, cap; } wbuf;
static void wput(wbuf *b, const uint32_t *x, size_t k) { if (b->n + k > b->cap) { b->cap = (b->n + k) * 2; b->w = realloc(b->w, b->cap * 4); } memcpy(b->w + b->n, x, k * 4); b->n += k; }

// NVN's compiler aggregates an atomic add over a warp: ballot(true), the ballot's highest lane leads and adds its bit count for all of them. After some lanes have
// returned, that ballot never completes on NVIDIA's RTX 50 cards (#12, #21, #22: shader d2c50cca6e386abc, driver 617). Where the add's result is never read, a
// ballot of only the lane's own bit gives the same sum: every lane leads itself and adds 1. Only that shape is rewritten: ballot(true) whose value reaches a
// FindUMsb / FindSMsb and a bit count, the bit count an OpAtomicIAdd whose result goes nowhere (or only into a variable nothing reads), or is handed out from
// the leader by a shuffle (below). The new word count (*pw replaced) and *rewritten ballots; n when nothing changed
size_t vkspv_ballot_per_lane(uint32_t **pw, size_t n, int *rewritten) {
    enum { B_NAME = 5, B_EXT_INST = 12, B_TYPE_INT = 21, B_TYPE_VECTOR = 23, B_TYPE_POINTER = 32, B_CONSTANT_TRUE = 41, B_CONSTANT = 43, B_FUNCTION = 54,
           B_VARIABLE = 59, B_LOAD = 61, B_STORE = 62, B_ACCESS_CHAIN = 65, B_IN_BOUNDS_ACCESS_CHAIN = 66, B_DECORATE = 71, B_COMPOSITE_CONSTRUCT = 80,
           B_COMPOSITE_EXTRACT = 81, B_COPY_OBJECT = 83, B_BITCAST = 124, B_IADD = 128, B_IMUL = 132, B_SHR_L = 194, B_SHR_A = 195, B_SHL = 196, B_AND = 199,
           B_BIT_COUNT = 205, B_ATOMIC_IADD = 234, B_BALLOT = 339,
           B_DEC_BUILTIN = 11, B_BI_SUBGROUP_LANE = 41, B_BI_LT_MASK = 4420, B_SC_FUNCTION = 7, B_FIND_SMSB = 74, B_FIND_UMSB = 75, MAXB = 32 };
    uint32_t *w = *pw;
    *rewritten = 0;
    if (!spv_ok(w, n)) return n;
    uint32_t bound = w[3], tu = 0, c0 = 0, c1 = 0, lane_var = 0, lane_ptr = 0, lt_var = 0, nb = 0, bres[MAXB], bat[MAXB];
    size_t first_fn = 0;
    uint32_t *def = calloc(bound, sizeof *def);
    uint8_t *is_true = calloc(bound, 1), *nst = calloc(bound, 1);   // 1: the constant true, 2: a constant; the stores into each variable (2: more than one)
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) { free(def); free(is_true); free(nst); return n; }
        if (op == B_STORE && wc >= 3 && w[i + 1] < bound && nst[w[i + 1]] < 2) nst[w[i + 1]]++;
        if (op == B_DECORATE && wc == 4 && w[i + 2] == B_DEC_BUILTIN && w[i + 3] == B_BI_LT_MASK) lt_var = w[i + 1];
        if ((op == B_TYPE_INT || op == B_TYPE_VECTOR || op == B_TYPE_POINTER) && wc >= 2 && w[i + 1] < bound) def[w[i + 1]] = (uint32_t)i;
        if (op == B_VARIABLE && wc >= 4 && w[i + 2] < bound) def[w[i + 2]] = (uint32_t)i;
        if (op == B_TYPE_INT && wc == 4 && w[i + 2] == 32 && !w[i + 3] && !tu) tu = w[i + 1];
        if (op == B_CONSTANT_TRUE && wc == 3 && w[i + 2] < bound) is_true[w[i + 2]] = 1;
        if (op == B_CONSTANT && wc == 4 && tu && w[i + 1] == tu) { if (w[i + 3] == 0 && !c0) c0 = w[i + 2]; if (w[i + 3] == 1 && !c1) c1 = w[i + 2]; }
        if (op == B_CONSTANT && wc >= 4 && w[i + 2] < bound) is_true[w[i + 2]] |= 2;
        if (op == B_DECORATE && wc == 4 && w[i + 2] == B_DEC_BUILTIN && w[i + 3] == B_BI_SUBGROUP_LANE) lane_var = w[i + 1];
        if (op == B_FUNCTION && !first_fn) first_fn = i;
        if (op == B_BALLOT && wc == 5 && w[i + 4] < bound && (is_true[w[i + 4]] & 1) && nb < MAXB) { bres[nb] = w[i + 2]; bat[nb++] = (uint32_t)i; }
        i += wc;
    }
    // the lane's index must be an Input uint (as NVN's leader test reads it), the ballot a vector of 4 of them
    if (lane_var && lane_var < bound && def[lane_var]) { uint32_t pt = w[def[lane_var] + 1];
        if (pt < bound && def[pt] && (w[def[pt]] & 0xffff) == B_TYPE_POINTER && w[def[pt] + 3] == tu) lane_ptr = pt; }
    if (!nb || !tu || !lane_ptr || !first_fn) { free(def); free(is_true); free(nst); return n; }
    // the LtMask (Mk) through loads, bit casts and the variables stored once (the translator's temporaries; one stored more than once is a register)
    uint32_t *D = calloc(bound, 4), *C = calloc(bound, 4), *Mk = calloc(bound, 4), msb = 0, atom = 0, atom_used = 0;
    if (lt_var && lt_var < bound) Mk[lt_var] = 1;
    for (int pass = 0, changed = 1; changed && pass < 32; pass++) {
        changed = 0;
        for (size_t i = 5; i < n; i += w[i] >> 16) {
            uint32_t wc = w[i] >> 16, op = w[i] & 0xffff, dst = 0, src = 0;
            if (op == B_STORE && wc >= 3 && w[i + 1] < bound && w[i + 2] < bound && nst[w[i + 1]] == 1) dst = w[i + 1], src = w[i + 2];
            else if ((op == B_LOAD || op == B_ACCESS_CHAIN || op == B_IN_BOUNDS_ACCESS_CHAIN || op == B_COMPOSITE_EXTRACT || op == B_COPY_OBJECT || op == B_BITCAST)
                     && wc >= 4 && w[i + 2] < bound && w[i + 3] < bound) dst = w[i + 2], src = w[i + 3];
            if (dst && Mk[src] && !Mk[dst]) { Mk[dst] = 1; changed = 1; }
        }
    }
    // what each ballot's value reaches (bit k for ballot k): D its bits, C a bit count of them, through variables, access chains and bit casts (not the
    // rank's AND with the LtMask: followed below)
    for (uint32_t k = 0; k < nb; k++) D[bres[k]] |= 1u << k;
    for (int pass = 0, changed = 1; changed && pass < 32; pass++) {
        changed = 0; atom = 0;
        #define FLOW(dst, d, c) do { uint32_t d_ = (d), c_ = (c); if ((D[dst] | d_) != D[dst] || (C[dst] | c_) != C[dst]) { D[dst] |= d_; C[dst] |= c_; changed = 1; } } while (0)
        for (size_t i = 5; i < n; i += w[i] >> 16) {
            uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
            if (op == B_STORE && wc >= 3 && w[i + 1] < bound && w[i + 2] < bound) FLOW(w[i + 1], D[w[i + 2]], C[w[i + 2]]);
            else if ((op == B_LOAD || op == B_ACCESS_CHAIN || op == B_IN_BOUNDS_ACCESS_CHAIN || op == B_COMPOSITE_EXTRACT || op == B_COPY_OBJECT || op == B_BITCAST)
                     && wc >= 4 && w[i + 2] < bound && w[i + 3] < bound) FLOW(w[i + 2], D[w[i + 3]], C[w[i + 3]]);
            else if (op == B_AND && wc == 5 && w[i + 2] < bound && w[i + 3] < bound && w[i + 4] < bound && !Mk[w[i + 3]] && !Mk[w[i + 4]])
                FLOW(w[i + 2], D[w[i + 3]] | D[w[i + 4]], C[w[i + 3]] | C[w[i + 4]]);
            else if (op == B_BIT_COUNT && wc == 4 && w[i + 2] < bound && w[i + 3] < bound) FLOW(w[i + 2], 0, D[w[i + 3]] | C[w[i + 3]]);
            else if ((op == B_SHL || op == B_IMUL) && wc == 5 && w[i + 2] < bound && w[i + 3] < bound && w[i + 4] < bound &&
                     ((is_true[w[i + 4]] & 2) || (op == B_IMUL && (is_true[w[i + 3]] & 2))))   // a count of entries of a constant size (7f1a413b3fe36690 adds count << 2)
                FLOW(w[i + 2], 0, C[w[i + 3]] | C[w[i + 4]]);
            else if (op == B_EXT_INST && wc == 6 && (w[i + 4] == B_FIND_UMSB || w[i + 4] == B_FIND_SMSB) && w[i + 5] < bound) msb |= D[w[i + 5]];
            else if (op == B_ATOMIC_IADD && wc == 7 && w[i + 6] < bound && C[w[i + 6]]) {
                atom |= C[w[i + 6]];
                // its result read anywhere (beyond a store into a variable nothing else names) keeps those ballots as they are
                uint32_t r = w[i + 2], stored = 0;
                for (size_t j = 5; j < n && !(atom_used & C[w[i + 6]]) ; j += w[j] >> 16) {
                    if (j == i) continue;
                    uint32_t jc = w[j] >> 16, jo = w[j] & 0xffff;
                    for (uint32_t a = 1; a < jc; a++) if (w[j + a] == r) {
                        if (jo == B_STORE && a == 2 && !stored) stored = w[j + 1]; else atom_used |= C[w[i + 6]];
                        break;
                    }
                }
                if (stored) for (size_t j = 5; j < n && !(atom_used & C[w[i + 6]]); j += w[j] >> 16) {
                    uint32_t jc = w[j] >> 16, jo = w[j] & 0xffff;
                    if (jo == B_NAME || (jo == B_VARIABLE && w[j + 2] == stored) || (jo == B_STORE && w[j + 1] == stored)) continue;
                    for (uint32_t a = 1; a < jc; a++) if (w[j + a] == stored) { atom_used |= C[w[i + 6]]; break; }
                }
            }
        }
        #undef FLOW
    }
    // A counter whose result is read (D-1: shader 2cb5df5cb96b324e on an RTX 3050 Laptop, driver 621, the same hang): NVN hands each lane its slot as the
    // leader's result shuffled from the leader's lane plus the lane's rank, bitCount(LtMask & ballot). With a per-lane ballot every lane leads itself, adds 1
    // and gets its own slot: the shuffle reads the lane's own value and the rank is 0, so the slots are as many and as unique as before (only which lane
    // takes which within a warp changes). Taken only when the result reaches nothing but such a shuffle (a call of a function that shuffles, with the
    // leader among its arguments), the count nothing but the add and the result's variables, the leader nothing but the leader test and that call, the
    // ballot nothing but the leader, the count and the rank, the rank nothing but masks, constant scales and adds
    uint32_t cand = msb & atom & atom_used, rd_ok = 0;
    if (cand) {
        enum { R_FUNCTION_END = 56, R_FUNCTION_CALL = 57, R_IEQUAL = 170, R_SHUFFLE = 345 };
        uint32_t *L = calloc(bound, 4), *R = calloc(bound, 4), *RK = calloc(bound, 4);
        uint8_t *shuf_fn = calloc(bound, 1);
        for (size_t i = 5, fn = 0; i < n; i += w[i] >> 16) {   // the functions that shuffle
            uint32_t op = w[i] & 0xffff;
            if (op == B_FUNCTION && (w[i] >> 16) >= 3) fn = w[i + 2]; else if (op == R_FUNCTION_END) fn = 0;
            if (op == R_SHUFFLE && fn && fn < bound) shuf_fn[fn] = 1;
        }
        for (int pass = 0, changed = 1; changed && pass < 32; pass++) {   // the leader (L), the result (R), the rank (RK: not into a register, see below)
            changed = 0;
            #define FLOW2(a, dst, v) do { if ((a[dst] | (v)) != a[dst]) { a[dst] |= (v); changed = 1; } } while (0)
            for (size_t i = 5; i < n; i += w[i] >> 16) {
                uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
                if (op == B_STORE && wc >= 3 && w[i + 1] < bound && w[i + 2] < bound) {
                    FLOW2(L, w[i + 1], L[w[i + 2]]); FLOW2(R, w[i + 1], R[w[i + 2]]); if (nst[w[i + 1]] == 1) FLOW2(RK, w[i + 1], RK[w[i + 2]]);
                } else if ((op == B_LOAD || op == B_ACCESS_CHAIN || op == B_IN_BOUNDS_ACCESS_CHAIN || op == B_COMPOSITE_EXTRACT || op == B_COPY_OBJECT || op == B_BITCAST)
                           && wc >= 4 && w[i + 2] < bound && w[i + 3] < bound) {
                    FLOW2(L, w[i + 2], L[w[i + 3]]); FLOW2(R, w[i + 2], R[w[i + 3]]); FLOW2(RK, w[i + 2], RK[w[i + 3]]);
                } else if (op == B_BIT_COUNT && wc == 4 && w[i + 2] < bound && w[i + 3] < bound) FLOW2(RK, w[i + 2], RK[w[i + 3]]);   // the rank
                else if ((op == B_SHL || op == B_SHR_L || op == B_SHR_A || op == B_IMUL) && wc == 5 && w[i + 2] < bound && w[i + 3] < bound && w[i + 4] < bound)
                    FLOW2(RK, w[i + 2], RK[w[i + 3]] | RK[w[i + 4]]);
                else if (op == B_EXT_INST && wc == 6 && (w[i + 4] == B_FIND_UMSB || w[i + 4] == B_FIND_SMSB) && w[i + 5] < bound && w[i + 2] < bound) FLOW2(L, w[i + 2], D[w[i + 5]]);
                else if (op == B_ATOMIC_IADD && wc == 7 && w[i + 6] < bound && w[i + 2] < bound) FLOW2(R, w[i + 2], C[w[i + 6]]);
                else if (op == B_AND && wc == 5 && w[i + 2] < bound && w[i + 3] < bound && w[i + 4] < bound) {
                    if (Mk[w[i + 3]]) FLOW2(RK, w[i + 2], D[w[i + 4]]);
                    if (Mk[w[i + 4]]) FLOW2(RK, w[i + 2], D[w[i + 3]]);
                    FLOW2(RK, w[i + 2], RK[w[i + 3]] | RK[w[i + 4]]);   // the rank masked (7f1a413b3fe36690: & 65535)
                }
            }
            #undef FLOW2
        }
        // every use of what each candidate's ballot, count, leader and result reach
        uint32_t bad = 0;
        for (size_t i = 5; i < n; i += w[i] >> 16) {
            uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
            if (op == B_NAME || op == B_DECORATE || op == 6 /* OpMemberName */) continue;
            // the operands: after the result type and id, the extended set and its instruction, or the function called; from the start where there is no
            // result (a store or copy, an atomic store, an image write, a branch, a switch, a returned value)
            uint32_t first = op == B_EXT_INST ? 5 : op == R_FUNCTION_CALL ? 4 : 3;
            if (op == B_STORE || op == 63 || op == 99 || op == 228 || op == 250 || op == 251 || op == 254) first = 1;
            uint32_t call_lead = 0, call_ok = op == R_FUNCTION_CALL && wc >= 4 && w[i + 3] < bound && shuf_fn[w[i + 3]];
            if (call_ok) for (uint32_t a = 4; a < wc; a++) if (w[i + a] < bound) call_lead |= L[w[i + a]];
            for (uint32_t a = first; a < wc; a++) {
                uint32_t id = w[i + a]; if (id >= bound) continue;
                uint32_t d = D[id] & cand, c = C[id] & cand, l = L[id] & cand, r = R[id] & cand, k = RK[id] & cand;
                if (!(d | c | l | r | k)) continue;
                // the rank (0 in every lane once rewritten): only counted, masked or scaled by a constant, added (to the slot) or kept in a variable; one
                // stored more than once is a register, not followed (NVN's rank is dead after its add: 560a0262fe47818a reads its register again only
                // in a predicated add whose compare is masked by the same predicate)
                if (k) {
                    uint32_t p = w[i + 1];
                    int var = op == B_STORE && p < bound && def[p] && (w[def[p]] & 0xffff) == B_VARIABLE && w[def[p] + 3] == B_SC_FUNCTION;
                    int by_const = wc == 5 && w[i + 3] < bound && w[i + 4] < bound && ((is_true[w[i + 4]] & 2) || ((op == B_AND || op == B_IMUL) && (is_true[w[i + 3]] & 2)));
                    if (!(var || op == B_LOAD || op == B_BITCAST || op == B_COPY_OBJECT || op == B_BIT_COUNT || op == B_IADD ||
                          ((op == B_AND || op == B_IMUL || op == B_SHL || op == B_SHR_L || op == B_SHR_A) && by_const))) bad |= k;
                    if (!(d | c | l | r)) continue;
                }
                int pass_op = op == B_STORE || op == B_LOAD || op == B_COPY_OBJECT || op == B_BITCAST || op == B_COMPOSITE_EXTRACT;
                if (pass_op) continue;
                if (op == R_FUNCTION_CALL && call_ok) { bad |= (c | r | l) & ~call_lead; bad |= d; continue; }   // the result handed out from the leader
                if (op == B_ATOMIC_IADD && a == 6) { bad |= d | l | r; continue; }   // the count added
                if (op == B_EXT_INST && (w[i + 4] == B_FIND_UMSB || w[i + 4] == B_FIND_SMSB)) { bad |= c | l | r; continue; }   // the leader elected
                if (op == B_BIT_COUNT) { bad |= l | r; continue; }   // the count
                if (op == B_AND && wc == 5 && w[i + 3] < bound && w[i + 4] < bound && (Mk[w[i + 3]] || Mk[w[i + 4]])) { bad |= c | l | r; continue; }   // the rank
                if (op == R_IEQUAL) { bad |= d | c | r; continue; }   // the leader test
                if ((op == B_SHL || op == B_IMUL) && wc == 5 && w[i + 3] < bound && w[i + 4] < bound &&
                    ((is_true[w[i + 4]] & 2) || (op == B_IMUL && (is_true[w[i + 3]] & 2)))) { bad |= d | l | r; continue; }   // the count scaled by a constant
                bad |= d | c | l | r;
            }
        }
        rd_ok = cand & ~bad;
        free(L); free(R); free(RK); free(shuf_fn);
    }
    uint32_t ok = (msb & atom & ~atom_used) | rd_ok, vt = 0;
    for (uint32_t k = 0; k < nb; k++) if (ok >> k & 1) {   // the ballot's type: a vector of 4 uints
        vt = w[bat[k] + 1];
        if (!(vt < bound && def[vt] && (w[def[vt]] & 0xffff) == B_TYPE_VECTOR && w[def[vt] + 2] == tu && w[def[vt] + 3] == 4)) ok &= ~(1u << k);
    }
    free(D); free(C); free(Mk); free(is_true); free(nst);
    if (!ok) { free(def); return n; }
    // the rewrite: the constants 0 and 1 when missing (before the first function), each ballot → this lane's bit in .x
    wbuf o = {0};
    wput(&o, w, 5);
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16;
        if (i == first_fn) {
            if (!c0) { c0 = bound++; uint32_t c[4] = {4u << 16 | B_CONSTANT, tu, c0, 0}; wput(&o, c, 4); }
            if (!c1) { c1 = bound++; uint32_t c[4] = {4u << 16 | B_CONSTANT, tu, c1, 1}; wput(&o, c, 4); }
        }
        int k = -1;
        for (uint32_t b = 0; b < nb; b++) if (bat[b] == i && (ok >> b & 1)) k = (int)b;
        if (k < 0) { wput(&o, w + i, wc); i += wc; continue; }
        uint32_t lid = bound++, bit = bound++;
        uint32_t ld[4] = {4u << 16 | B_LOAD, tu, lid, lane_var}, sh[5] = {5u << 16 | B_SHL, tu, bit, c1, lid},
                 cc[7] = {7u << 16 | B_COMPOSITE_CONSTRUCT, w[i + 1], w[i + 2], bit, c0, c0, c0};
        wput(&o, ld, 4); wput(&o, sh, 5); wput(&o, cc, 7);
        (*rewritten)++;
        i += wc;
    }
    o.w[3] = bound;
    free(def); free(w);
    *pw = o.w;
    return o.n;
}

int vkspv_ballot_fix_wanted(uint32_t vendor, const char *env) { return env ? *env != '0' : vendor == 0x10de; }
// The loop guards: 1 the table-size guards (NVIDIA, where D-1's and D-11's hangs were), 2 constant caps of 16384 (other GPUs: AMD's Windows driver crashed making
// 634565ab's table-size guard, an edit that queries the table's size; a cap is a plain minimum), 0 none. NXRT_VK_LOOP_GUARD: 0 none, cap the caps, else the table guards
int vkspv_loop_guard_wanted(uint32_t vendor, const char *env) { if (env) return *env == '0' ? 0 : !strcmp(env, "cap") ? 2 : 1; return vendor == 0x10de ? 1 : 2; }

// A loop over the items of a table whose count is a constant buffer's first word (shader 634565ab77c61463: D-1's hang on 1.3.0, a count that was not the
// game's runs the loop for seconds): in integer comparisons, count_var's [0][0].x read as an integer becomes min(count, the items tex_var holds). The table is
// NVN's texel buffer as the translator lays it out (rows of 16384 texels; stride texels an item). Without tex_var the count is capped at 16384 instead (no
// size query; stride unused). The new word count (*pw replaced) and *limited comparisons
size_t vkspv_limit_count(uint32_t **pw, size_t n, const char *count_var, uint32_t word, const char *tex_var, uint32_t stride, int *limited) {
    enum { L_NAME = 5, L_EXT_IMPORT = 11, L_EXT_INST = 12, L_CAPABILITY = 17, CAP_IMAGE_QUERY = 50, L_TYPE_INT = 21, L_TYPE_VECTOR = 23, L_TYPE_SAMPLED_IMAGE = 27, L_TYPE_POINTER = 32, L_CONSTANT = 43,
           L_FUNCTION = 54, L_VARIABLE = 59, L_LOAD = 61, L_ACCESS_CHAIN = 65, L_COMPOSITE_EXTRACT = 81, L_IMAGE = 100, L_QUERY_SIZE_LOD = 103, L_BITCAST = 124,
           L_IADD = 128, L_ISUB = 130, L_IMUL = 132, L_SDIV = 135, L_CMP_FIRST = 170, L_CMP_LAST = 181, GLSL_UMIN = 38, GLSL_SMIN = 39 };
    uint32_t *w = *pw; *limited = 0;
    if (!spv_ok(w, n) || !stride) return n;
    uint32_t bound = w[3], cv = 0, tv = 0, glsl = 0, ti = 0, tu = 0, v2 = 0, c[4] = {0}, cval[4] = {0, 1, 16384, stride - 1}, cst = 0, query = 0;
    const uint32_t at[3] = {0, word / 4, word % 4};   // the count's access chain into the block: member 0 (its vec4 array), the vec4, the component
    size_t first_fn = 0;
    uint32_t *def = calloc(bound, sizeof *def), *kval = calloc(bound, sizeof *kval);
    uint8_t *isk = calloc(bound, 1), *i32 = calloc(bound, 1), *ac = calloc(bound, 1), *ld = calloc(bound, 1), *bc = calloc(bound, 1);   // bc: 1 a signed count, 2 an unsigned one
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) goto none;
        if (op == L_NAME && wc >= 3 && w[i + 1] < bound) { const char *s = (const char *)(w + i + 2); if (!strcmp(s, count_var)) cv = w[i + 1]; else if (tex_var && !strcmp(s, tex_var)) tv = w[i + 1]; }
        if (op == L_EXT_IMPORT && wc >= 3 && !strcmp((const char *)(w + i + 2), "GLSL.std.450")) glsl = w[i + 1];
        if (op == L_CAPABILITY && wc == 2 && w[i + 1] == CAP_IMAGE_QUERY) query = 1;
        if ((op == L_TYPE_POINTER || op == L_TYPE_SAMPLED_IMAGE) && wc >= 3 && w[i + 1] < bound) def[w[i + 1]] = (uint32_t)i;
        if (op == L_VARIABLE && wc >= 4 && w[i + 2] < bound) def[w[i + 2]] = (uint32_t)i;
        if (op == L_TYPE_INT && wc == 4 && w[i + 2] == 32 && w[i + 1] < bound) {
            i32[w[i + 1]] = 1;
            if (w[i + 3] == 1 && !ti) ti = w[i + 1];
            if (w[i + 3] == 0 && !tu) tu = w[i + 1];
        }
        if (op == L_TYPE_VECTOR && wc == 4 && ti && w[i + 2] == ti && w[i + 3] == 2 && !v2) v2 = w[i + 1];
        if (op == L_CONSTANT && wc == 4 && w[i + 2] < bound && w[i + 1] < bound && i32[w[i + 1]]) {
            isk[w[i + 2]] = 1; kval[w[i + 2]] = w[i + 3];
            for (int k = 0; k < 4; k++) if (ti && w[i + 1] == ti && w[i + 3] == cval[k] && !c[k]) c[k] = w[i + 2];
            if (ti && w[i + 1] == ti && w[i + 3] == stride && !cst) cst = w[i + 2];
        }
        if (op == L_FUNCTION && !first_fn) first_fn = i;
        if (op == L_ACCESS_CHAIN && wc == 7 && w[i + 3] == cv && cv && w[i + 2] < bound) {
            int m = 1; for (int k = 0; k < 3; k++) m &= w[i + 4 + k] < bound && isk[w[i + 4 + k]] && kval[w[i + 4 + k]] == at[k];
            if (m) ac[w[i + 2]] = 1;
        }
        if (op == L_LOAD && wc >= 4 && w[i + 2] < bound && w[i + 3] < bound && ac[w[i + 3]]) ld[w[i + 2]] = 1;
        if (op == L_BITCAST && wc == 4 && (w[i + 1] == ti || w[i + 1] == tu) && w[i + 1] && w[i + 2] < bound && w[i + 3] < bound && ld[w[i + 3]]) bc[w[i + 2]] = w[i + 1] == ti ? 1 : 2;
        i += wc;
    }
    // the table's sampled image and image types (a UniformConstant pointer to an OpTypeSampledImage)
    uint32_t sit = 0, it = 0;
    if (tv && def[tv]) { uint32_t pt = w[def[tv] + 1]; if (pt < bound && def[pt] && (w[def[pt]] & 0xffff) == L_TYPE_POINTER) sit = w[def[pt] + 3]; }
    if (sit && sit < bound && def[sit] && (w[def[sit]] & 0xffff) == L_TYPE_SAMPLED_IMAGE) it = w[def[sit] + 2];
    if (!glsl || !ti || (tex_var && !it) || !first_fn) goto none;
    int any = 0; for (uint32_t k = 0; k < bound; k++) any |= bc[k];
    if (!any) goto none;
    wbuf o = {0};
    wput(&o, w, 5);
    if (!query && tex_var) { uint32_t cap[2] = {2u << 16 | L_CAPABILITY, CAP_IMAGE_QUERY}; wput(&o, cap, 2); }   // the table's size is queried (the translator's modules declare it)
    uint32_t *nid = calloc(bound, sizeof *nid);   // each count read's limited value
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (i == first_fn) {   // what the limit needs and the module lacks: ivec2, the constants
            if (!v2) { v2 = bound++; uint32_t t[4] = {4u << 16 | L_TYPE_VECTOR, v2, ti, 2}; wput(&o, t, 4); }
            for (int k = 0; k < 4; k++) if (!c[k]) { c[k] = bound++; uint32_t t[4] = {4u << 16 | L_CONSTANT, ti, c[k], cval[k]}; wput(&o, t, 4); }
            if (!cst) { cst = bound++; uint32_t t[4] = {4u << 16 | L_CONSTANT, ti, cst, stride}; wput(&o, t, 4); }
        }
        if (op >= L_CMP_FIRST && op <= L_CMP_LAST && wc == 5 && ((w[i + 3] < bound && nid[w[i + 3]]) || (w[i + 4] < bound && nid[w[i + 4]]))) {
            uint32_t x[5]; memcpy(x, w + i, sizeof x);
            for (int a = 3; a < 5; a++) if (x[a] < bound && nid[x[a]]) x[a] = nid[x[a]];
            wput(&o, x, 5); (*limited)++; i += wc; continue;
        }
        wput(&o, w + i, wc);
        if (op == L_BITCAST && wc == 4 && w[i + 2] < bound && bc[w[i + 2]]) {   // the count read: min(count, ((rows - 1) * 16384 + width + stride - 1) / stride), or min(count, 16384)
            uint32_t cap = c[2], lim = bound++;
            if (tex_var) {
                uint32_t tex = bound++, img = bound++, sz = bound++, wd = bound++, ht = bound++, hm = bound++, rows = bound++, tot = bound++, tr = bound++; cap = bound++;
                uint32_t s[] = {4u << 16 | L_LOAD, sit, tex, tv,   4u << 16 | L_IMAGE, it, img, tex,   5u << 16 | L_QUERY_SIZE_LOD, v2, sz, img, c[0],
                                5u << 16 | L_COMPOSITE_EXTRACT, ti, wd, sz, 0,   5u << 16 | L_COMPOSITE_EXTRACT, ti, ht, sz, 1,   5u << 16 | L_ISUB, ti, hm, ht, c[1],
                                5u << 16 | L_IMUL, ti, rows, hm, c[2],   5u << 16 | L_IADD, ti, tot, rows, wd,   5u << 16 | L_IADD, ti, tr, tot, c[3],
                                5u << 16 | L_SDIV, ti, cap, tr, cst};
                wput(&o, s, sizeof s / 4);
            }
            if (bc[w[i + 2]] == 1) { uint32_t m[] = {7u << 16 | L_EXT_INST, ti, lim, glsl, GLSL_SMIN, w[i + 2], cap}; wput(&o, m, 7); }
            else {   // an unsigned count (compared as one): the cap as unsigned, the unsigned minimum (a garbage count of 0xffffffff is no negative number)
                uint32_t cu = bound++, m[] = {4u << 16 | L_BITCAST, tu, cu, cap,   7u << 16 | L_EXT_INST, tu, lim, glsl, GLSL_UMIN, w[i + 2], cu};
                wput(&o, m, 11);
            }
            nid[w[i + 2]] = lim;
        }
        i += wc;
    }
    free(nid);
    if (!*limited) { free(o.w); goto none; }
    o.w[3] = bound;
    free(def); free(kval); free(isk); free(i32); free(ac); free(ld); free(bc); free(w);
    *pw = o.w;
    return o.n;
none:
    free(def); free(kval); free(isk); free(i32); free(ac); free(ld); free(bc);
    return n;
}

size_t vkspv_cap_bound(uint32_t **pw, size_t n, uint32_t var, uint32_t cap, int *capped) {
    enum { C_EXT_IMPORT = 11, C_EXT_INST = 12, C_TYPE_INT = 21, C_TYPE_POINTER = 32, C_CONSTANT = 43, C_FUNCTION = 54, C_VARIABLE = 59, C_LOAD = 61, C_STORE = 62,
           C_UGE = 174, GLSL_UMIN = 38, SC_FUNCTION = 7 };
    uint32_t *w = *pw; *capped = 0;
    if (!spv_ok(w, n) || !var || var >= w[3]) return n;
    uint32_t bound = w[3], glsl = 0, tu = 0, kcap = 0, vt = 0, nstore = 0, uge = 0;
    size_t first_fn = 0, store_at = 0;
    uint32_t *ptr_sc = calloc(bound, sizeof *ptr_sc), *ptr_to = calloc(bound, sizeof *ptr_to); uint8_t *ld = calloc(bound, 1);
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) { nstore = 0; break; }
        if (op == C_EXT_IMPORT && wc >= 3 && !strcmp((const char *)(w + i + 2), "GLSL.std.450")) glsl = w[i + 1];
        if (op == C_TYPE_INT && wc == 4 && w[i + 2] == 32 && w[i + 3] == 0 && !tu) tu = w[i + 1];
        if (op == C_CONSTANT && wc == 4 && tu && w[i + 1] == tu && w[i + 3] == cap && !kcap) kcap = w[i + 2];
        if (op == C_TYPE_POINTER && wc == 4 && w[i + 1] < bound) { ptr_sc[w[i + 1]] = w[i + 2]; ptr_to[w[i + 1]] = w[i + 3]; }
        if (op == C_VARIABLE && wc >= 4 && w[i + 2] == var) vt = w[i + 1];
        if (op == C_FUNCTION && !first_fn) first_fn = i;
        if (op == C_STORE && wc >= 3 && w[i + 1] == var) { nstore++; store_at = i; }
        if (op == C_LOAD && wc >= 4 && w[i + 3] == var && w[i + 2] < bound) ld[w[i + 2]] = 1;
        if (op == C_UGE && wc == 5 && w[i + 4] < bound && ld[w[i + 4]]) uge++;
        i += wc;
    }
    int ok = glsl && tu && vt && vt < bound && ptr_sc[vt] == SC_FUNCTION && ptr_to[vt] == tu && nstore == 1 && uge && first_fn && store_at > first_fn;
    free(ptr_sc); free(ptr_to); free(ld);
    if (!ok) return n;
    wbuf o = {0};
    wput(&o, w, 5);
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16;
        if (i == first_fn && !kcap) { kcap = bound++; uint32_t k[4] = {4u << 16 | C_CONSTANT, tu, kcap, cap}; wput(&o, k, 4); }
        if (i == store_at && wc <= 8) {   // the bound stored: min(bound, cap)
            uint32_t lim = bound++, m[7] = {7u << 16 | C_EXT_INST, tu, lim, glsl, GLSL_UMIN, w[i + 2], kcap}, st[8];
            memcpy(st, w + i, wc * 4); st[2] = lim;
            wput(&o, m, 7); wput(&o, st, wc);
            *capped = 1; i += wc; continue;
        }
        wput(&o, w + i, wc);
        i += wc;
    }
    if (!*capped) { free(o.w); return n; }
    o.w[3] = bound;
    free(w);
    *pw = o.w;
    return o.n;
}

int vkspv_reads_member(const uint32_t *w, size_t n, uint32_t binding, uint32_t member) {
    enum { OP_CONSTANT = 43, OP_ACCESS_CHAIN = 65, OP_IN_BOUNDS_ACCESS_CHAIN = 66 };
    if (!spv_ok(w, n)) return 0;
    uint32_t bound = w[3], var = 0;
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) return 0;
        if (op == OP_DECORATE && wc >= 4 && w[i + 2] == DEC_BINDING && w[i + 3] == binding) { var = w[i + 1]; break; }
        i += wc;
    }
    if (!var || var >= bound) return 0;
    uint8_t *is_m = calloc(bound, 1);   // the ids of 32-bit constants equal to member
    int found = 0;
    for (size_t i = 5; i < n && !found; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) break;
        if (op == OP_CONSTANT && wc == 4 && w[i + 2] < bound && w[i + 3] == member) is_m[w[i + 2]] = 1;
        else if ((op == OP_ACCESS_CHAIN || op == OP_IN_BOUNDS_ACCESS_CHAIN) && wc >= 5 && w[i + 3] == var && w[i + 4] < bound && is_m[w[i + 4]]) found = 1;
        i += wc;
    }
    free(is_m);
    return found;
}

int vkspv_image_of(const uint32_t *w, size_t n, uint32_t binding, vkspv_img *o) {
    memset(o, 0, sizeof *o); o->binding = binding;
    if (!spv_ok(w, n)) return -1;
    uint32_t bound = w[3], var = 0;
    for (size_t i = 5; i < n; ) {   // the variable decorated with this binding
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) return -1;
        if (op == OP_DECORATE && wc >= 4 && w[i + 2] == DEC_BINDING && w[i + 3] == binding) { var = w[i + 1]; break; }
        i += wc;
    }
    if (!var || var >= bound) return -1;
    // types by id: what each defines (opcode, operands' offset)
    uint32_t *def = calloc(bound, sizeof *def);   // index of the defining instruction
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) break;
        uint32_t res = 0;
        if (op >= OP_TYPE_INT && op <= OP_TYPE_POINTER && wc >= 2) res = w[i + 1];
        else if (op == OP_VARIABLE && wc >= 3) res = w[i + 2];
        if (res && res < bound) def[res] = (uint32_t)i;
        i += wc;
    }
    int rc = -1;
    uint32_t vi = def[var];
    if (vi && (w[vi] & 0xffff) == OP_VARIABLE) {
        uint32_t t = w[vi + 1];
        for (int hop = 0; hop < 8 && t < bound && def[t]; hop++) {
            uint32_t d = def[t], op = w[d] & 0xffff;
            if (op == OP_TYPE_POINTER) t = w[d + 3];
            else if (op == OP_TYPE_ARRAY || op == OP_TYPE_RUNTIME_ARRAY) t = w[d + 2];
            else if (op == OP_TYPE_SAMPLED_IMAGE) t = w[d + 2];
            else if (op == OP_TYPE_IMAGE) {
                o->is_image = 1; o->dim = (uint8_t)w[d + 3]; o->depth = w[d + 4] == 1; o->arrayed = (uint8_t)w[d + 5]; o->ms = (uint8_t)w[d + 6];
                uint32_t st = w[d + 2];
                if (st < bound && def[st]) { uint32_t sd = def[st]; o->ct = (w[sd] & 0xffff) == OP_TYPE_INT ? (w[sd + 3] ? VKS_CT_INT : VKS_CT_UINT) : VKS_CT_FLOAT; }
                break;
            } else break;
        }
        o->found = 1; rc = 0;
    }
    free(def);
    return rc;
}

static VkImageViewType view_type(uint8_t dim, uint8_t arrayed) {
    switch (dim) {
    case 0: return arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
    case 2: return VK_IMAGE_VIEW_TYPE_3D;
    case 3: return arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
    default: return arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;   // 2D, rect
    }
}

// ---------- Vertex input retyping (vksh_vin_variant) ----------
enum { OP_TYPE_VECTOR = 23, OP_ACCESS_CHAIN = 65, OP_IN_BOUNDS_ACCESS_CHAIN = 66, OP_LOAD = 61, OP_BITCAST = 124, DEC_LOCATION = 30, SC_INPUT = 1 };
uint32_t *vkspv_retype_inputs(const uint32_t *w, size_t n, uint32_t mask, const uint8_t ct[32], size_t *out_n) {
    if (!spv_ok(w, n) || !mask) return NULL;
    uint32_t bound = w[3];
    // what the module defines: scalar types by class, vectors, input pointers, the input variables by location
    uint32_t *def = calloc(bound, sizeof *def), loc_of_var[32] = {0}, scal[3] = {0}, first_var = 0;
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) { free(def); return NULL; }
        if (op == OP_DECORATE && wc >= 4 && w[i + 2] == DEC_LOCATION && w[i + 3] < 32) loc_of_var[w[i + 3]] = w[i + 1];   // the last one wins; inputs are checked below
        uint32_t res = 0;
        if (op >= OP_TYPE_INT && op <= OP_TYPE_POINTER && wc >= 2) res = w[i + 1];
        else if (op == OP_VARIABLE && wc >= 4) { res = w[i + 2]; if (!first_var && w[i + 3] != 7) first_var = (uint32_t)i; }
        if (res && res < bound) def[res] = (uint32_t)i;
        if (op == OP_TYPE_INT && wc == 4 && w[i + 2] == 32) scal[w[i + 3] ? VKS_CT_INT : VKS_CT_UINT] = w[i + 1];
        if (op == OP_TYPE_FLOAT && wc == 3 && w[i + 2] == 32) scal[VKS_CT_FLOAT] = w[i + 1];
        i += wc;
    }
    // the input variables to retype (their Location decorations may sit on outputs too: only Input variables count)
    uint32_t var_of[32] = {0}, var_ct[32];
    for (int l = 0; l < 32; l++) {
        if (!(mask >> l & 1)) continue;
        uint32_t v = 0;
        for (size_t i = 5; i < n; i += w[i] >> 16) if ((w[i] & 0xffff) == OP_DECORATE && (w[i] >> 16) >= 4 && w[i + 2] == DEC_LOCATION && w[i + 3] == (uint32_t)l) {
            uint32_t c = w[i + 1]; if (c < bound && def[c] && (w[def[c]] & 0xffff) == OP_VARIABLE && w[def[c] + 3] == SC_INPUT) v = c; }
        if (v) { var_of[l] = v; var_ct[l] = ct[l]; }
    }
    (void)loc_of_var;
    // the new types go right before the first variable retyped; the existing types they name must be defined before that
    first_var = 0;
    for (int l = 0; l < 32; l++) if (var_of[l] && (!first_var || def[var_of[l]] < first_var)) first_var = def[var_of[l]];
    if (!first_var) { free(def); return NULL; }
    for (int k = 0; k < 3; k++) if (scal[k] && def[scal[k]] > first_var) { free(def); return NULL; }
    // new types, placed before the first global variable: the scalars of each class, vectors of them, input pointers to both
    wbuf types = {0}; uint32_t vec_of[3][5] = {{0}}, pv[3][5] = {{0}}, ps[3] = {0};
    for (size_t i = 5; i < n; i += w[i] >> 16) {   // existing vectors and input pointers
        uint32_t op = w[i] & 0xffff;
        for (int k = 0; k < 3; k++) {
            if (op == OP_TYPE_VECTOR && w[i + 2] == scal[k] && w[i + 3] <= 4) vec_of[k][w[i + 3]] = w[i + 1];
            if (op == OP_TYPE_POINTER && w[i + 2] == SC_INPUT && w[i + 3] == scal[k] && scal[k]) ps[k] = w[i + 1];
        }
    }
    for (size_t i = 5; i < n; i += w[i] >> 16) if ((w[i] & 0xffff) == OP_TYPE_POINTER && w[i + 2] == SC_INPUT)
        for (int k = 0; k < 3; k++) for (int c = 2; c <= 4; c++) if (vec_of[k][c] && w[i + 3] == vec_of[k][c]) pv[k][c] = w[i + 1];
    #define NEWT(...) do { uint32_t t_[] = {__VA_ARGS__}; wput(&types, t_, sizeof t_ / 4); } while (0)
    for (int l = 0; l < 32; l++) {
        if (!var_of[l]) continue;
        int k = var_ct[l];
        if (!scal[k]) { scal[k] = bound++; if (k == VKS_CT_FLOAT) NEWT(3u << 16 | OP_TYPE_FLOAT, scal[k], 32); else NEWT(4u << 16 | OP_TYPE_INT, scal[k], 32, k == VKS_CT_INT); }
        if (!ps[k]) { ps[k] = bound++; NEWT(4u << 16 | OP_TYPE_POINTER, ps[k], SC_INPUT, scal[k]); }
        uint32_t pt = w[def[var_of[l]] + 1], pointee = w[def[pt] + 3], pop = w[def[pointee]] & 0xffff, nc = pop == OP_TYPE_VECTOR ? w[def[pointee] + 3] : 1;
        if (nc > 1 && nc <= 4) {
            if (!vec_of[k][nc]) { vec_of[k][nc] = bound++; NEWT(4u << 16 | OP_TYPE_VECTOR, vec_of[k][nc], scal[k], nc); }
            if (!pv[k][nc]) { pv[k][nc] = bound++; NEWT(4u << 16 | OP_TYPE_POINTER, pv[k][nc], SC_INPUT, vec_of[k][nc]); }
        }
    }
    #undef NEWT
    // the rewrite: variables take the new pointer type; access chains into them the scalar pointer; loads through either load the new type and bit-cast
    uint32_t *repl = calloc(bound + 1, sizeof *repl);   // pointer id → class + 1 of what it now points to
    wbuf o = {0};
    wput(&o, w, 5);
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (i == first_var && types.n) wput(&o, types.w, types.n);
        uint32_t x[64]; memcpy(x, w + i, (wc < 64 ? wc : 64) * 4);
        if (wc > 64) { wput(&o, w + i, wc); i += wc; continue; }
        if (op == OP_VARIABLE) {
            for (int l = 0; l < 32; l++) if (var_of[l] && x[2] == var_of[l]) {
                int k = var_ct[l]; uint32_t pointee = w[def[x[1]] + 3], nc = (w[def[pointee]] & 0xffff) == OP_TYPE_VECTOR ? w[def[pointee] + 3] : 1;
                x[1] = nc > 1 ? pv[k][nc] : ps[k]; repl[x[2]] = (uint32_t)k + 1;
            }
            wput(&o, x, wc);
        } else if ((op == OP_ACCESS_CHAIN || op == OP_IN_BOUNDS_ACCESS_CHAIN) && x[3] < bound && repl[x[3]]) {
            int k = (int)repl[x[3]] - 1; x[1] = ps[k]; repl[x[2]] = (uint32_t)k + 1;   // one index into a vector: the scalar
            wput(&o, x, wc);
        } else if (op == OP_LOAD && x[3] < bound && repl[x[3]]) {
            int k = (int)repl[x[3]] - 1; uint32_t want = x[1], old = x[2], nid = bound++;
            uint32_t wop = w[def[want]] & 0xffff, nc = wop == OP_TYPE_VECTOR ? w[def[want] + 3] : 1;
            x[1] = nc > 1 ? vec_of[k][nc] : scal[k]; x[2] = nid;
            wput(&o, x, wc);
            uint32_t bc[4] = {4u << 16 | OP_BITCAST, want, old, nid}; wput(&o, bc, 4);
        } else wput(&o, x, wc);
        i += wc;
    }
    o.w[3] = bound;
    free(def); free(repl); free(types.w);
    *out_n = o.n;
    return o.w;
}
static const vks_shader *vin_variant_l(const vks_shader *vs_, uint32_t mask, const uint8_t ct[32]) {
    vks_shader *vs = (vks_shader *)vs_;
    if (!mask || !vs->words) return vs;
    uint64_t key = mask; for (int l = 0; l < 32; l++) if (mask >> l & 1) key = key * 31 + ct[l] + 1;
    for (int i = 0; i < vs->nvar; i++) if (vs->var[i].key == key) return vs->var[i].s;
    if (vs->nvar == 8) { static int n; if (n++ < 4) VKRED("shader %s: more than 8 vertex input variants", vs->key); return vs; }
    size_t nn; uint32_t *nw = vkspv_retype_inputs(vs->words, vs->nwords, mask, ct, &nn);
    VkShaderModule mod = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo mci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, nn * 4, nw};
    if (!nw || vkCreateShaderModule(g_vk.dev, &mci, NULL, &mod) != VK_SUCCESS) { free(nw); static int n; if (n++ < 4) VKRED("shader %s: its inputs %#x could not be retyped", vs->key, mask); return vs; }
    vks_shader *s = malloc(sizeof *s); *s = *vs;
    s->mod = mod; s->words = nw; s->nwords = nn; s->base = vs; s->nvar = 0;
    for (int i = 0; i < s->nin; i++) if (mask >> s->in[i].loc & 1) s->in[i].ct = ct[s->in[i].loc];
    s->vin_mask = mask; memcpy(s->vin_ct, ct, sizeof s->vin_ct);
    vs->var[vs->nvar].key = key; vs->var[vs->nvar].s = s; vs->nvar++;
    static int nl; if (nl++ < 16) VKLOG("vertex shader %s: inputs %#x declared as their formats' types (variant %d)", vs->key, mask, vs->nvar);
    return s;
}
const vks_shader *vksh_vin_variant(const vks_shader *vs, uint32_t mask, const uint8_t ct[32]) {
    if (!mask) return vs;
    AcquireSRWLockExclusive(&g_shl); const vks_shader *r = vin_variant_l(vs, mask, ct); ReleaseSRWLockExclusive(&g_shl);
    return r;
}

// ---------- Fragment variants (M6 phase B): dual-source blending, the alpha test ----------
enum { OP_ENTRY_POINT = 15, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_STRUCT = 30, OP_CONSTANT = 43, OP_FUNCTION = 54, OP_FUNCTION_END = 56, OP_MEMBER_DECORATE = 72,
       OP_ISUB = 130, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167, OP_INOT_EQUAL = 171, OP_FORD_EQUAL = 180, OP_FORD_LESS = 184, OP_FORD_GREATER = 186, OP_BITWISE_AND = 199,
       OP_SELECTION_MERGE = 247, OP_LABEL = 248, OP_BRANCH_COND = 250, OP_KILL = 252, OP_RETURN = 253,
       DEC_BLOCK = 2, DEC_BUILTIN = 11, DEC_INDEX = 32, DEC_OFFSET = 35, SC_OUTPUT = 3, SC_PUSH = 9 };
// The non-builtin Output variables by Location (-1 when absent; -2 when a location holds more than one; a second blend source, Index 1, is not counted); returns how many
static int spv_outputs(const uint32_t *w, size_t n, int32_t var_at[8]) {
    uint32_t bound = w[3]; int cnt = 0;
    uint8_t *out = calloc(bound, 1), *bi = calloc(bound, 1);
    for (size_t i = 5; i < n; i += w[i] >> 16) {
        uint32_t op = w[i] & 0xffff, wc = w[i] >> 16;
        if (op == OP_VARIABLE && wc >= 4 && w[i + 3] == SC_OUTPUT && w[i + 2] < bound) out[w[i + 2]] = 1;
        if (op == OP_DECORATE && wc >= 3 && (w[i + 2] == DEC_BUILTIN || (w[i + 2] == DEC_INDEX && wc >= 4 && w[i + 3] == 1)) && w[i + 1] < bound) bi[w[i + 1]] = 1;
    }
    for (int l = 0; l < 8; l++) var_at[l] = -1;
    for (size_t i = 5; i < n; i += w[i] >> 16)
        if ((w[i] & 0xffff) == OP_DECORATE && (w[i] >> 16) >= 4 && w[i + 2] == DEC_LOCATION && w[i + 1] < bound && out[w[i + 1]] && !bi[w[i + 1]] && w[i + 3] < 8) {
            uint32_t l = w[i + 3]; var_at[l] = var_at[l] == -1 ? (int32_t)w[i + 1] : -2; cnt++;
        }
    free(out); free(bi);
    return cnt;
}
// Dual-source blending (nxshader's DualSource pass, nxnative's fs_dual): output 1 becomes Location 0 Index 1, the second source of target 0. Only for a shader writing
// exactly outputs 0 and 1. New words or NULL
uint32_t *vkspv_dual_source(const uint32_t *w, size_t n, size_t *out_n) {
    if (!spv_ok(w, n)) return NULL;
    int32_t at[8]; if (spv_outputs(w, n, at) != 2 || at[0] < 0 || at[1] < 0) return NULL;
    wbuf o = {0}; wput(&o, w, 5);
    for (size_t i = 5; i < n; i += w[i] >> 16) {
        uint32_t wc = w[i] >> 16;
        if ((w[i] & 0xffff) == OP_DECORATE && wc == 4 && w[i + 1] == (uint32_t)at[1] && w[i + 2] == DEC_LOCATION) {
            uint32_t d[8] = {4u << 16 | OP_DECORATE, w[i + 1], DEC_LOCATION, 0, 4u << 16 | OP_DECORATE, w[i + 1], DEC_INDEX, 1}; wput(&o, d, 8);
        } else wput(&o, w + i, wc);
    }
    *out_n = o.n; return o.w;
}
// The alpha test (nxnative's dmsh_alpha.py library, as nvn_mtl.m at_fn / at_bind use it): before each return of the entry point, colour 0's alpha is compared with the
// reference by NVN's alpha function, both in a push constant block {uint function; float reference;} (fragment stage, offset 0, 8 bytes), and the fragment is discarded
// when the comparison fails. The function and reference are data, so one pipeline serves every value. Functions NEVER = 1 … ALWAYS = 8: function − 1 is a mask of
// less (1), equal (2), greater (4). Needs colour 0 as a float vec4 output. New words or NULL
uint32_t *vkspv_alpha_test(const uint32_t *w, size_t n, size_t *out_n) {
    if (!spv_ok(w, n)) return NULL;
    int32_t at[8]; spv_outputs(w, n, at);
    if (at[0] < 0) return NULL;
    uint32_t bound = w[3], *def = calloc(bound, sizeof *def), entry = 0, f32 = 0, u32 = 0, tbool = 0, pof = 0;
    size_t first_type = 0, first_fn = 0;
    for (size_t i = 5; i < n; ) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (!wc || i + wc > n) { free(def); return NULL; }
        if (op >= OP_TYPE_VOID && op <= OP_TYPE_POINTER && wc >= 2 && w[i + 1] < bound) { def[w[i + 1]] = (uint32_t)i; if (!first_type) first_type = i; }
        if (op == OP_VARIABLE && wc >= 4 && w[i + 2] < bound) def[w[i + 2]] = (uint32_t)i;
        if (op == OP_ENTRY_POINT && wc >= 3 && !entry) entry = w[i + 2];
        if (op == OP_TYPE_FLOAT && wc == 3 && w[i + 2] == 32) f32 = w[i + 1];
        if (op == OP_TYPE_INT && wc == 4 && w[i + 2] == 32 && !w[i + 3]) u32 = w[i + 1];
        if (op == OP_TYPE_BOOL && wc == 2) tbool = w[i + 1];
        if (op == OP_FUNCTION && !first_fn) first_fn = i;
        i += wc;
    }
    // colour 0: an Output pointer to a vector of 4 floats
    uint32_t vi = def[at[0]], pt = vi ? w[vi + 1] : 0, pti = pt < bound ? def[pt] : 0, vt = pti && (w[pti] & 0xffff) == OP_TYPE_POINTER ? w[pti + 3] : 0, vti = vt < bound ? def[vt] : 0;
    int ok = entry && f32 && first_type && first_fn && vti && (w[vti] & 0xffff) == OP_TYPE_VECTOR && w[vti + 2] == f32 && w[vti + 3] == 4;
    for (size_t i = 5; ok && i < n; i += w[i] >> 16)   // an existing Output float pointer (a second declaration of a non-aggregate type is invalid)
        if ((w[i] & 0xffff) == OP_TYPE_POINTER && w[i + 2] == SC_OUTPUT && w[i + 3] == f32) pof = w[i + 1];
    free(def);
    if (!ok) return NULL;
    // new ids, declarations and decorations
    wbuf ty = {0}, dec = {0};
    #define T_(...) do { uint32_t t_[] = {__VA_ARGS__}; wput(&ty, t_, sizeof t_ / 4); } while (0)
    if (!u32) { u32 = bound++; T_(4u << 16 | OP_TYPE_INT, u32, 32, 0); }
    if (!tbool) { tbool = bound++; T_(2u << 16 | OP_TYPE_BOOL, tbool); }
    if (!pof) { pof = bound++; T_(4u << 16 | OP_TYPE_POINTER, pof, SC_OUTPUT, f32); }
    uint32_t st = bound++, pst = bound++, pu = bound++, pfl = bound++, var = bound++, c[5];
    T_(4u << 16 | OP_TYPE_STRUCT, st, u32, f32); T_(4u << 16 | OP_TYPE_POINTER, pst, SC_PUSH, st);
    T_(4u << 16 | OP_TYPE_POINTER, pu, SC_PUSH, u32); T_(4u << 16 | OP_TYPE_POINTER, pfl, SC_PUSH, f32);
    T_(4u << 16 | OP_VARIABLE, pst, var, SC_PUSH);
    for (uint32_t k = 0; k < 5; k++) { c[k] = bound++; T_(4u << 16 | OP_CONSTANT, u32, c[k], k); }
    #undef T_
    uint32_t d[] = {3u << 16 | OP_DECORATE, st, DEC_BLOCK, 5u << 16 | OP_MEMBER_DECORATE, st, 0, DEC_OFFSET, 0, 5u << 16 | OP_MEMBER_DECORATE, st, 1, DEC_OFFSET, 4};
    wput(&dec, d, sizeof d / 4);
    // the rewrite: decorations before the first type, declarations before the first function, the test before each return of the entry point
    wbuf o = {0}; wput(&o, w, 5);
    uint32_t in_fn = 0;
    for (size_t i = 5; i < n; i += w[i] >> 16) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xffff;
        if (i == first_type) wput(&o, dec.w, dec.n);
        if (i == first_fn) wput(&o, ty.w, ty.n);
        if (op == OP_FUNCTION) in_fn = w[i + 2];
        if (op == OP_RETURN && in_fn == entry) {
            uint32_t pf = bound++, f = bound++, pr = bound++, r = bound++, pa = bound++, a = bound++, m = bound++, lt = bound++, eq = bound++, gt = bound++;
            uint32_t b0 = bound++, h0 = bound++, p0 = bound++, b1 = bound++, h1 = bound++, p1 = bound++, b2 = bound++, h2 = bound++, p2 = bound++, q = bound++, pass = bound++;
            uint32_t ok_l = bound++, kill_l = bound++;
            uint32_t s[] = {
                5u << 16 | OP_ACCESS_CHAIN, pu, pf, var, c[0],   4u << 16 | OP_LOAD, u32, f, pf,
                5u << 16 | OP_ACCESS_CHAIN, pfl, pr, var, c[1],  4u << 16 | OP_LOAD, f32, r, pr,
                5u << 16 | OP_ACCESS_CHAIN, pof, pa, (uint32_t)at[0], c[3], 4u << 16 | OP_LOAD, f32, a, pa,
                5u << 16 | OP_ISUB, u32, m, f, c[1],
                5u << 16 | OP_FORD_LESS, tbool, lt, a, r, 5u << 16 | OP_FORD_EQUAL, tbool, eq, a, r, 5u << 16 | OP_FORD_GREATER, tbool, gt, a, r,
                5u << 16 | OP_BITWISE_AND, u32, b0, m, c[1], 5u << 16 | OP_INOT_EQUAL, tbool, h0, b0, c[0], 5u << 16 | OP_LOGICAL_AND, tbool, p0, h0, lt,
                5u << 16 | OP_BITWISE_AND, u32, b1, m, c[2], 5u << 16 | OP_INOT_EQUAL, tbool, h1, b1, c[0], 5u << 16 | OP_LOGICAL_AND, tbool, p1, h1, eq,
                5u << 16 | OP_BITWISE_AND, u32, b2, m, c[4], 5u << 16 | OP_INOT_EQUAL, tbool, h2, b2, c[0], 5u << 16 | OP_LOGICAL_AND, tbool, p2, h2, gt,
                5u << 16 | OP_LOGICAL_OR, tbool, q, p0, p1, 5u << 16 | OP_LOGICAL_OR, tbool, pass, q, p2,
                3u << 16 | OP_SELECTION_MERGE, ok_l, 0, 4u << 16 | OP_BRANCH_COND, pass, ok_l, kill_l,
                2u << 16 | OP_LABEL, kill_l, 1u << 16 | OP_KILL, 2u << 16 | OP_LABEL, ok_l};
            wput(&o, s, sizeof s / 4);
        }
        if (op == OP_FUNCTION_END) in_fn = 0;
        if (op == OP_ENTRY_POINT && w[i + 2] == entry && w[1] >= 0x00010400) {   // SPIR-V 1.4 on: an entry point lists every global it uses
            uint32_t h = (wc + 1) << 16 | op; wput(&o, &h, 1); wput(&o, w + i + 1, wc - 1); wput(&o, &var, 1); continue;
        }
        wput(&o, w + i, wc);
    }
    o.w[3] = bound;
    free(ty.w); free(dec.w);
    *out_n = o.n; return o.w;
}
static const vks_shader *fs_variant_l(const vks_shader *fs_, int flags) {
    vks_shader *fs = (vks_shader *)fs_;
    if (!flags || !fs->words || fs->stage != VKS_FS) return fs;
    uint64_t key = 0x100u | (unsigned)flags;
    for (int i = 0; i < fs->nvar; i++) if (fs->var[i].key == key) return fs->var[i].s;
    if (fs->nvar == 8) return fs;
    size_t n = fs->nwords, nn = 0; uint32_t *w = NULL; int dual = 0, alpha = 0;
    if (flags & VKSH_FS_DUAL) {
        if ((w = vkspv_dual_source(fs->words, n, &nn))) { n = nn; dual = 1; }
        else { static int k; if (k++ < 8) VKRED("shader %s: no dual-source variant (it does not write exactly outputs 0 and 1): the second source counts as zero", fs->key); }
    }
    if (flags & VKSH_FS_ALPHA) {
        uint32_t *a = vkspv_alpha_test(w ? w : fs->words, n, &nn);
        if (a) { free(w); w = a; n = nn; alpha = 1; }
        else { static int k; if (k++ < 8) VKRED("shader %s: no alpha test variant (its colour 0 is not a float vec4): drawn without the test", fs->key); }
    }
    VkShaderModule mod = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo mci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, n * 4, w};
    if (!w || vkCreateShaderModule(g_vk.dev, &mci, NULL, &mod) != VK_SUCCESS) { free(w); fs->var[fs->nvar].key = key; fs->var[fs->nvar].s = fs; fs->nvar++; return fs; }   // the base, remembered
    vks_shader *s = malloc(sizeof *s); *s = *fs;
    s->mod = mod; s->words = w; s->nwords = n; s->base = fs; s->nvar = 0; s->dual = dual; s->alpha = alpha; s->fs_flags = (uint8_t)flags;
    if (dual) s->out_mask = fs->out_mask & ~2u;   // output 1 is target 0's second source now, not a target of its own
    fs->var[fs->nvar].key = key; fs->var[fs->nvar].s = s; fs->nvar++;
    static int nl; if (nl++ < 16) VKLOG("fragment shader %s: %s%s%s variant", fs->key, dual ? "dual-source" : "", dual && alpha ? " + " : "", alpha ? "alpha test" : "");
    return s;
}
const vks_shader *vksh_fs_variant(const vks_shader *fs, int flags) {
    if (!flags) return fs;
    AcquireSRWLockExclusive(&g_shl); const vks_shader *r = fs_variant_l(fs, flags); ReleaseSRWLockExclusive(&g_shl);
    return r;
}

// ---------- The c1 bank ----------
static int c1_store(vks_shader *s, const uint8_t *data, uint32_t len) {
    VkDeviceSize need = ((VkDeviceSize)len + 255) & ~(VkDeviceSize)255, align = g_vk.props.limits.minUniformBufferOffsetAlignment;
    if (align < 256) align = 256;
    if (!g_c1.buf || g_c1.used + need > g_c1.size) {
        // a new bank; the old one stays (shaders keep their slices for the run)
        VkDeviceSize sz = 4u << 20; if (need > sz) sz = need;
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
        VkBuffer b; vk_alloc m;
        if (vkCreateBuffer(g_vk.dev, &bci, NULL, &b) != VK_SUCCESS) return -1;
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_vk.dev, b, &mr);
        if (vk_alloc_mem(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &m)) { vkDestroyBuffer(g_vk.dev, b, NULL); return -1; }
        vkBindBufferMemory(g_vk.dev, b, m.mem, m.off);
        memset(m.map, 0, (size_t)sz);
        vk_name((uint64_t)b, VK_OBJECT_TYPE_BUFFER, "c1 bank");
        g_c1.buf = b; g_c1.mem = m; g_c1.used = 0; g_c1.size = sz;
    }
    VkDeviceSize off = (g_c1.used + align - 1) / align * align;
    memcpy((uint8_t *)g_c1.mem.map + off, data, len);
    g_c1.used = off + need;
    s->c1buf = g_c1.buf; s->c1off = off; s->c1_len = len;
    return 0;
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// ---------- From a module and its reflection ----------
static int stage_of(const char *s) {
    return !strcmp(s, "vs") ? VKS_VS : !strcmp(s, "ps") ? VKS_FS : !strcmp(s, "gs") ? VKS_GS : !strcmp(s, "cs") ? VKS_CS : !strcmp(s, "hs") || !strcmp(s, "tcs") ? VKS_TCS : !strcmp(s, "ds") || !strcmp(s, "tes") ? VKS_TES : -1;
}
static VkDescriptorType desc_type(const char *d) {
    if (!strcmp(d, "uniform_buffer")) return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if (!strcmp(d, "storage_buffer")) return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if (!strcmp(d, "combined_image_sampler")) return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    if (!strcmp(d, "sampled_image")) return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if (!strcmp(d, "sampler")) return VK_DESCRIPTOR_TYPE_SAMPLER;
    if (!strcmp(d, "storage_image")) return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    if (!strcmp(d, "uniform_texel_buffer")) return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    if (!strcmp(d, "storage_texel_buffer")) return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}
static int ct_of(const char *t) { return !t ? VKS_CT_FLOAT : !strcmp(t, "uint") ? VKS_CT_UINT : !strcmp(t, "int") ? VKS_CT_INT : VKS_CT_FLOAT; }

void vksh_free(vks_shader *s) {
    if (!s || s == &g_failed_mark) return;
    if (s->mod) vkDestroyShaderModule(g_vk.dev, s->mod, NULL);
    if (s->mod_plain) vkDestroyShaderModule(g_vk.dev, s->mod_plain, NULL);
    for (int i = 0; i < s->nvar; i++) if (s->var[i].s != s) vksh_free(s->var[i].s);   // a variant that could not be made is the shader itself
    if (s->dsl && !s->base) vkDestroyDescriptorSetLayout(g_vk.dev, s->dsl, NULL);
    if (!s->base) free(s->b);
    free(s->words); free(s);
}

vks_shader *vksh_make(const char *key, const uint8_t *spv, size_t spv_size, const char *json, size_t json_size, const char **why) {
    *why = NULL;
    vj *j = vj_parse(json, json_size);
    if (!j) { *why = "its reflection is not JSON"; return NULL; }
    vks_shader *s = calloc(1, sizeof *s);
    snprintf(s->key, sizeof s->key, "%s", key);
    s->stage = stage_of(vj_str(vj_get(j, "stage"), ""));
    if (s->stage < 0) { *why = "unknown stage"; goto fail; }
    s->set = vksh_set_of(s->stage);
    size_t nw = spv_size / 4;
    uint32_t *w = malloc(spv_size); memcpy(w, spv, spv_size);
    if (vkspv_set_descriptor_set(w, nw, (uint32_t)s->set) < 0) { free(w); *why = "malformed SPIR-V"; goto fail; }
    if (s->stage != VKS_FS && s->stage != VKS_CS && g_vk.props.limits.maxCombinedClipAndCullDistances) {
        size_t tn = vkspv_trim_cull(&w, nw, g_vk.props.limits.maxCombinedClipAndCullDistances);
        if (tn) nw = tn; else { static int n; if (n++ < 4) VKRED("shader %s: more clip and cull distances than the device takes, and the cull ones are used", key); }
    }
    static int ballot_fix = -1;   // decided at the first shader
    if (ballot_fix < 0) {
        const char *e = getenv("NXRT_VK_BALLOT_FIX");
        ballot_fix = vkspv_ballot_fix_wanted(g_vk.props.vendorID, e);
        if (e) VKLOG("subgroup ballots: the per-lane rewrite %s (NXRT_VK_BALLOT_FIX=%s)", ballot_fix ? "on" : "off", e);
        else if (ballot_fix) VKLOG("subgroup ballots: counters made per lane on this NVIDIA GPU (the hang after early returns: #12, D-1)");
        else VKLOG("subgroup ballots left as translated: their rewrite is for NVIDIA's GPUs (NXRT_VK_BALLOT_FIX=1 makes it anyway)");
    }
    if (ballot_fix) {
        int nb = 0; size_t bn = vkspv_ballot_per_lane(&w, nw, &nb);
        if (nb) { nw = bn; VKLOG("shader %s: %d subgroup ballot%s for a counter made per lane (NVIDIA hang after early returns: #12, D-1)", key, nb, nb > 1 ? "s" : ""); }
    }
    static int loop_guard = -1;   // decided at the first shader, as the ballot rewrite (vkspv_loop_guard_wanted: 1 table-size guards, 2 constant caps, 0 none)
    if (loop_guard < 0) {
        const char *e = getenv("NXRT_VK_LOOP_GUARD");
        loop_guard = vkspv_loop_guard_wanted(g_vk.props.vendorID, e);
        const char *what = loop_guard == 1 ? "kept to the items their tables hold" : loop_guard == 2 ? "capped at 16384" : "off";
        if (e) VKLOG("the loop guards (634565ab, 560a0262, 2cb5df5c): %s (NXRT_VK_LOOP_GUARD=%s)", what, e);
        else if (loop_guard == 2) VKLOG("the loop guards (634565ab, 560a0262, 2cb5df5c): %s (not NVIDIA: AMD's Windows driver crashed making the table-size guard; NXRT_VK_LOOP_GUARD=1 makes those, 0 none)", what);
        else VKLOG("the loop guards (634565ab, 560a0262, 2cb5df5c): %s", what);
    }
    // A guarded module keeps the module as translated beside it (vk_pipe.c: a driver that cannot make the guarded pipeline gets that one)
    int guarded = loop_guard && (!strcmp(key, "634565ab77c61463") || !strcmp(key, "560a0262fe47818a") || !strcmp(key, "2cb5df5cb96b324e"));
    uint32_t *w_plain = guarded ? malloc(nw * 4) : NULL; size_t n_plain = nw;
    if (w_plain) memcpy(w_plain, w, nw * 4);
    const char *table = loop_guard == 1 ? "kept to the items its table holds" : "capped at 16384";
    if (loop_guard && !strcmp(key, "634565ab77c61463")) {   // D-1's hang on 1.3.0: a nearest-item search whose count is a constant buffer word (8 items in play)
        int nl = 0; size_t ln = vkspv_limit_count(&w, nw, "cp_c3", 0, loop_guard == 1 ? "cp_t_tcb_10" : NULL, loop_guard == 1 ? 5 : 1, &nl);
        if (nl) { nw = ln; VKLOG("shader %s: its item loop %s (%d comparison%s; a GPU hang on D-1's RTX 3050)", key, table, nl, nl > 1 ? "s" : ""); }
        else VKRED("shader %s: its item loop could not be limited (the module is not as expected)", key);
    }
    if (loop_guard && !strcmp(key, "560a0262fe47818a")) {   // D-11's hang on 1.3.1: per-cluster light lists, two loops over texel tables (one texel an item) whose counts are c3's words 12 and 13
        int n1 = 0, n2 = 0; size_t ln = vkspv_limit_count(&w, nw, "cp_c3", 12, loop_guard == 1 ? "cp_t_tcb_10" : NULL, 1, &n1); if (n1) nw = ln;
        ln = vkspv_limit_count(&w, nw, "cp_c3", 13, loop_guard == 1 ? "cp_t_tcb_12" : NULL, 1, &n2); if (n2) nw = ln;
        if (n1 && n2) VKLOG("shader %s: its two light loops %s (%d + %d comparisons; a GPU hang on D-11's GTX 1650)", key, loop_guard == 1 ? "kept to the lights their tables hold" : table, n1, n2);
        else VKRED("shader %s: its light loops could not both be limited (%d + %d comparisons; the module is not as expected)", key, n1, n2);
    }
    if (loop_guard && !strcmp(key, "2cb5df5cb96b324e")) {   // Styphix's hang on 1.3.3: a loop over a list whose count is a word of the game's data texture (variable %2154)
        int c = 0; size_t ln = vkspv_cap_bound(&w, nw, 2154, 16384, &c);
        if (c) { nw = ln; VKLOG("shader %s: its list loop kept to at most 16384 entries (a GPU hang on 1.3.3)", key); }
        else VKRED("shader %s: its list loop could not be capped (the module is not as expected)", key);
    }
    if (w_plain && n_plain == nw && !memcmp(w_plain, w, nw * 4)) { free(w_plain); w_plain = NULL; }   // nothing was changed
    // bindings
    const vj *bs = vj_get(j, "bindings");
    s->nb = bs ? bs->n : 0; s->b = calloc(s->nb ? s->nb : 1, sizeof *s->b);
    VkDescriptorSetLayoutBinding *lb = calloc(s->nb ? s->nb : 1, sizeof *lb);
    for (int i = 0; i < s->nb; i++) {
        const vj *e = vj_at(bs, i), *nv = vj_get(e, "nvn"); vks_bind *b = &s->b[i];
        b->binding = (uint32_t)vj_int(vj_get(e, "binding"), 0); b->count = (uint32_t)vj_int(vj_get(e, "count"), 1);
        b->type = desc_type(vj_str(vj_get(e, "descriptor"), ""));
        if (vj_int(vj_get(e, "set"), 0) != 0) { *why = "a descriptor outside set 0"; free(w); free(w_plain); free(lb); goto fail; }
        if (b->type == VK_DESCRIPTOR_TYPE_MAX_ENUM) { *why = "an unknown descriptor type"; free(w); free(w_plain); free(lb); goto fail; }
        const char *k = vj_str(vj_get(nv, "kind"), "?");
        b->reg = (int16_t)vj_int(vj_get(nv, "reg"), 0); b->bank = (uint8_t)vj_int(vj_get(nv, "bank"), 0);
        b->kind = !strcmp(k, "support") ? VKB_SUPPORT : !strcmp(k, "cbuf") ? VKB_CBUF : !strcmp(k, "c1") ? VKB_C1 : !strcmp(k, "drvcb") ? VKB_DRV : !strcmp(k, "sbuf") ? VKB_SBUF :
                  !strcmp(k, "tex") ? VKB_TEX : !strcmp(k, "tbuf") ? VKB_TBUF : !strcmp(k, "img") ? VKB_IMG : VKB_UNKNOWN;
        if (b->kind == VKB_UNKNOWN) { static int n; if (n++ < 16) VKRED("shader %s binding %u: kind %s is not replayed yet (bound as empty)", key, b->binding, k); }
        b->rw = vj_true(vj_get(nv, "rw")); b->c0_off = (uint16_t)vj_int(vj_get(nv, "c0_offset"), 0);
        const vj *sm = vj_get(nv, "sampler");
        if (sm) { const char *sk = vj_str(vj_get(sm, "kind"), ""); b->samp_reg = (int16_t)vj_int(vj_get(sm, "reg"), 0);
            b->samp_kind = !strcmp(sk, "samp_tex") ? VKB_SAMP_TEX : !strcmp(sk, "samp") ? VKB_SAMP_SLOT : !strcmp(sk, "samp_id") ? VKB_SAMP_ID : VKB_SAMP_NONE; }
        else if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) { b->samp_kind = VKB_SAMP_TEX; b->samp_reg = b->reg; }
        vkspv_img im; b->vtype = VK_IMAGE_VIEW_TYPE_2D;
        if (!vkspv_image_of(w, nw, b->binding, &im) && im.is_image) { b->dim = im.dim; b->vtype = view_type(im.dim, im.arrayed); b->ct = im.ct; b->depth = im.depth; b->ms = im.ms; }
        lb[i] = (VkDescriptorSetLayoutBinding){b->binding, b->type, b->count, (VkShaderStageFlags)vksh_vk_stage(s->stage), NULL};
        b->scale_idx = -1;
        if (b->kind == VKB_SUPPORT) s->reads_scale = (uint8_t)vkspv_reads_member(w, nw, b->binding, 5);   // Ryujinx's SupportBuffer.RenderScale
    }
    // F-5: the render scale entry of each texture and image, as the translator numbers them (its texture descriptors sorted by binding, the images after
    // them: Ryujinx's TexelFetchScale / TextureSizeUnscale index). The reflection's "translator" entries are those descriptors, the bindings come sorted
    int ntex = 0;
    for (int i = 0; i < s->nb; i++) ntex += vj_get(vj_get(vj_at(bs, i), "nvn"), "translator") && s->b[i].kind != VKB_IMG;
    for (int i = 0, t = 0, m = 0; i < s->nb && s->reads_scale; i++) {
        if (!vj_get(vj_get(vj_at(bs, i), "nvn"), "translator")) continue;
        int idx = 1 + (s->b[i].kind == VKB_IMG ? ntex + m++ : t++);
        if (idx < 73) s->b[i].scale_idx = (int8_t)idx;
    }
    // interface
    const vj *in = vj_get(j, "inputs");
    for (int i = 0; in && i < in->n && s->stage == VKS_VS && s->nin < 32; i++) {
        const vj *e = vj_at(in, i);
        s->in[s->nin].loc = (uint8_t)vj_int(vj_get(e, "location"), 0); s->in[s->nin].ct = (uint8_t)ct_of(vj_str(vj_get(e, "type"), "float"));
        s->in[s->nin].ncomp = (uint8_t)vj_int(vj_get(e, "components"), 4); s->nin++;
    }
    const vj *out = vj_get(j, "outputs");
    for (int i = 0; out && i < out->n && s->stage == VKS_FS; i++) {
        const vj *e = vj_at(out, i); int l = vj_int(vj_get(e, "location"), -1);
        if (l >= 0 && l < 8) { s->out_mask |= 1u << l; s->out_ct[l] = (uint8_t)ct_of(vj_str(vj_get(e, "type"), "float")); }
    }
    const vj *ls = vj_get(j, "local_size");
    for (int i = 0; i < 3; i++) s->local[i] = (uint32_t)vj_int(vj_at(ls, i), 1);
    s->subgroups = vj_int(vj_get(j, "uses_subgroups"), 0) != 0;
    const vj *dr = vj_get(j, "drv_reads");
    for (int i = 0; dr && i < dr->n; i++) { int bank = atoi(dr->keys[i]); if (bank >= 0 && bank < 3) s->drv_read[bank] = 1; }
    const vj *s2 = vj_get(j, "s2_compute_layout");
    if (s2) { s->s2cs = 1; s->s2_ssbo_base = (uint32_t)vj_int(vj_get(s2, "ssbo_base"), 0); s->s2_tex_base = (uint32_t)vj_int(vj_get(s2, "tex_base"), 0); s->s2_ntex = (uint32_t)vj_int(vj_get(s2, "n_tex"), 0); }
    // c1: the bytes nxshader took from the shader's record (its translation job), published beside the reflection
    int has_c1 = 0; for (int i = 0; i < s->nb; i++) has_c1 |= s->b[i].kind == VKB_C1;
    if (has_c1) {
        const char *hex = vj_str(vj_get(j, "c1"), NULL);
        if (!hex) { s->c1_missing = 1; static int n; if (n++ < 8) VKRED("shader %s reads its c1 segment but the reflection has no \"c1\" (an older nxshader publish): it reads zeros", key); }
        else {
            size_t hl = strlen(hex) / 2; uint8_t *c1 = malloc(hl ? hl : 1);
            for (size_t k = 0; k < hl; k++) { int a = hexval(hex[2 * k]), b = hexval(hex[2 * k + 1]); c1[k] = (uint8_t)(a < 0 || b < 0 ? 0 : a * 16 + b); }
            if (hl && c1_store(s, c1, (uint32_t)hl)) VKRED("shader %s: no memory for its c1 segment", key);
            free(c1);
        }
    }
    // Vulkan objects
    VkShaderModuleCreateInfo mci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, nw * 4, w};
    VkResult r = vkCreateShaderModule(g_vk.dev, &mci, NULL, &s->mod);
    if (w_plain && r == VK_SUCCESS) {
        VkShaderModuleCreateInfo pci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, n_plain * 4, w_plain};
        if (vkCreateShaderModule(g_vk.dev, &pci, NULL, &s->mod_plain) != VK_SUCCESS) s->mod_plain = VK_NULL_HANDLE;
    }
    free(w_plain);
    if (s->stage == VKS_VS || s->stage == VKS_FS) { s->words = w; s->nwords = nw; } else free(w);   // the sources of their variants
    if (r != VK_SUCCESS) { free(lb); *why = "vkCreateShaderModule failed"; goto fail; }
    VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, (uint32_t)s->nb, lb};
    r = vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &s->dsl);
    free(lb);
    if (r != VK_SUCCESS) { *why = "vkCreateDescriptorSetLayout failed"; goto fail; }
    vj_free(j);
    return s;
fail:
    vj_free(j); vksh_free(s);
    return NULL;
}

static vks_shader *get_l(const nxs_shader *n) {   // g_shl held
    vks_shader *s = vk_hm_get(&g_sh, n->key);
    if (s == &g_failed_mark) return NULL;
    if (!s) {
        const char *why = NULL;
        s = vksh_make(n->key, n->spv, n->spv_size, n->json, n->json_size, &why);
        if (!s) { g_failed++; VKRED("shader %s cannot be used: %s; its draws are skipped", n->key, why ? why : "?"); vk_hm_put(&g_sh, n->key, &g_failed_mark); return NULL; }
        g_loaded++;
        vk_hm_put(&g_sh, n->key, s);
        { char k[128]; int o = 0; int cnt[VKB_UNKNOWN + 1] = {0}; for (int i = 0; i < s->nb; i++) cnt[s->b[i].kind]++;
          static const char *nm[] = {"", "support", "cbuf", "c1", "drv", "sbuf", "tex", "tbuf", "img", "?"};
          for (int i = 1; i <= VKB_UNKNOWN; i++) if (cnt[i]) o += snprintf(k + o, sizeof k - o, " %s %d", nm[i], cnt[i]);
          VKLOG("shader %s (%s):%s%s%s", n->key, s->stage == VKS_VS ? "vertex" : s->stage == VKS_FS ? "fragment" : s->stage == VKS_CS ? "compute" : s->stage == VKS_GS ? "geometry" : "tessellation", k,
                s->drv_read[0] || s->drv_read[2] ? " (reads driver segments)" : "", s->s2cs ? " (Switch 2 compute bank)" : ""); }
    }
    s->uses++;
    return s;
}
const vks_shader *vksh_get(const prog_t *p, unsigned i) {
    const nxs_shader *n = NULL;
    if (!p || nxs_resolve(p, i, &n) != 1 || !n) return NULL;
    AcquireSRWLockExclusive(&g_shl); vks_shader *s = get_l(n); ReleaseSRWLockExclusive(&g_shl);
    return s;
}
const vks_shader *vksh_by_key(const char *key) {
    const nxs_shader *n = nxs_peek(key);
    if (!n) return NULL;
    AcquireSRWLockExclusive(&g_shl); vks_shader *s = get_l(n); ReleaseSRWLockExclusive(&g_shl);
    return s;
}

int vksh_init(void) {
    VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL};
    return vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_empty) == VK_SUCCESS ? 0 : -1;
}
void vksh_shutdown(void) {
    for (uint32_t i = 0; i < g_sh.cap; i++) if (g_sh.vals[i]) vksh_free(g_sh.vals[i]);
    vk_hm_free(&g_sh);
    if (g_empty) vkDestroyDescriptorSetLayout(g_vk.dev, g_empty, NULL);
    g_empty = VK_NULL_HANDLE;
}
