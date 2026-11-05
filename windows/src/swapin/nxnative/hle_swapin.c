// Swap-in copy of nxnative only (windows-native src/swapin, docs/m4-swapin.md): loads the swap-in dylib named by DM_SWAPIN_LIB
// and lets it run the functions DM_SWAPIN_LIST chooses as lifted code. DM_SWAPIN_BASES="main=<hex>,subsdk0=<hex>,sdk=<hex>"
// maps the modules at the bases the code was lifted for (loader.c asks swapin_module_map). Off unless those are set.
#include "dm.h"
#include "swapin.h"
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdarg.h>

static uint64_t module_hint(const char *name) {
    const char *e = getenv("DM_SWAPIN_BASES");
    if (!e || !*e) return 0;
    size_t n = strlen(name);
    for (const char *p = e; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
        if (!strncmp(p, name, n) && p[n] == '=') return strtoull(p + n + 1, NULL, 16);
    return 0;
}
// The module's zero-filled read-write mapping at its DM_SWAPIN_BASES address (mmap ignores a hint there), or NULL: anywhere
void *swapin_module_map(const char *name, size_t size) {
    mach_vm_address_t a = module_hint(name);
    if (!a) return NULL;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &a, size, VM_FLAGS_FIXED);
    if (kr != KERN_SUCCESS) {   // who holds the range
        mach_vm_address_t r = a; mach_vm_size_t rs = 0; vm_region_extended_info_data_t ei; mach_msg_type_number_t c = VM_REGION_EXTENDED_INFO_COUNT; mach_port_t o;
        if (mach_vm_region(mach_task_self(), &r, &rs, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&ei, &c, &o) == KERN_SUCCESS)
            LOG("swapin: the next region from %#llx: %#llx +%#llx, tag %u, protection %d", (unsigned long long)a, (unsigned long long)r,
                (unsigned long long)rs, ei.user_tag, ei.protection);
        die("swapin: cannot map %s at %#llx (%#zx bytes): %d", name, (unsigned long long)a, size, kr);
    }
    return (void *)a;
}

// DM_SWAPIN_SHOTS="<frame>:<png path>,...": a screenshot of exactly that presented frame (hle_os.c dm_guest_frame_tick calls this
// on every present), so runs with frame-numbered DM_KEYS steps compare frame for frame
void dm_gfx_shot_at(long k, const char *path);
void swapin_frame(uint64_t frame) {
    static int n = -1; static struct { uint64_t f; char path[512]; } shot[16];
    if (n < 0) {
        n = 0;
        const char *e = getenv("DM_SWAPIN_SHOTS");
        for (const char *p = e; p && *p && n < 16; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) {
            char *c; shot[n].f = strtoull(p, &c, 10);
            if (*c != ':') break;
            const char *end = strchr(c + 1, ','); size_t len = end ? (size_t)(end - c - 1) : strlen(c + 1);
            if (len >= sizeof shot[n].path) break;
            memcpy(shot[n].path, c + 1, len); shot[n].path[len] = 0; n++;
        }
    }
    for (int i = 0; i < n; i++)
        if (shot[i].f == frame + 1) { dm_gfx_shot_at(1, shot[i].path); LOG("swapin: screenshot of frame %llu → %s", (unsigned long long)shot[i].f, shot[i].path); }
}

static void swapin_log(const char *fmt, va_list ap) {
    flockfile(stderr);
    fputs("[dm] swapin: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    funlockfile(stderr);
}

uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr);
void thr_svc_enter(uint64_t *regs, uint64_t pc);
void thr_svc_leave(void);
uint64_t guest_ticks(void);
void dm_spin_wait(uint64_t gate, uint64_t site);
uint32_t thr_cur_handle(void);
extern int g_trace_on;
void trace_cov_note(uint64_t pc, int kind);   // hle_trace.c
static void cov_note(uint64_t pc, int kind) { if (g_trace_on) trace_cov_note(pc, kind); }

void swapin_install(void) {
    const char *lib = getenv("DM_SWAPIN_LIB");
    if (!lib || !*lib) return;
    void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h) die("swapin: cannot load %s: %s", lib, dlerror());
    NxwSwapinInit init = (NxwSwapinInit)dlsym(h, "nxw_swapin_init");
    if (!init) die("swapin: %s has no nxw_swapin_init", lib);
    static NxwSwapinHost host;
    host.api = NXW_SWAPIN_API;
    for (int i = 0; i < g_nmod && host.nmod < 8; i++) {
        module_t *m = &g_mod[i];
        if (m->nro) continue;
        NxwSwapinMapped *o = &host.mod[host.nmod++];
        strlcpy(o->name, m->name, sizeof o->name);
        o->base = (uint64_t)m->base, o->size = m->size, o->text_end = m->text_end;
    }
    host.tsd_k18 = (uint64_t)g_key18 * 8, host.tsd_ktls = (uint64_t)g_keytls * 8, host.tsd_ktlr = (uint64_t)g_keytlr * 8;
    host.module_dir = dm_module_dir();
    host.log = swapin_log;
    host.svc_dispatch = svc_dispatch;
    host.thr_svc_enter = thr_svc_enter;
    host.thr_svc_leave = thr_svc_leave;
    host.guest_ticks = guest_ticks;
    host.guest_clock_read_wait = guest_clock_read_wait;
    host.spin_wait = dm_spin_wait;
    host.thread_handle = thr_cur_handle;
    const char *tr = getenv("DM_TRACE");
    host.cov_note = tr && *tr ? cov_note : NULL;
    if (init(&host)) die("swapin: %s refused (see above)", lib);
}
