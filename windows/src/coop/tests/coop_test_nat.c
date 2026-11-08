// coop unit tests, automatic port forwarding (MIT; coop_nat.c, docs/coop-nat.md). Included by coop_test.c after coop_test_inet.c. Section nat:
// - the pieces: URLs, an SSDP answer's LOCATION, a tag's text (any prefix, spaces), the IGD control URL (relative, absolute, URLBase);
// - NAT-PMP and PCP against small routers on loopback: the address, a mapping, its deletion, a PCP-only router answering NAT-PMP;
// - a host node over the internet (loopback-bound, its router given) forwarding its port while it hosts and giving it back after: through UPnP (a
//   chunked description, the router's address, TCP and UDP), UPnP routers that only take permanent leases (725) or have the port taken (718), a
//   router behind carrier-grade NAT and one behind a second router, NAT-PMP, PCP, and no router answering at all.

static void put32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// ---------- a NAT-PMP / PCP router ----------
typedef struct { csock s; uint16_t port; atomic_int run, maps, dels, pcp_maps, pcp_dels; int pcp_only, silent; cthread th; } fake_pmp;
static void pmp_main(void *arg) {
    fake_pmp *r = arg; uint8_t b[1100], o[64];
    while (atomic_load(&r->run)) {
        uint32_t ip; uint16_t pt; int64_t k = cplat_recvfrom(r->s, b, sizeof b, &ip, &pt);
        if (k < 2) { msleep(2); continue; }
        if (r->silent) continue;
        memset(o, 0, sizeof o);
        if (b[0] == 0 && r->pcp_only) { o[0] = 2; o[1] = (uint8_t)(0x80 | b[1]); o[3] = 1; cplat_sendto(r->s, o, 24, ip, pt); continue; }   // UNSUPP_VERSION, in PCP
        if (b[0] == 0 && b[1] == 0) { o[1] = 128; put32be(o + 4, 1234); put32be(o + 8, 0xCB007107u); cplat_sendto(r->s, o, 12, ip, pt); continue; }   // 203.0.113.7
        if (b[0] == 0 && (b[1] == 1 || b[1] == 2) && k >= 12) {
            uint32_t life = get32be(b + 8); atomic_fetch_add(life ? &r->maps : &r->dels, 1);
            o[1] = (uint8_t)(128 + b[1]); put32be(o + 4, 1234); memcpy(o + 8, b + 4, 2); memcpy(o + 10, b + 4, 2); put32be(o + 12, life > 7200 ? 7200 : life);
            cplat_sendto(r->s, o, 16, ip, pt); continue;
        }
        if (b[0] == 2 && b[1] == 1 && k >= 60) {   // PCP MAP: the nonce, protocol and ports back, the external address 203.0.113.8
            uint32_t life = get32be(b + 4); atomic_fetch_add(life ? &r->pcp_maps : &r->pcp_dels, 1);
            uint8_t p[60] = {0}; p[0] = 2; p[1] = 0x81; put32be(p + 4, life); put32be(p + 8, 1234);
            memcpy(p + 24, b + 24, 12); p[36] = b[36]; memcpy(p + 40, b + 40, 2); memcpy(p + 42, b + 40, 2); p[54] = 0xFF; p[55] = 0xFF; put32be(p + 56, 0xCB007108u);
            cplat_sendto(r->s, p, 60, ip, pt);
        }
    }
}
static void pmp_start(fake_pmp *r, int pcp_only, int silent) {
    memset(r, 0, sizeof *r); r->pcp_only = pcp_only; r->silent = silent;
    r->s = cplat_udp_open(0x7F000001u, 0, &r->port); CHECK(r->s != CSOCK_BAD);
    atomic_store(&r->run, 1); cplat_thread_start(&r->th, pmp_main, r);
}
static void pmp_stop(fake_pmp *r) { atomic_store(&r->run, 0); cplat_thread_join(r->th); cplat_close(r->s); }

// ---------- a UPnP IGD: the description (sent chunked) and the WANIPConnection:1 control URL ----------
typedef struct { csock l; uint16_t port; atomic_int run, adds, dels, gets, faults; char ext[32]; int err_add, only_permanent; char last_add[2048]; cthread th; } fake_igd;
static const char k_desc[] = "<?xml version=\"1.0\"?>\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><deviceType>urn:schemas-upnp-org:device:"
    "InternetGatewayDevice:1</deviceType><deviceList><device><deviceList><device><serviceList><service>\n  <serviceType>urn:schemas-upnp-org:service:"
    "WANIPConnection:1</serviceType>\n  <serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>\n  <controlURL> /ctl/IPConn </controlURL>\n"
    "</service></serviceList></device></deviceList></device></deviceList></device></root>\n";
static void igd_send(csock c, const char *t) {
    size_t n = strlen(t), o = 0; int64_t until = now_ms() + 2000;
    while (o < n && now_ms() < until) { int64_t k = cplat_send(c, t + o, n - o); if (k > 0) o += (size_t)k; else if (k == -2) break; else msleep(1); }
}
static void igd_main(void *arg) {
    fake_igd *g = arg;
    while (atomic_load(&g->run)) {
        uint32_t ip; uint16_t pt; csock c = cplat_tcp_accept(g->l, &ip, &pt);
        if (c == CSOCK_BAD) { msleep(2); continue; }
        char req[8192]; size_t n = 0; int64_t until = now_ms() + 2000;
        while (now_ms() < until && n < sizeof req - 1) {
            cpollfd f = {c, CP_IN, 0}; if (cplat_poll(&f, 1, 50) <= 0) continue;
            int64_t k = cplat_recv(c, req + n, sizeof req - 1 - n); if (k == -1) continue; if (k <= 0) break;
            n += (size_t)k; req[n] = 0;
            char *h = strstr(req, "\r\n\r\n"); if (!h) continue;
            char *cl = strstr(req, "Content-Length:"); size_t need = cl ? strtoul(cl + 15, NULL, 10) : 0;
            if (n >= (size_t)(h + 4 - req) + need) break;
        }
        req[n] = 0;
        char body[3000] = "", resp[4000]; int status = 200;
        static const char env0[] = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>", env1[] = "</s:Body></s:Envelope>";
        if (!strncmp(req, "GET /rootDesc.xml ", 18)) {   // chunked, in two pieces
            size_t a = strlen(k_desc) / 2;
            snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%.*s\r\n%zx\r\n%s\r\n0\r\n\r\n",
                     a, (int)a, k_desc, strlen(k_desc) - a, k_desc + a);
            igd_send(c, resp); cplat_close(c); continue;
        }
        int err = 0;
        if (!strstr(req, "POST /ctl/IPConn ")) status = 404;
        else if (strstr(req, "WANIPConnection:1#GetExternalIPAddress")) {
            atomic_fetch_add(&g->gets, 1);
            snprintf(body, sizeof body, "%s<u:GetExternalIPAddressResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\"><NewExternalIPAddress>%s"
                     "</NewExternalIPAddress></u:GetExternalIPAddressResponse>%s", env0, g->ext, env1);
        } else if (strstr(req, "WANIPConnection:1#AddPortMapping")) {
            if (g->err_add) err = g->err_add;
            else if (g->only_permanent && !strstr(req, "<NewLeaseDuration>0</NewLeaseDuration>")) err = 725;
            else { atomic_fetch_add(&g->adds, 1); snprintf(g->last_add, sizeof g->last_add, "%s", strstr(req, "\r\n\r\n")); snprintf(body, sizeof body, "%s%s", env0, env1); }
        } else if (strstr(req, "WANIPConnection:1#DeletePortMapping")) { atomic_fetch_add(&g->dels, 1); snprintf(body, sizeof body, "%s%s", env0, env1); }
        else status = 404;
        if (err) {
            status = 500; atomic_fetch_add(&g->faults, 1);
            snprintf(body, sizeof body, "%s<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail><UPnPError xmlns=\"urn:schemas-"
                     "upnp-org:control-1-0\"><errorCode>%d</errorCode><errorDescription>x</errorDescription></UPnPError></detail></s:Fault>%s", env0, err, env1);
        }
        snprintf(resp, sizeof resp, "HTTP/1.1 %d %s\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                 status, status == 200 ? "OK" : "Error", strlen(body), body);
        igd_send(c, resp); cplat_close(c);
    }
}
static void igd_start(fake_igd *g, const char *ext) {
    memset(g, 0, sizeof *g); snprintf(g->ext, sizeof g->ext, "%s", ext);
    g->l = cplat_tcp_listen(0x7F000001u, 0, &g->port); CHECK(g->l != CSOCK_BAD);
    atomic_store(&g->run, 1); cplat_thread_start(&g->th, igd_main, g);
}
static void igd_stop(fake_igd *g) { atomic_store(&g->run, 0); cplat_thread_join(g->th); cplat_close(g->l); }

// ---------- a host over the internet with its router given: hosts, waits for the forward, ends hosting ----------
static uint16_t closed_udp_port(void) { uint16_t p = 0; csock s = cplat_udp_open(0x7F000001u, 0, &p); cplat_close(s); return p; }
static coop_node *nat_host(uint16_t pmp_port, const fake_igd *g, coop_info *i, uint16_t *port) {
    coop_cfg c; char err[240]; static tnode t = {.name = "nat"};
    CHECK_EQ(coop_cfg_parse_ex(&c, "auto", NULL, "1", ROOM, NULL, err, sizeof err), 1);
    c.hooks = hooks(&t); c.bind_loopback = 1; c.discovery = 0; c.nat_gw = 0x7F000001u; c.nat_gport = pmp_port;
    if (g) snprintf(c.nat_location, sizeof c.nat_location, "http://127.0.0.1:%u/rootDesc.xml", g->port);
    else snprintf(c.nat_location, sizeof c.nat_location, "-");   // never SSDP from a test: a real router could answer
    coop_node *n = coop_create(&c);
    uint8_t cfg[0x20], sec[0x44], uh[0x30]; mk_netcfg(cfg, 4, 301); mk_sec(sec, 3); mk_user(uh, "Host");
    CHECK_EQ(coop_ldn_initialize(n), 0); CHECK_EQ(coop_ldn_open_ap(n), 0); CHECK_EQ(coop_ldn_create_network(n, cfg, sec, uh), 0);
    *port = coop_port(n);
    WAIT_UNTIL((coop_get_info(n, i), i->nat.state == COOP_NAT_MAPPED || i->nat.state == COOP_NAT_FAILED), 20000);
    return n;
}
static void nat_end(coop_node *n) { CHECK_EQ(coop_ldn_destroy_network(n), 0); }

static void test_nat(void) {
    fprintf(stderr, "== nat: NAT-PMP, PCP and UPnP IGD against routers on loopback\n");
    uint32_t ip; uint16_t port; char path[200], t[256], svc[100], ctl[256];
    // the pieces
    CHECK_EQ(cnat_url("http://192.168.1.1:5000/rootDesc.xml", &ip, &port, path, sizeof path), 0); CHECK_EQ(ip, 0xC0A80101u); CHECK_EQ(port, 5000); CHECK(!strcmp(path, "/rootDesc.xml"));
    CHECK_EQ(cnat_url("http://10.0.0.138", &ip, &port, path, sizeof path), 0); CHECK_EQ(port, 80); CHECK(!strcmp(path, "/"));
    CHECK_EQ(cnat_url("http://router.local:80/x", &ip, &port, path, sizeof path), -1); CHECK_EQ(cnat_url("https://1.2.3.4/x", &ip, &port, path, sizeof path), -1);
    CHECK_EQ(cnat_url("http://1.2.3.4:0/x", &ip, &port, path, sizeof path), -1); CHECK_EQ(cnat_url("http://1.2.3.400/x", &ip, &port, path, sizeof path), -1);
    CHECK_EQ(cnat_ssdp_location("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nLocation:  http://192.168.1.1:1900/igd.xml\r\nST: x\r\n\r\n", t, sizeof t), 0);
    CHECK(!strcmp(t, "http://192.168.1.1:1900/igd.xml"));
    CHECK_EQ(cnat_ssdp_location("HTTP/1.1 200 OK\r\nST: x\r\n\r\n", t, sizeof t), -1);
    CHECK_EQ(cnat_xml_text("<s:Body><u:R><NewExternalIPAddress>\n 203.0.113.7 </NewExternalIPAddress></u:R>", "NewExternalIPAddress", t, sizeof t), 0); CHECK(!strcmp(t, "203.0.113.7"));
    CHECK_EQ(cnat_xml_text("<a><errorCode>718</errorCode>", "errorcode", t, sizeof t), 0); CHECK(!strcmp(t, "718"));
    CHECK_EQ(cnat_xml_text("<a><errorCodes>1</errorCodes>", "errorCode", t, sizeof t), -1);
    CHECK_EQ(cnat_igd_control(k_desc, "http://192.168.1.1:5000/rootDesc.xml", svc, sizeof svc, ctl, sizeof ctl), 0);
    CHECK(!strcmp(svc, "urn:schemas-upnp-org:service:WANIPConnection:1")); CHECK(!strcmp(ctl, "http://192.168.1.1:5000/ctl/IPConn"));
    CHECK_EQ(cnat_igd_control("<URLBase>http://10.0.0.1:49000</URLBase><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType><controlURL>upnp/control/ppp</controlURL>",
                              "http://10.0.0.1:1900/d.xml", svc, sizeof svc, ctl, sizeof ctl), 0);
    CHECK(!strcmp(ctl, "http://10.0.0.1:49000/upnp/control/ppp")); CHECK(strstr(svc, "WANPPPConnection") != NULL);
    CHECK_EQ(cnat_igd_control("<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>http://10.0.0.1:2/c</controlURL>", "http://x", svc, sizeof svc, ctl, sizeof ctl), 0);
    CHECK(!strcmp(ctl, "http://10.0.0.1:2/c"));
    CHECK_EQ(cnat_igd_control("<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>", "http://10.0.0.1/", svc, sizeof svc, ctl, sizeof ctl), -1);

    // NAT-PMP and PCP, directly
    fake_pmp r; pmp_start(&r, 0, 0);
    uint32_t ext = 0, life = 0; uint16_t mp = 0;
    CHECK_EQ(cnat_pmp(0x7F000001u, r.port, 0, 0, 0, 0, &ext, NULL, NULL, 3), 0); CHECK_EQ(ext, 0xCB007107u);
    CHECK_EQ(cnat_pmp(0x7F000001u, r.port, 1, 47600, 47600, 3600, NULL, &mp, &life, 3), 0); CHECK_EQ(mp, 47600); CHECK_EQ(life, 3600); CHECK_EQ(atomic_load(&r.maps), 1);
    CHECK_EQ(cnat_pmp(0x7F000001u, r.port, 2, 47600, 47600, 0, NULL, &mp, &life, 3), 0); CHECK_EQ(atomic_load(&r.dels), 1);
    uint8_t nonce[12]; cplat_random(nonce, sizeof nonce);
    CHECK_EQ(cnat_pcp_map(0x7F000001u, r.port, 0x7F000001u, nonce, 17, 47600, 47600, 3600, &ext, &mp, &life, 3), 0); CHECK_EQ(ext, 0xCB007108u); CHECK_EQ(mp, 47600);
    CHECK_EQ(cnat_pmp(0x7F000002u, r.port, 0, 0, 0, 0, &ext, NULL, NULL, 1), -1);   // an answer from another address is not the router's
    pmp_stop(&r);
    pmp_start(&r, 1, 0);
    CHECK_EQ(cnat_pmp(0x7F000001u, r.port, 0, 0, 0, 0, &ext, NULL, NULL, 3), 1);   // a PCP-only router
    pmp_stop(&r);

    // a host forwarding its port while it hosts: UPnP (no NAT-PMP answer first)
    fake_igd g; coop_info i; uint16_t hp; uint16_t shut = closed_udp_port();
    igd_start(&g, "203.0.113.9");
    coop_node *n = nat_host(shut, &g, &i, &hp);
    CHECK_EQ(i.nat.state, COOP_NAT_MAPPED); CHECK_EQ(i.nat.method, COOP_NATM_UPNP); CHECK_EQ(i.nat.problem, COOP_NATP_NONE); CHECK_EQ(i.nat.external, 0xCB007109u);
    CHECK_EQ(i.nat.port, hp); CHECK_EQ(atomic_load(&g.adds), 2); CHECK_EQ(atomic_load(&g.gets), 1);
    CHECK(strstr(g.last_add, "<NewInternalClient>127.0.0.1</NewInternalClient>") && strstr(g.last_add, "<NewPortMappingDescription>Pokopia Native<"));
    nat_end(n);
    WAIT_UNTIL(atomic_load(&g.dels) == 2, 10000); CHECK_EQ(atomic_load(&g.dels), 2);   // given back when hosting ends
    WAIT_UNTIL((coop_get_info(n, &i), i.nat.state == COOP_NAT_OFF), 5000); CHECK_EQ(i.nat.state, COOP_NAT_OFF);
    coop_destroy(n); igd_stop(&g);
    // a router that takes permanent mappings only (725): mapped without a lease
    igd_start(&g, "203.0.113.9"); g.only_permanent = 1;
    n = nat_host(shut, &g, &i, &hp); CHECK_EQ(i.nat.state, COOP_NAT_MAPPED); CHECK(atomic_load(&g.faults) >= 1); CHECK_EQ(atomic_load(&g.adds), 2);
    coop_destroy(n); WAIT_UNTIL(atomic_load(&g.dels) == 2, 10000); CHECK_EQ(atomic_load(&g.dels), 2); igd_stop(&g);   // given back at the node's end too
    // the port forwarded to another device (718)
    igd_start(&g, "203.0.113.9"); g.err_add = 718;
    n = nat_host(shut, &g, &i, &hp); CHECK_EQ(i.nat.state, COOP_NAT_FAILED); CHECK_EQ(i.nat.problem, COOP_NATP_CONFLICT);
    coop_destroy(n); igd_stop(&g);
    // carrier-grade NAT and a second router: mapped, and the reason it won't help
    igd_start(&g, "100.64.1.2"); n = nat_host(shut, &g, &i, &hp); CHECK_EQ(i.nat.state, COOP_NAT_MAPPED); CHECK_EQ(i.nat.problem, COOP_NATP_CGNAT); coop_destroy(n); igd_stop(&g);
    igd_start(&g, "192.168.0.2"); n = nat_host(shut, &g, &i, &hp); CHECK_EQ(i.nat.problem, COOP_NATP_DOUBLE_NAT); coop_destroy(n); igd_stop(&g);
    // NAT-PMP, then PCP, then nobody
    pmp_start(&r, 0, 0);
    n = nat_host(r.port, NULL, &i, &hp); CHECK_EQ(i.nat.state, COOP_NAT_MAPPED); CHECK_EQ(i.nat.method, COOP_NATM_NATPMP); CHECK_EQ(i.nat.external, 0xCB007107u);
    CHECK_EQ(atomic_load(&r.maps), 2);
    nat_end(n); WAIT_UNTIL(atomic_load(&r.dels) == 2, 10000); CHECK_EQ(atomic_load(&r.dels), 2);
    coop_destroy(n); pmp_stop(&r);
    pmp_start(&r, 1, 0);
    n = nat_host(r.port, NULL, &i, &hp); CHECK_EQ(i.nat.method, COOP_NATM_PCP); CHECK_EQ(i.nat.external, 0xCB007108u); CHECK_EQ(atomic_load(&r.pcp_maps), 2);
    coop_nat_quit(n); CHECK_EQ(atomic_load(&r.pcp_dels), 2);   // the game quits: given back at once
    coop_destroy(n); pmp_stop(&r);
    n = nat_host(shut, NULL, &i, &hp); CHECK_EQ(i.nat.state, COOP_NAT_FAILED); CHECK_EQ(i.nat.problem, COOP_NATP_NO_SUPPORT);
    coop_destroy(n);
}
