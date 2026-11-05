#!/usr/bin/env python3
"""Turns a copy of nxnative into the swap-in host (docs/m4-swapin.md). Run on the Mac against the copy only, never ~/nxnative:
    rsync -a --exclude '*.o' ~/nxnative/ ~/nxnative-swapin/
    python3 apply_swapin_patch.py ~/nxnative-swapin <windows-native checkout>
    cd ~/nxnative-swapin && DM_GAMES_DIR="$HOME/installer-test/build201-clean/profile" make -C dm/host GAME=0400A3A01EE4C000 dmhost -j8
It brings research/05's trace module along (research/mac-traces/hle_trace.c and its three hooks), with two additions: lifted
functions log their first entries into its coverage log, and DM_TRACE_COV_ENTRIES=1 arms function starts only. Then:
  - hle_swapin.c and swapin.h into dm/host (the Makefile takes hle_*.c by wildcard);
  - loader.c maps each module at its DM_SWAPIN_BASES address (swapin_module_map), so lifted code's addresses hold;
  - main.c installs the swap-in dylib after the probes and before the trace module (whose coverage traps then sit on top of the
    swapped entries and put the swap-in branch back when they fire)."""
import pathlib, shutil, sys

root = pathlib.Path(sys.argv[1]).expanduser().resolve()
repo = pathlib.Path(sys.argv[2]).expanduser().resolve()
if root.name == 'nxnative':
    sys.exit('refusing to patch ~/nxnative itself: give the copy')
host = root / 'dm/host'
shutil.copy(repo / 'research/mac-traces/hle_trace.c', host / 'hle_trace.c')
shutil.copy(repo / 'src/swapin/nxnative/hle_swapin.c', host / 'hle_swapin.c')
shutil.copy(repo / 'src/swapin/swapin.h', host / 'swapin.h')


def edit(name, old, new):
    p = host / name; s = p.read_text()
    if new in s:
        return
    if s.count(old) != 1:
        sys.exit(f'{name}: anchor not found exactly once: {old!r}')
    p.write_text(s.replace(old, new))


# research/05's three hooks (research/mac-traces/apply_trace_patch.py)
edit('crash.c', 'void crash_set_trap_hook(int (*fn)(ucontext_t *uc)) { g_trap_hook = fn; }',
     'void crash_set_trap_hook(int (*fn)(ucontext_t *uc)) { g_trap_hook = fn; }\n'
     'int (*crash_get_trap_hook(void))(ucontext_t *uc) { return g_trap_hook; }   // trace copy: hle_trace.c chains to it')
edit('main.c', '    probe_install();   // DM_PROBE probes (for diagnostics)\n',
     '    probe_install();   // DM_PROBE probes (for diagnostics)\n'
     '    { void swapin_install(void); swapin_install(); }   // swap-in copy: DM_SWAPIN_LIB (hle_swapin.c)\n'
     '    { void trace_install(void); trace_install(); }   // trace copy: DM_TRACE (hle_trace.c)\n')
edit('hle.c', '        if (g->ret == ret_addr) switch (g->kind) {',
     '        if (g->ret == ret_addr) { extern int g_trace_on; void trace_gate(uint64_t, int, int, uint64_t, uint64_t *);   // trace copy\n'
     '            if (g_trace_on) trace_gate(g->ret, g->kind, g->arg, g->orig, regs); }\n'
     '        if (g->ret == ret_addr) switch (g->kind) {')
# the trace module's two additions
edit('hle_trace.c', 'static int trace_trap(ucontext_t *uc) {',
     'void trace_cov_note(uint64_t pc, int kind) {   // swap-in copy: first entries of lifted functions (hle_swapin.c)\n'
     '    crec_t *r = tlog_slot(&g_lcov); if (!r) return;\n'
     '    r->tid = (uint16_t)thr_cur_handle(); r->kind = (uint8_t)kind; r->at = pc;\n'
     '    atomic_store_explicit(&r->ms1, t_ms() + 1, memory_order_release);\n'
     '}\n'
     'static int trace_trap(ucontext_t *uc) {')
edit('hle_trace.c', 'static unsigned g_ind_max = 8;',
     'static unsigned g_ind_max = 8;\nstatic int g_entries_only;   // swap-in copy: DM_TRACE_COV_ENTRIES=1 arms function starts only')
edit('hle_trace.c', '    if (ind) g_ind_max = (unsigned)atoi(ind);',
     '    if (ind) g_ind_max = (unsigned)atoi(ind);\n    g_entries_only = getenv("DM_TRACE_COV_ENTRIES") != NULL;')
edit('hle_trace.c', '        else if (!in_fde[w]) { atomic_store(&t->st[w], ST_ARMED); t->narm_out++; }',
     '        else if (!in_fde[w] && !g_entries_only) { atomic_store(&t->st[w], ST_ARMED); t->narm_out++; }')
# frame-exact screenshots (DM_SWAPIN_SHOTS)
edit('hle_os.c', '    atomic_fetch_add(&g_guest_frame, 1);\n',
     '    atomic_fetch_add(&g_guest_frame, 1);\n'
     '    { void swapin_frame(uint64_t); swapin_frame(atomic_load(&g_guest_frame)); }   // swap-in copy (hle_swapin.c)\n')
# DM_SWAPIN_FS_DELAY_US: every ReadFile waits that long first (slows the game's loading without touching its code: the control
# experiment for timing-dependent divergences)
edit('hle_fs.c', 'size_t *out, uint64_t h, int64_t off, void *buf, size_t sz) { h = FH_IN(h);',
     'size_t *out, uint64_t h, int64_t off, void *buf, size_t sz) { h = FH_IN(h);\n'
     '    { static long us = -1; if (us < 0) { const char *e = getenv("DM_SWAPIN_FS_DELAY_US"); us = e ? atol(e) : 0; } if (us > 0) usleep((useconds_t)us); }   // swap-in copy')
# the modules at the bases the code was lifted for
edit('loader.c', '    m->base = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);\n'
                 '    if (m->base == MAP_FAILED) die("%s mmap", m->name);',
     '    void *swapin_module_map(const char *name, size_t size); m->base = swapin_module_map(m->name, m->size);   // swap-in copy (hle_swapin.c)\n'
     '    if (!m->base) m->base = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);\n'
     '    if (m->base == MAP_FAILED) die("%s mmap", m->name);')
print('patched', host)
