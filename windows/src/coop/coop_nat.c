// coop_nat: automatic port forwarding while this device hosts over the internet (docs/coop-nat.md). MIT, our own code, written from the public
// specifications: NAT-PMP (RFC 6886), PCP (RFC 6887), the UPnP Device Architecture 1.1 (SSDP discovery, the device description) and the UPnP IGD
// WANIPConnection / WANPPPConnection services (GetExternalIPAddress, AddPortMapping, DeletePortMapping). No library.
// The host asks its router to forward the hosting port, TCP and UDP, here, with a one-hour lease renewed at half time, and gives it back when hosting
// ends or the game quits (coop_nat_quit). A worker thread per node does the talking, each step with a short timeout; the node keeps what the player is
// shown (coop_nat_info): the method, the router's own internet address, and what stands in the way (no router answer, carrier-grade NAT, a second
// router, the port taken). Everything a router sends is untrusted: lengths are checked, text is cut to the buffers.
#include "coop_int.h"
#include <stdio.h>

#define NAT_LEASE 3600
#define PMP_PORT 5351
#define NAT_DESC "Pokopia Native"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void ipstr(char *o, size_t n, uint32_t ip) { snprintf(o, n, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 0xFF, ip >> 8 & 0xFF, ip & 0xFF); }

// ---------- NAT-PMP and PCP: a UDP request to the gateway's port 5351, sent again at 250, 500 and 1000 ms (RFC 6886 §3.1 starts so, and goes on
// longer than a player waits); only an answer from the gateway's address and port counts ----------
static int64_t udp_ask(uint32_t gw, uint16_t port, const uint8_t *req, size_t rn, uint8_t *resp, size_t cap, uint8_t want_op, int tries) {
    uint16_t bound; csock s = cplat_udp_open(0, 0, &bound);
    if (s == CSOCK_BAD) return -2;
    int64_t got = -1;
    for (int t = 0, wait = 250; t < tries && got < 0; t++, wait *= 2) {
        if (cplat_sendto(s, req, rn, gw, port)) { got = -2; break; }
        int64_t until = cplat_now_ms() + wait;
        while (got < 0) {
            int64_t left = until - cplat_now_ms(); if (left <= 0) break;
            cpollfd f = {s, CP_IN, 0};
            if (cplat_poll(&f, 1, (int)left) <= 0 || !(f.got & CP_IN)) continue;
            uint32_t ip; uint16_t pt; int64_t k = cplat_recvfrom(s, resp, cap, &ip, &pt);
            if (k == -2) { got = -3; break; }   // the port is closed there (an ICMP answer): nobody listens
            if (k >= 4 && ip == gw && pt == port && (resp[1] == want_op || resp[0] != req[0])) got = k;   // ours, or a version answer
        }
        if (got == -3) break;
    }
    cplat_close(s);
    return got;
}
// NAT-PMP: op 0 the router's internet address; op 1 (UDP) / op 2 (TCP) a mapping with a lifetime (0 deletes). The result code (0 success, 1 the
// router speaks another version, 2 refused, 3 network failure, 4 out of resources, 5 unknown op), -1 no answer, -2 not usable
int cnat_pmp(uint32_t gw, uint16_t gport, int op, uint16_t iport, uint16_t eport, uint32_t life, uint32_t *ext, uint16_t *mapped, uint32_t *life_out, int tries) {
    uint8_t req[12] = {0}, r[64]; size_t rn = op ? 12 : 2;
    req[1] = (uint8_t)op;
    if (op) { wr16(req + 4, iport); wr16(req + 6, eport); wr32(req + 8, life); }
    int64_t k = udp_ask(gw, gport, req, rn, r, sizeof r, (uint8_t)(128 + op), tries);
    if (k < 0) return -1;
    if (r[0] != 0) return r[0] == 2 && k >= 4 ? 1 : -2;   // a PCP server's answer (version 2): it wants PCP
    uint16_t res = rd16(r + 2);
    if (res) return res;
    if (!op) { if (k < 12) return -2; if (ext) *ext = rd32(r + 8); return 0; }
    if (k < 16 || rd16(r + 8) != iport) return -2;
    if (mapped) *mapped = rd16(r + 10);
    if (life_out) *life_out = rd32(r + 12);
    return 0;
}
// PCP MAP (RFC 6887 §11): proto 6 TCP, 17 UDP; the nonce ties a renewal or deletion to the mapping. The result code (0 success; 1 the router speaks
// NAT-PMP, 2 refused, 8 no resources, 11 no external address, ...), -1 no answer, -2 not usable
int cnat_pcp_map(uint32_t gw, uint16_t gport, uint32_t client, const uint8_t nonce[12], int proto, uint16_t iport, uint16_t eport, uint32_t life,
                 uint32_t *ext, uint16_t *mapped, uint32_t *life_out, int tries) {
    uint8_t req[60] = {0}, r[1100];
    req[0] = 2; req[1] = 1; wr32(req + 4, life);
    req[18] = 0xFF; req[19] = 0xFF; wr32(req + 20, client);          // the client's address as an IPv4-mapped IPv6 address
    memcpy(req + 24, nonce, 12); req[36] = (uint8_t)proto;
    wr16(req + 40, iport); wr16(req + 42, eport);
    req[54] = 0xFF; req[55] = 0xFF;                                   // suggested external address ::ffff:0.0.0.0: any IPv4
    int64_t k = udp_ask(gw, gport, req, sizeof req, r, sizeof r, 0x81, tries);
    if (k < 0) return -1;
    if (r[0] == 0) return 1;                                          // a NAT-PMP server's answer
    if (r[0] != 2 || r[1] != 0x81 || k < 60) return -2;
    if (r[3]) return r[3];
    if (memcmp(r + 24, nonce, 12) || r[36] != proto || rd16(r + 40) != iport) return -2;
    if (mapped) *mapped = rd16(r + 42);
    if (ext) *ext = rd32(r + 56);
    if (life_out) *life_out = rd32(r + 4);
    return 0;
}

// ---------- UPnP IGD ----------
// "http://<IPv4>[:port]/path": 0, or -1 for anything else (a router's description is at an IPv4 address in practice)
int cnat_url(const char *url, uint32_t *ip, uint16_t *port, char *path, size_t pn) {
    unsigned a, b, c, d, p = 80; int k = 0;
    if (strncmp(url, "http://", 7)) return -1;
    if (sscanf(url + 7, "%3u.%3u.%3u.%3u%n", &a, &b, &c, &d, &k) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return -1;
    const char *s = url + 7 + k;
    if (*s == ':') { int m = 0; if (sscanf(s + 1, "%5u%n", &p, &m) != 1 || !p || p > 65535) return -1; s += 1 + m; }
    if (*s && *s != '/') return -1;
    *ip = a << 24 | b << 16 | c << 8 | d; *port = (uint16_t)p;
    snprintf(path, pn, "%s", *s ? s : "/");
    return 0;
}
static int ci_starts(const char *s, const char *p) { for (; *p; s++, p++) { char x = *s, y = *p; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; } return 1; }
static const char *ci_find(const char *s, const char *p) { for (; *s; s++) if (ci_starts(s, p)) return s; return NULL; }
// An SSDP answer's LOCATION header: 0, or -1 when it has none
int cnat_ssdp_location(const char *resp, char *loc, size_t n) {
    const char *l = resp;
    for (; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
        if (!ci_starts(l, "location:")) continue;
        l += 9; while (*l == ' ' || *l == '\t') l++;
        size_t k = 0; while (l[k] && l[k] != '\r' && l[k] != '\n' && k + 1 < n) { loc[k] = l[k]; k++; }
        loc[k] = 0; return k ? 0 : -1;
    }
    return -1;
}
// SSDP M-SEARCH to 239.255.255.250:1900 for an Internet Gateway Device; the first answer with a LOCATION (the gateway's own first, when it is known)
static int ssdp(uint32_t gw, char *loc, size_t n, int ms) {
    static const char *const st[3] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                       "urn:schemas-upnp-org:service:WANIPConnection:1"};
    uint16_t bound; csock s = cplat_udp_open(0, 0, &bound);
    if (s == CSOCK_BAD) return -1;
    char req[300], r[1500]; int found = 0;
    for (int i = 0; i < 3; i++) {
        int k = snprintf(req, sizeof req, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", st[i]);
        cplat_sendto(s, req, (size_t)k, 0xEFFFFFFAu, 1900);
    }
    int64_t until = cplat_now_ms() + ms;
    while (cplat_now_ms() < until) {
        cpollfd f = {s, CP_IN, 0};
        if (cplat_poll(&f, 1, (int)(until - cplat_now_ms())) <= 0 || !(f.got & CP_IN)) continue;
        uint32_t ip; uint16_t pt; int64_t k = cplat_recvfrom(s, r, sizeof r - 1, &ip, &pt);
        if (k <= 0) continue;
        r[k] = 0; char l[256];
        if (cnat_ssdp_location(r, l, sizeof l)) continue;
        uint32_t lip; uint16_t lp; char path[200];
        if (cnat_url(l, &lip, &lp, path, sizeof path)) continue;
        if (!found || lip == gw) { snprintf(loc, n, "%s", l); found = 1; }
        if (lip == gw) break;
    }
    cplat_close(s);
    return found ? 0 : -1;
}
// One HTTP exchange (Connection: close): the status, the body (chunked bodies put together) in out; -1 no answer, -2 not HTTP
static int http(uint32_t ip, uint16_t port, const char *req, size_t rn, char *out, size_t cap, int ms) {
    csock s = cplat_tcp_connect(ip, port);
    if (s == CSOCK_BAD) return -1;
    int64_t until = cplat_now_ms() + ms; size_t sent = 0, got = 0; int ok = 0;
    char *buf = malloc(65536); if (!buf) { cplat_close(s); return -1; }
    while (cplat_now_ms() < until) {
        cpollfd f = {s, sent < rn ? CP_OUT : CP_IN, 0};
        if (cplat_poll(&f, 1, (int)(until - cplat_now_ms())) <= 0) continue;
        if (sent < rn) {
            if (!ok) { if (cplat_tcp_connected(s) != 1) break; ok = 1; }
            int64_t k = cplat_send(s, req + sent, rn - sent); if (k == -2) break; if (k > 0) sent += (size_t)k;
            continue;
        }
        int64_t k = cplat_recv(s, buf + got, 65535 - got);
        if (k == -1) continue;
        if (k <= 0) break;   // closed (done) or an error
        got += (size_t)k; if (got >= 65535) break;
    }
    cplat_close(s);
    buf[got] = 0;
    int status = -1;
    if (got) {
        status = -2;
        unsigned st;
        if (sscanf(buf, "HTTP/%*u.%*u %3u", &st) == 1) status = (int)st;
        const char *body = strstr(buf, "\r\n\r\n");
        if (status > 0 && body) {
            body += 4; size_t o = 0;
            const char *te = ci_find(buf, "transfer-encoding:");
            if (te && te < body && ci_find(te, "chunked") && ci_find(te, "chunked") < body) {   // chunk sizes in hex, each followed by its bytes
                const char *p = body;
                for (;;) {
                    unsigned long len = strtoul(p, NULL, 16); const char *d = strstr(p, "\r\n");
                    if (!d || !len) break;
                    d += 2; if ((size_t)(buf + got - d) < len) len = (unsigned long)(buf + got - d);
                    size_t take = o + len < cap ? len : cap - 1 - o; memcpy(out + o, d, take); o += take;
                    p = d + len; if (*p == '\r') p += 2;
                    if (o + 1 >= cap) break;
                }
            } else { size_t len = strlen(body); if (len >= cap) len = cap - 1; memcpy(out, body, len); o = len; }
            out[o] = 0;
        } else if (cap) out[0] = 0;
    }
    free(buf);
    return status;
}
// <tag>text</tag> (any namespace prefix, any case), the first one at or after s: the text, or -1
int cnat_xml_text(const char *s, const char *tag, char *out, size_t n) {
    size_t tl = strlen(tag);
    for (const char *p = s; (p = strchr(p, '<')); p++) {
        const char *q = p + 1; if (*q == '/') continue;
        const char *colon = q; while (*colon && *colon != '>' && *colon != ' ' && *colon != ':') colon++;
        const char *name = *colon == ':' ? colon + 1 : q;
        if (!ci_starts(name, tag) || (name[tl] != '>' && name[tl] != ' ')) continue;
        const char *b = strchr(name, '>'); if (!b) return -1; b++;
        const char *e = strchr(b, '<'); if (!e) return -1;
        size_t k = (size_t)(e - b); if (k >= n) k = n - 1;
        memcpy(out, b, k); out[k] = 0;
        while (k && (out[k - 1] == ' ' || out[k - 1] == '\r' || out[k - 1] == '\n' || out[k - 1] == '\t')) out[--k] = 0;
        size_t lead = 0; while (out[lead] == ' ' || out[lead] == '\r' || out[lead] == '\n' || out[lead] == '\t') lead++;
        if (lead) memmove(out, out + lead, k - lead + 1);
        return 0;
    }
    return -1;
}
// The device description: the WANIPConnection (else WANPPPConnection) service's type and control URL, made absolute from the description's own URL
// (or its URLBase): 0, or -1 when it has neither service
int cnat_igd_control(const char *xml, const char *desc_url, char *service, size_t sn, char *control, size_t cn) {
    static const char *const types[] = {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
                                         "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    for (int t = 0; t < 3; t++) {
        const char *p = strstr(xml, types[t]); if (!p) continue;
        char rel[256]; if (cnat_xml_text(p, "controlURL", rel, sizeof rel)) continue;
        snprintf(service, sn, "%s", types[t]);
        if (!strncmp(rel, "http://", 7)) { snprintf(control, cn, "%s", rel); return 0; }
        char base[256]; if (cnat_xml_text(xml, "URLBase", base, sizeof base)) snprintf(base, sizeof base, "%s", desc_url);
        uint32_t ip; uint16_t port; char path[200];
        if (cnat_url(base, &ip, &port, path, sizeof path)) return -1;
        char host[32]; ipstr(host, sizeof host, ip);
        snprintf(control, cn, "http://%s:%u%s%s", host, port, rel[0] == '/' ? "" : "/", rel);
        return 0;
    }
    return -1;
}
// A SOAP action: the HTTP status (200 done), the response's body in out; a fault's UPnP errorCode in *err (else 0)
static int soap(const char *control, const char *service, const char *action, const char *args, char *out, size_t cap, int *err) {
    uint32_t ip; uint16_t port; char path[200]; *err = 0;
    if (cnat_url(control, &ip, &port, path, sizeof path)) return -2;
    char *req = malloc(4096); if (!req) return -1;
    char body[2048], host[32]; ipstr(host, sizeof host, ip);
    int bl = snprintf(body, sizeof body, "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
                      action, service, args, action);
    int rl = snprintf(req, 4096, "POST %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\n"
                      "Content-Length: %d\r\nConnection: close\r\n\r\n%s", path, host, port, service, action, bl, body);
    int st = http(ip, port, req, (size_t)rl, out, cap, 4000);
    free(req);
    char code[16]; if (st != 200 && !cnat_xml_text(out, "errorCode", code, sizeof code)) *err = atoi(code);
    return st;
}

// ---------- the worker: map, renew, give back ----------
static void nat_say(coop_node *n, int state, int method, int problem, uint32_t ext, uint16_t port, int lease, const char *log) {
    cnode_lock(n);
    n->nat.info.state = state; n->nat.info.method = method; n->nat.info.problem = problem;
    if (ext) n->nat.info.external = ext;
    n->nat.info.port = port; n->nat.info.lease_s = lease;
    cnode_unlock(n);
    if (log) coop_logf(n, "nat: %s", log);
}
static int ext_problem(uint32_t ext) {   // what the router's own internet address says about the path beyond it
    if (!ext) return COOP_NATP_NO_SUPPORT;
    if ((ext >> 22) == 0x191) return COOP_NATP_CGNAT;                           // 100.64/10
    int c = coop_addr4_class(ext);
    return c == COOP_ADDR_PRIVATE || c == COOP_ADDR_LINKLOCAL ? COOP_NATP_DOUBLE_NAT : COOP_NATP_NONE;
}
static int map_pmp(natmap *m, int pcp, uint32_t life, uint32_t *life_out) {   // UDP and TCP; 0 both mapped
    uint16_t e[2] = {0, 0}; uint32_t l[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        int r = pcp ? cnat_pcp_map(m->gw, m->gport, m->client, m->nonce[i], i ? 6 : 17, m->port, m->port, life, &m->ext, &e[i], &l[i], 3)
                    : cnat_pmp(m->gw, m->gport, i ? 2 : 1, m->port, m->port, life, NULL, &e[i], &l[i], 3);
        if (r) return r;
    }
    m->eport = e[0]; if (life_out) *life_out = l[0] < l[1] ? l[0] : l[1];
    return 0;
}
static int map_upnp(natmap *m, uint32_t life, int *err) {   // TCP and UDP; 0 both mapped, else the UPnP error code (or -1 no answer)
    char args[700], local[32], out[4096]; ipstr(local, sizeof local, m->client);
    static const char *const pr[2] = {"TCP", "UDP"};
    for (int i = 0; i < 2; i++) {
        snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol><NewInternalPort>%u"
                 "</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" NAT_DESC
                 "</NewPortMappingDescription><NewLeaseDuration>%u</NewLeaseDuration>", m->port, pr[i], m->port, local, life);
        int st = soap(m->control, m->service, "AddPortMapping", args, out, sizeof out, err);
        if (st != 200) return *err ? *err : -1;
    }
    m->eport = m->port;
    return 0;
}
static void unmap(coop_node *n, natmap *m) {
    if (m->method == COOP_NATM_NATPMP || m->method == COOP_NATM_PCP) map_pmp(m, m->method == COOP_NATM_PCP, 0, NULL);
    else if (m->method == COOP_NATM_UPNP) {
        char args[300], out[2048]; int err;
        static const char *const pr[2] = {"TCP", "UDP"};
        for (int i = 0; i < 2; i++) {
            snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>", m->eport, pr[i]);
            soap(m->control, m->service, "DeletePortMapping", args, out, sizeof out, &err);
        }
    }
    if (m->method) coop_logf(n, "nat: port %u given back to the router", m->eport);
    m->method = COOP_NATM_NONE;
}
// One attempt to forward port: NAT-PMP, then PCP, then UPnP. Returns the lease in seconds (0 none: info says why)
static uint32_t map_port(coop_node *n, natmap *m, uint16_t port) {
    char t[220], a[24];
    m->port = port; m->ext = 0; m->method = COOP_NATM_NONE;
    cnode_lock(n); m->gw = n->cfg.nat_gw; m->gport = n->cfg.nat_gport ? n->cfg.nat_gport : PMP_PORT; char loc[256]; snprintf(loc, sizeof loc, "%s", n->cfg.nat_location); cnode_unlock(n);
    if (!m->gw && cplat_gateway(&m->gw)) m->gw = 0;
    if (cplat_route_ip(&m->client)) m->client = 0;
    if (n->cfg.bind_loopback) m->client = 0x7F000001u;
    nat_say(n, COOP_NAT_TRYING, 0, 0, 0, port, 0, NULL);
    cnode_lock(n); n->nat.info.gateway = m->gw; cnode_unlock(n);
    uint32_t life = 0; int r = -1;
    if (m->gw) {
        r = cnat_pmp(m->gw, m->gport, 0, 0, 0, 0, &m->ext, NULL, NULL, 3);
        if (r == 0 && !(r = map_pmp(m, 0, NAT_LEASE, &life))) m->method = COOP_NATM_NATPMP;
        else if (r == 1 || r == -1 || r == -2) {   // PCP instead (an answer in PCP, no answer, or a NAT-PMP that refuses the version)
            cplat_random(m->nonce, sizeof m->nonce);
            int p = map_pmp(m, 1, NAT_LEASE, &life);
            if (!p) m->method = COOP_NATM_PCP; else if (p > 0 && p != 1) r = p;
        }
    }
    int uerr = 0;
    if (!m->method && strcmp(loc, "-")) {   // UPnP: SSDP (or the location given for tests; "-" none), the description, the router's address, the two mappings
        char xml[16384];
        uint32_t lip; uint16_t lp; char path[200], host[32];
        if ((loc[0] || !ssdp(m->gw, loc, sizeof loc, 2500)) && !cnat_url(loc, &lip, &lp, path, sizeof path)) {
            ipstr(host, sizeof host, lip);
            char req[400]; int rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", path, host, lp);
            if (http(lip, lp, req, (size_t)rl, xml, sizeof xml, 3000) == 200 && !cnat_igd_control(xml, loc, m->service, sizeof m->service, m->control, sizeof m->control)) {
                char out[2048], ip[40]; int err;
                if (soap(m->control, m->service, "GetExternalIPAddress", "", out, sizeof out, &err) == 200 && !cnat_xml_text(out, "NewExternalIPAddress", ip, sizeof ip)) {
                    unsigned b[4]; if (sscanf(ip, "%3u.%3u.%3u.%3u", &b[0], &b[1], &b[2], &b[3]) == 4) m->ext = b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
                }
                int u = map_upnp(m, NAT_LEASE, &uerr), perm = 0;
                if (u == 725) { perm = 1; u = map_upnp(m, 0, &uerr); }   // OnlyPermanentLeasesSupported: no lease; given back when hosting ends
                if (!u) { m->method = COOP_NATM_UPNP; life = perm ? 24 * 3600 : NAT_LEASE; }   // a permanent one is added again daily (harmless)
                else uerr = u;
            }
        }
    }
    ipstr(a, sizeof a, m->ext);
    static const char *const how[4] = {"", "NAT-PMP", "PCP", "UPnP"};
    if (m->method) {
        int pb = ext_problem(m->ext);
        if (pb == COOP_NATP_NO_SUPPORT) pb = COOP_NATP_NONE;   // mapped, the address just not told
        snprintf(t, sizeof t, "port %u (TCP and UDP) forwarded here by the router (%s, lease %u s); its internet address %s%s", m->eport, how[m->method], life,
                 m->ext ? a : "not told", pb == COOP_NATP_CGNAT ? ": carrier-grade NAT, visitors from the internet cannot reach it" :
                 pb == COOP_NATP_DOUBLE_NAT ? ": another router is in front of it" : "");
        nat_say(n, COOP_NAT_MAPPED, m->method, pb, m->ext, m->eport, (int)life, t);
        return life ? life : 1;
    }
    int pb = !m->gw && !loc[0] ? COOP_NATP_NO_GATEWAY : uerr == 718 ? COOP_NATP_CONFLICT : (uerr > 0 || r == 2) ? COOP_NATP_REFUSED : COOP_NATP_NO_SUPPORT;
    if (pb == COOP_NATP_NO_SUPPORT && m->ext) pb = ext_problem(m->ext);
    snprintf(t, sizeof t, "port %u not forwarded: %s", port, pb == COOP_NATP_NO_GATEWAY ? "no router found" : pb == COOP_NATP_CONFLICT ?
             "the router has it forwarded to another device (UPnP 718)" : pb == COOP_NATP_REFUSED ? "the router refused" : "the router answers neither NAT-PMP, PCP nor UPnP");
    nat_say(n, COOP_NAT_FAILED, 0, pb, m->ext, port, 0, t);
    return 0;
}
static void nat_main(void *arg) {
    coop_node *n = arg; natmap m; memset(&m, 0, sizeof m);
    uint16_t have = 0; int64_t renew = 0; int tried = 0;
    for (;;) {
        cnode_lock(n); uint16_t want = n->nat.want; int stop = n->nat.stop; cnode_unlock(n);
        if (stop) break;
        if (want != have || (want && !m.method && !tried)) {
            if (m.method) unmap(n, &m);
            have = 0; tried = 0;
            if (want) {
                uint32_t life = map_port(n, &m, want); tried = 1; have = want;
                renew = life ? cplat_now_ms() + (int64_t)life * 500 : 0;
            } else nat_say(n, COOP_NAT_OFF, 0, 0, 0, 0, 0, NULL);
        } else if (m.method && renew && cplat_now_ms() >= renew) {
            uint32_t life = map_port(n, &m, want); renew = life ? cplat_now_ms() + (int64_t)life * 500 : 0;
        }
        cplat_sleep_ns(100000000);
    }
    cnode_lock(n); int busy_quit = n->nat.quitting; cnode_unlock(n);
    if (m.method && !busy_quit) unmap(n, &m);
    cnode_lock(n); n->nat.m_left = m; cnode_unlock(n);
}

// ---------- the node's side ----------
void cnat_want(coop_node *n, uint16_t port) {   // with the lock held: hosting over the internet starts (port) or ends (0)
    if (!n->cfg.nat) { n->nat.info.state = COOP_NAT_OFF; return; }
    n->nat.want = port;
    if (!n->nat.running && port && !cplat_thread_start(&n->nat.th, nat_main, n)) n->nat.running = 1;
}
void cnat_stop(coop_node *n) {   // without the lock: the worker gives its mapping back and ends (coop_destroy)
    cnode_lock(n); int run = n->nat.running; n->nat.stop = 1; n->nat.running = 0; cnode_unlock(n);
    if (run) cplat_thread_join(n->nat.th);
}
void coop_nat_quit(coop_node *n) {   // the game quits: the worker's mapping given back from here at once, without waiting for the worker
    if (!n) return;
    cnode_lock(n); n->nat.stop = 1; n->nat.quitting = 1; int mapped = n->nat.info.state == COOP_NAT_MAPPED; cnode_unlock(n);
    if (!mapped) return;
    // the worker keeps its mapping in a local; it checks stop every 100 ms and leaves the mapping to us: wait for its copy a little
    for (int i = 0; i < 20; i++) { cnode_lock(n); int left = n->nat.m_left.method != 0; cnode_unlock(n); if (left) break; cplat_sleep_ns(50000000); }
    cnode_lock(n); natmap m = n->nat.m_left; n->nat.m_left.method = 0; cnode_unlock(n);
    if (m.method) unmap(n, &m);
}
