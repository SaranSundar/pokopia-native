int dm_sk_poll(void *fds, uint64_t n, int ms) {   // also hle_offline.c's plain pollfd form. Co-op on: a set whose descriptors are all virtual is waited on by the coop module
    sk_pollfd *p = fds;
    int virt = 0;
    for (uint64_t i = 0; i < n; i++) if (p[i].fd >= 0) { if (!mpsk_is(p[i].fd)) return sk_fail(); virt = 1; }
    if (virt) return (int)mpsk_ret(coop_vnet_poll(dm_coop(), (coop_pollfd *)fds, n, ms));   // readable when a datagram waits, always writable
    for (uint64_t i = 0; i < n; i++) p[i].revents = 0;
    return sk_idle(ms);
}
int dm_sk_select(int nfds, const uint64_t *r, const uint64_t *w, const uint64_t *e, const int64_t *tv) {   // fd_set: 1024 bits; timeval: {i64 seconds, i64 microseconds}
    const uint64_t *sets[3] = {r, w, e};
    int virt = 0;
    for (int k = 0; k < 3; k++) for (int fd = 0; sets[k] && fd < nfds && fd < 1024; fd++) if (sets[k][fd >> 6] >> (fd & 63) & 1) { if (!mpsk_is(fd)) return sk_fail(); virt = 1; }
    int64_t ms = tv ? tv[0] * 1000 + tv[1] / 1000 : -1;
    if (virt) return (int)mpsk_ret(coop_vnet_select(dm_coop(), nfds, (uint64_t *)r, (uint64_t *)w, (uint64_t *)e, ms > 0x7fffffff ? 0x7fffffff : (int)ms));
    return sk_idle(ms);
}
