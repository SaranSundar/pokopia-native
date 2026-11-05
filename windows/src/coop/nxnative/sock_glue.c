// Link Play (docs/coop-step1c.md): with co-op on, the AF_INET datagram sockets the game makes while nn::ldn is initialized are the coop module's virtual
// sockets (descriptors 0x100 and up on 169.254.S.0/24, hle_coop_vnet.c). Any other descriptor, and every socket with co-op off, keeps the offline answer
// (-1, ENETDOWN). A module result -errno comes back as -1 with the thread's last error set (FreeBSD values). hle_offline.c's overloads call these too.
#include "coop.h"
coop_node *dm_coop(void);   // hle_offline.c: NULL with co-op off
static int mpsk_is(int s) { coop_node *c = dm_coop(); return c && coop_vnet_is(c, s); }
static long mpsk_ret(int64_t r) { if (r >= 0) return (long)r; t_skerr = (int)-r; return -1; }
int mpsk_errno(void) { return t_skerr; }
int mpsk_socket(int fam, int type, int proto) {
    coop_node *c = dm_coop();
    if (!c) return sk_fail();
    int r = coop_vnet_socket(c, fam, type, proto);
    if (r < 0) LOG("[coop] sock: Socket(%d, %#x, %d) stays offline (only AF_INET datagram sockets after ldn::Initialize are virtual)", fam, type, proto);
    return (int)mpsk_ret(r);
}
int mpsk_bind(int s, const void *a, unsigned n) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_bind(dm_coop(), s, a, n)) : sk_fail(); }
int mpsk_close(int s) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_close(dm_coop(), s)) : sk_fail(); }
int mpsk_setopt(int s, int l, int o, const void *v, unsigned n) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_setsockopt(dm_coop(), s, l, o, v, n)) : sk_fail(); }
int mpsk_getopt(int s, int l, int o, void *v, unsigned *n) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_getsockopt(dm_coop(), s, l, o, v, n)) : sk_fail(); }
int mpsk_getname(int s, void *a, unsigned *n) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_getsockname(dm_coop(), s, a, n)) : sk_fail(); }
int mpsk_fcntl(int s, int cmd, int v) { return mpsk_is(s) ? (int)mpsk_ret(coop_vnet_fcntl(dm_coop(), s, cmd, v)) : sk_fail(); }
long mpsk_sendto(int s, const void *b, unsigned long n, int f, const void *to, unsigned tl) { return mpsk_is(s) ? mpsk_ret(coop_vnet_sendto(dm_coop(), s, b, n, f, to, tl)) : sk_fail(); }
long mpsk_send(int s, const void *b, unsigned long n, int f) { return mpsk_sendto(s, b, n, f, NULL, 0); }   // no connected sockets: DestAddrRequired on a virtual one
long mpsk_recvfrom(int s, void *b, unsigned long n, int f, void *from, unsigned *fl) { return mpsk_is(s) ? mpsk_ret(coop_vnet_recvfrom(dm_coop(), s, b, n, f, from, fl)) : sk_fail(); }
long mpsk_recv(int s, void *b, unsigned long n, int f) { return mpsk_recvfrom(s, b, n, f, NULL, NULL); }
