#!/usr/bin/env python3
"""Link Play co-op in nxnative (docs/coop-step1c.md, docs/coop-step1d.md): applies this module and its product glue to an nxnative checkout that carries
pokopia-native's Switch 2 patch, for pokopia-native's host/nxnative-switch2.patch (make_patch.py turns the result into that patch). MIT, our own code.
    python3 apply_coop.py <nxnative checkout>
Idempotent, with exact anchors; refuses a tree it does not recognise. What it does:
- copies the module in as dm/host/coop.h, coop_int.h and hle_coop_{ldn,vnet,mpnet,plat,disc,auth,interact}.c (the host Makefile's hle_*.c wildcard; the
  iOS build takes its sources from that Makefile);
- dm/host/dm.h: observation gate OBS_COOP_INTERACT, "Interact everywhere" (docs/coop-interact.md): the profile's patches.py places it, ldn_glue.c attaches
  the shared callback;
- hle_offline.c: the ldn section routes to the module while DM_COOP is set and not off (ldn_glue.c), with main's build ID and the cheats that are on as
  the compatibility identity (with "Interact everywhere" when it is on); the socket overloads go through hle_net.c's mpsk_* only then, so the offline answers keep their exact form;
- hle_net.c: the mpsk_* routing block (sock_glue.c), Poll / Select that know virtual descriptors (sock_poll.c), the socket hooks through mpsk_*;
- hle_misc.c: parental controls answer "allowed" only while co-op is on;
- dm/games/settings.tsv: the launcher's Link Play switch (DM_COOP auto / off, off by default), on the iPhone and the Mac; under it, a host to join
  by address (DM_COOP_JOIN), internet play (DM_COOP_INTERNET, off), the room code (DM_COOP_ROOM) and the relay address (DM_COOP_RELAY), the three
  texts empty by default (step 1d), and "Interact everywhere" (DM_COOP_INTERACT, off);
- the launcher's text kind for those three (launcher.json schema 2): dm/games/settings.py, docs/launcher.schema.json, docs/frontends.md, the shared
  settings row and model (dm/ios/Sources/LauncherRow.swift, LauncherSettings.swift), the Mac launcher's saved-choice check (dm/mac/launcher.c,
  pack_app.py's DMSettings) and dm/tools/tests/test_launcher_settings.py;
- docs/env_switches.tsv: rows for DM_COOP, DM_COOP_DISCOVERY, DM_COOP_INTERACT, DM_COOP_INTERNET, DM_COOP_JOIN, DM_COOP_RELAY, DM_COOP_ROOM, DM_COOP_TCP and DM_UID;
- dm/ios/project.yml and dm/mac/pack_app.py: NSLocalNetworkUsageDescription and NSBonjourServices (_pokopia-ldn._udp)."""
import pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
MOD = HERE.parent
root = pathlib.Path(sys.argv[1]).expanduser().resolve()
host = root / 'dm/host'
if not (host / 'hle_offline.c').is_file() or not (root / 'dm/gfx/nvn/.switch2-patch').is_file():
    sys.exit(f'{root}: not an nxnative checkout with the Switch 2 patch applied')
REASON = "Link Play uses your local network to find and visit other players' towns."
SERVICE = '_pokopia-ldn._udp'


def put(dst, data):
    if not dst.exists() or dst.read_bytes() != data:
        dst.write_bytes(data)


for f in ('coop.h', 'coop_int.h'):
    put(host / f, (MOD / f).read_bytes())
for f in ('coop_ldn.c', 'coop_vnet.c', 'coop_mpnet.c', 'coop_plat.c', 'coop_disc.c', 'coop_auth.c', 'coop_interact.c'):
    put(host / ('hle_' + f), (MOD / f).read_bytes())


def text(p): return p.read_text(encoding='utf-8')
def write(p, s):
    if text(p) != s: p.write_bytes(s.encode('utf-8'))


def edit(path, old, new):
    s = text(path)
    if new in s: return
    if s.count(old) != 1: sys.exit(f'{path.name}: anchor not found exactly once: {old[:70]!r}')
    write(path, s.replace(old, new))


def once(lines, pred, what):
    hit = [i for i, l in enumerate(lines) if pred(l)]
    if len(hit) != 1: sys.exit(f'{what}: found {len(hit)} times')
    return hit[0]


def section(path, start, new_text):   # from the line starting with `start` to the next "// ----------" section line
    lines = text(path).split('\n')
    a = once(lines, lambda l: l.startswith(start), f'{path.name}: {start!r}')
    b = next(i for i in range(a + 1, len(lines)) if lines[i].startswith('// ----------'))
    new = new_text.rstrip('\n').split('\n') + ['']
    if lines[a:b] != new:
        lines[a:b] = new
        write(path, '\n'.join(lines))


def func(path, header, new_text):   # a whole function: its header line to the first line that is "}"
    lines = text(path).split('\n')
    a = once(lines, lambda l: l.startswith(header), f'{path.name}: {header!r}')
    b = next(i for i in range(a + 1, len(lines)) if lines[i] == '}')
    new = new_text.rstrip('\n').split('\n')
    if lines[a:b + 1] != new:
        lines[a:b + 1] = new
        write(path, '\n'.join(lines))


def body(path, name, new, sig=None):   # a one-line hook: keep its head (or change its argument list), replace its body
    ls = text(path).split('\n')
    i = once(ls, lambda l: l.startswith(('SKF("' + name + '"', 'HLE_FORCE("' + name + '"')), f'{path.name}: {name}')
    l = ls[i]
    if not l.rstrip().endswith('}'): sys.exit(f'{path.name}: {name} is not a one-line definition')
    head = l[:l.index(') { ') + 1]
    if sig and sig[1] not in head: head = head[:head.rindex(sig[0])] + sig[1]
    nl = head + ' ' + new
    if nl != l:
        ls[i] = nl
        write(path, '\n'.join(ls))


# ---- dm.h: the observation gate of "Interact everywhere" (the profile's patches.py places it on 2.0.1's main; ldn_glue.c attaches the callback)
edit(host / 'dm.h', '       OBS_MAX = 27 };\n',
     '       OBS_COOP_INTERACT = 27,                                         // Link Play\'s "Interact everywhere" (hle_coop_interact.c): a site of the profile\'s, its id in the upper bits\n'
     '       OBS_MAX = 28 };\n')

# ---- hle_offline.c: nn::ldn, and the socket overloads under co-op only
off = host / 'hle_offline.c'
section(off, '// ---------- ldn: local wireless', text(HERE / 'ldn_glue.c'))
edit(off, 'enum { SK_ENETDOWN = 50, SK_EAI_FAIL = 4 };\n',
     'enum { SK_ENETDOWN = 50, SK_EAI_FAIL = 4 };\n'
     '// Co-op on: the overloads the game\'s LDN data path can use go through hle_net.c\'s mpsk_* (the coop module\'s virtual UDP); a descriptor that is not virtual\n'
     '// still gets -1 / ENETDOWN. Co-op off: the offline answers, unchanged\n'
     'int mpsk_errno(void); int mpsk_socket(int fam, int type, int proto); int mpsk_bind(int s, const void *a, unsigned n);\n'
     'int mpsk_getname(int s, void *a, unsigned *n); int mpsk_getopt(int s, int l, int o, void *v, unsigned *n); int mpsk_setopt(int s, int l, int o, const void *v, unsigned n);\n'
     'int mpsk_fcntl(int s, int cmd, int v); long mpsk_sendto(int s, const void *b, unsigned long n, int f, const void *to, unsigned tl);\n'
     'long mpsk_recvfrom(int s, void *b, unsigned long n, int f, void *from, unsigned *fl); long mpsk_recv(int s, void *b, unsigned long n, int f); long mpsk_send(int s, const void *b, unsigned long n, int f);\n'
     '#define SKCOOP(call) do { if (dm_coop()) return call; } while (0)\n')
FCNTL = ('int c)', 'int c, int v)')   # Fcntl is variadic; its third argument arrives in x2
body(off, '_ZN2nn6socket12GetLastErrnoEv', '{ SKCOOP(mpsk_errno()); return SK_ENETDOWN; }')
body(off, '_ZN2nn6socket6SocketEiii', '{ SKCOOP(mpsk_socket(a, b, c)); (void)a; (void)b; (void)c; return -1; }')
body(off, '_ZN2nn6socket4BindEiPK8sockaddrj', '{ SKCOOP(mpsk_bind(s, a, n)); (void)s; (void)a; (void)n; return -1; }')
body(off, '_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj', '{ SKCOOP(mpsk_sendto(s, b, n, f, a, al)); (void)s; (void)b; (void)n; (void)f; (void)a; (void)al; return -1; }')
body(off, '_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj', '{ SKCOOP(mpsk_sendto(s, b, n, f, a, al)); (void)s; (void)b; (void)n; (void)f; (void)a; (void)al; return -1; }')
body(off, '_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj', '{ SKCOOP(mpsk_recvfrom(s, b, n, f, a, al)); (void)s; (void)b; (void)n; (void)f; (void)a; (void)al; return -1; }')
body(off, '_ZN2nn6socket11GetSockNameEiP8sockaddrPj', '{ SKCOOP(mpsk_getname(s, a, n)); (void)s; (void)a; (void)n; return -1; }')
body(off, '_ZN2nn6socket10GetSockOptEiiiPvPj', '{ SKCOOP(mpsk_getopt(s, l, o, v, n)); (void)s; (void)l; (void)o; (void)v; (void)n; return -1; }')
body(off, '_ZN2nn6socket10SetSockOptEiiiPKvj', '{ SKCOOP(mpsk_setopt(s, l, o, v, n)); (void)s; (void)l; (void)o; (void)v; (void)n; return -1; }')
body(off, '_ZN2nn6socket5FcntlEiiz', '{ SKCOOP(mpsk_fcntl(s, c, v)); (void)s; (void)c; (void)v; return -1; }', sig=FCNTL)
body(off, '_ZN2nn6socket4RecvEiPvmi', '{ SKCOOP(mpsk_recv(s, b, n, f)); (void)s; (void)b; (void)n; (void)f; return -1; }')
body(off, '_ZN2nn6socket4SendEiPKvmi', '{ SKCOOP(mpsk_send(s, b, n, f)); (void)s; (void)b; (void)n; (void)f; return -1; }')

# ---- hle_net.c: the routing block after sk_fail, Poll / Select, the socket hooks
net = host / 'hle_net.c'
s = text(net)
a, b = 'static int sk_fail(void) { t_skerr = SK_ENETDOWN; return -1; }\n', '#define SKF(m, cname, ...) HLE_FORCE('
if s.count(a) != 1 or s.count(b) != 1: sys.exit('hle_net.c: sk_fail / SKF anchors')
i, j = s.index(a) + len(a), s.index(b)
block = text(HERE / 'sock_glue.c')
if s[i:j] != block: write(net, s[:i] + block + s[j:])
poll = text(HERE / 'sock_poll.c').split('\nint dm_sk_select(')
func(net, 'int dm_sk_poll(void *fds, uint64_t n, int ms) {', poll[0])
func(net, 'int dm_sk_select(int nfds, const uint64_t *r, const uint64_t *w, const uint64_t *e, const int64_t *tv) {', 'int dm_sk_select(' + poll[1])
for nm in ('_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE', 'nnsocketSocket'): body(net, nm, '{ return mpsk_socket(a, b, c); }')
for nm in ('_ZN2nn6socket4BindEiPKNS0_8SockAddrEj', 'nnsocketBind'): body(net, nm, '{ return mpsk_bind(s, a, n); }')
for nm in ('_ZN2nn6socket5CloseEi', 'nnsocketClose'): body(net, nm, '{ return mpsk_close(s); }')
for nm in ('_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj', 'nnsocketGetSockName'): body(net, nm, '{ return mpsk_getname(s, a, n); }')
for nm in ('_ZN2nn6socket10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj', 'nnsocketGetSockOpt'): body(net, nm, '{ return mpsk_getopt(s, l, o, v, n); }')
for nm in ('_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj', 'nnsocketSetSockOpt'): body(net, nm, '{ return mpsk_setopt(s, l, o, v, n); }')
for nm in ('_ZN2nn6socket5FcntlEiNS0_12FcntlCommandEz', 'nnsocketFcntl'): body(net, nm, '{ return mpsk_fcntl(s, c, v); }', sig=FCNTL)
for nm in ('_ZN2nn6socket4RecvEiPvmNS0_7MsgFlagE', 'nnsocketRecv'): body(net, nm, '{ return mpsk_recv(s, b, n, f); }')
body(net, '_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj', '{ return mpsk_recvfrom(s, b, n, f, a, al); }')
for nm in ('_ZN2nn6socket4SendEiPKvmNS0_7MsgFlagE', 'nnsocketSend'): body(net, nm, '{ return mpsk_send(s, b, n, f); }')

# ---- hle_misc.c: parental controls allow communication only while co-op is on
misc = host / 'hle_misc.c'
edit(misc, '// ---------- nsd server name resolution / pctl free communication: networking, out of scope ----------\n',
     '// ---------- nsd server name resolution / pctl free communication: networking, out of scope ----------\n'
     '// Link Play asks parental controls before it opens (research/06 §3.1): while co-op is on (DM_COOP, the launcher\'s Link Play switch) the three\n'
     '// communication queries answer allowed, else never\n'
     'struct coop_node; struct coop_node *dm_coop(void);   // hle_offline.c: NULL with co-op off\n'
     'static unsigned pctl_coop(void) { return dm_coop() != NULL; }\n')
edit(misc, 'h_pctl_CheckFree, unsigned show_ui) { (void)show_ui; return 0; }', 'h_pctl_CheckFree, unsigned show_ui) { (void)show_ui; return pctl_coop(); }')
edit(misc, 'h_pctl_TryBegin, unsigned show_ui) { (void)show_ui; return 0; }', 'h_pctl_TryBegin, unsigned show_ui) { (void)show_ui; return pctl_coop(); }')
edit(misc, 'h_pctl_IsFree, void) { return 0; }', 'h_pctl_IsFree, void) { return pctl_coop(); }')

# ---- the launcher's switch: DM_COOP auto / off; under it, a host to join by address (DM_COOP_JOIN) and internet play (step 1d)
st = root / 'dm/games/settings.tsv'
rows = ['DM_COOP\ttoggle\tauto/off\t0\talways\tGame\tLink Play on this network\t'
        "Visit and host towns with players on the same Wi-Fi. Asks once for local network access.",
        'DM_COOP_JOIN\ttext\t21\t\talways\tGame\tJoin address\t'
        "Visit one host by its address, IPv4[:port] (on another network, turn on Internet play too). Empty: find hosts on this Wi-Fi, or host."
        "\tneeds=DM_COOP=auto",
        'DM_COOP_INTERNET\ttoggle\t-\t0\talways\tGame\tInternet play\t'
        "Play with someone on another network: both enter the same room code. Not encrypted.\tneeds=DM_COOP=auto",
        'DM_COOP_ROOM\ttext\t19\t\talways\tGame\tRoom code\t'
        "The host's code, XXXX-XXXX-XXXX-XXXX. A host without one gets a new code each time.\tneeds=DM_COOP_INTERNET=1",
        'DM_COOP_RELAY\ttext\t21\t\talways\tGame\tRelay address\t'
        "A relay you trust, as IPv4[:port]. Empty: visitors connect to the host's address.\tneeds=DM_COOP_INTERNET=1",
        'DM_COOP_INTERACT\ttoggle\t-\t0\talways\tGame\tInteract everywhere\t'
        "Experimental: in every town, not only Palette Town, visitors can use moves and items and build. Every player needs it on.\tneeds=DM_COOP=auto"]
mine = {r.split('\t', 1)[0] for r in rows}
lines = [l for l in text(st).split('\n') if l.split('\t', 1)[0] not in mine]   # an earlier version's rows are replaced
at = next((i for i, l in enumerate(lines) if l.startswith('DM_SHOW_FPS\t')), None)
if at is None: sys.exit('settings.tsv: no DM_SHOW_FPS row')
lines[at + 1:at + 1] = rows
write(st, '\n'.join(lines))
edit(st, '#             toggle: values - (on sets 1, off 0) or <on>/<off>, the two values the variable takes (on/off)\n',
     '#             toggle: values - (on sets 1, off 0) or <on>/<off>, the two values the variable takes (on/off)\n'
     '#             text: values its longest length (1-64): a short free text, printable ASCII, such as a code or an address; empty by default\n')

# ---- the launcher's text kind (launcher.json schema 2): the list's writer, its contract and the readers on the iPhone and the Mac
sp = root / 'dm/games/settings.py'
edit(sp, "SCHEMA = 1   # launcher.json's version (docs/launcher.schema.json); it grows only by optional fields, so a reader of 1 reads every 1\n",
     "SCHEMA = 2   # launcher.json's version (docs/launcher.schema.json): 2 added the text kind; within a version it grows only by optional fields\n")
edit(sp, "        else: sys.exit(f'settings.tsv:{n}: unknown kind {kind}')\n",
     "        elif kind == 'text':   # a short free text (a room code, an address): printable ASCII up to its length, empty by default\n"
     "            if not re.fullmatch(r'[1-9][0-9]?', values) or int(values) > 64: sys.exit(f'settings.tsv:{n}: text values are its longest length, 1-64: {values}')\n"
     "            if len(s['default']) > int(values) or not all(' ' <= ch <= '~' for ch in s['default']):\n"
     "                sys.exit(f'settings.tsv:{n}: a text default is printable ASCII of at most {values} characters: {s[\"default\"]}')\n"
     "            s['length'] = int(values)\n"
     "        else: sys.exit(f'settings.tsv:{n}: unknown kind {kind}')\n")
sch = root / 'docs/launcher.schema.json'
edit(sch, 'Schema 1 grows only by optional fields: a reader ignores fields it does not know, and a field keeps its meaning.',
     'Within a version it grows only by optional fields: a reader ignores fields it does not know, and a field keeps its meaning. A new kind is a new version: 2 added text.')
edit(sch, '"schema": {"description": "The list\'s version; a list without it is schema 1 written before the field existed.", "type": "integer", "const": 1},',
     '"schema": {"description": "The list\'s version; a list without it is schema 1 written before the field existed. 2 added the text kind; a schema 1 list is a schema 2 list without text rows.", "type": "integer", "const": 2},')
edit(sch, '"kind": {"enum": ["choice", "range", "toggle"]},', '"kind": {"enum": ["choice", "range", "toggle", "text"]},')
edit(sch, '        "off": {"description": "toggle: the value the switch takes when off.", "type": "string"},\n',
     '        "off": {"description": "toggle: the value the switch takes when off.", "type": "string"},\n'
     '        "length": {"description": "text: the longest value, in characters (printable ASCII); the default is empty or shorter.", "type": "integer"},\n')
edit(sch, '        {"if": {"properties": {"kind": {"const": "toggle"}}}, "then": {"required": ["on", "off"]}}\n',
     '        {"if": {"properties": {"kind": {"const": "toggle"}}}, "then": {"required": ["on", "off"]}},\n'
     '        {"if": {"properties": {"kind": {"const": "text"}}}, "then": {"required": ["length"]}}\n')
fe = root / 'docs/frontends.md'
edit(fe, "`range` (a number from `min` to `max` in `step`s) or `toggle` (`on` or `off`, the values the switch takes).",
     "`range` (a number from `min` to `max` in `step`s), `toggle` (`on` or `off`, the values the switch takes) or `text` (a short free text of at most `length` "
     "printable ASCII characters, such as a code or an address; empty by default).")
edit(fe, "`schema` is 1, and schema 1 grows only by optional fields: a reader ignores fields it does not know, draws sections it does not know, and a field never "
         "changes its meaning. A new `kind` would be schema 2, since a reader cannot draw a kind it does not know. A list written before the field existed has no `schema` and is 1.",
     "`schema` is 2, and within a version the list grows only by optional fields: a reader ignores fields it does not know, draws sections it does not know, and a field "
     "never changes its meaning. A new `kind` is a new version, since a reader cannot draw a kind it does not know: schema 2 added `text`, and a schema 1 list is a schema 2 "
     "list without text rows. A list written before the field existed has no `schema` and is 1.")
fe_saved = ("one of a `choice`'s options, a `toggle`'s `on` or `off`, or a number from a `range`'s `min` to `max`.",
            "one of a `choice`'s options, a `toggle`'s `on` or `off`, a number from a `range`'s `min` to `max`, or printable ASCII of at most a `text`'s `length`.")
edit(fe, *fe_saved)
ls_ = root / 'dm/ios/Sources/LauncherSettings.swift'
edit(ls_, "    let on, off: String?   // toggle: the values the variable takes (1 / 0 when not given)\n",
     "    let on, off: String?   // toggle: the values the variable takes (1 / 0 when not given)\n"
     "    let length: Int?       // text: the longest value (schema 2)\n")
edit(ls_, '        case "toggle": return v == onValue || v == offValue\n',
     '        case "toggle": return v == onValue || v == offValue\n'
     '        case "text": return v.count <= (length ?? 64) && v.unicodeScalars.allSatisfy { $0.value >= 0x20 && $0.value < 0x7F }   // as launcher.c\n')
edit(ls_, 'case env, kind, label, note, def = "default", section, needs, level, base, options, min, max, step, on, off }',
     'case env, kind, label, note, def = "default", section, needs, level, base, options, min, max, step, on, off, length }')
lr = root / 'dm/ios/Sources/LauncherRow.swift'
edit(lr, '// One launcher setting as a row (a menu, a slider with its percentage, a toggle),',
     '// One launcher setting as a row (a menu, a slider with its percentage, a toggle, a text field),')
edit(lr, '            default:\n                Toggle(isOn:',
     '            case "text":   // printable ASCII up to the setting\'s length, as Setting.accepts\n'
     '                VStack(alignment: .leading, spacing: 8) {\n'
     '                    settingLabel\n'
     '                    TextField(s.label, text: Binding(get: { m.values[s.env] ?? s.def },\n'
     '                                                     set: { m.values[s.env] = String($0.filter { $0.asciiValue.map { $0 >= 0x20 && $0 < 0x7F } ?? false }.prefix(s.length ?? 64)) }))\n'
     '                        .textFieldStyle(.roundedBorder)\n'
     '                        .autocorrectionDisabled()\n'
     '#if os(iOS)\n'
     '                        .textInputAutocapitalization(.characters)\n'
     '                        .keyboardType(.asciiCapable)\n'
     '#endif\n'
     '                }\n'
     '            default:\n                Toggle(isOn:')
lc = root / 'dm/mac/launcher.c'
edit(lc, 'static int saved_accepted(CFDictionaryRef rule, CFStringRef v, const char *s) {   // a saved choice this build still offers: one of its values, or a number in its range\n',
     'static int saved_accepted(CFDictionaryRef rule, CFStringRef v, const char *s) {   // a saved choice this build still offers: one of its values, a number in its range or a short text\n')
edit(lc, '        return 0;\n    }\n    CFTypeRef lo = CFDictionaryGetValue(rule, CFSTR("min"))',
     '        return 0;\n    }\n'
     '    CFTypeRef len = CFDictionaryGetValue(rule, CFSTR("length"));\n'
     '    if (len && CFGetTypeID(len) == CFNumberGetTypeID()) {   // text: printable ASCII of at most length characters, as Setting.accepts\n'
     '        long n = 0; CFNumberGetValue(len, kCFNumberLongType, &n);\n'
     '        size_t k = strlen(s);\n'
     '        if ((long)k > n) return 0;\n'
     '        for (size_t i = 0; i < k; i++) if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return 0;\n'
     '        return 1;\n'
     '    }\n'
     '    CFTypeRef lo = CFDictionaryGetValue(rule, CFSTR("min"))')
pa = root / 'dm/mac/pack_app.py'
edit(pa, "        elif s['kind'] == 'toggle': rules.append({'env': s['env'], 'values': [s.get('on', '1'), s.get('off', '0')]})\n",
     "        elif s['kind'] == 'toggle': rules.append({'env': s['env'], 'values': [s.get('on', '1'), s.get('off', '0')]})\n"
     "        elif s['kind'] == 'text': rules.append({'env': s['env'], 'length': int(s['length'])})\n")
tl = root / 'dm/tools/tests/test_launcher_settings.py'
edit(tl, "        self.assertEqual(listed['schema'], 1)\n", "        self.assertEqual(listed['schema'], 2)\n")
edit(tl, "    def test_schema_1_takes_new_sections_but_not_new_kinds(self):\n"
         "        \"\"\"A section is a heading a reader draws by its name, so a new one stays schema 1; a new kind would not\"\"\"\n",
     "    def test_new_sections_keep_the_schema_but_new_kinds_do_not(self):\n"
     "        \"\"\"A section is a heading a reader draws by its name, so a new one keeps the schema; a new kind would not (schema 2 added text)\"\"\"\n")
ROWS_1D = ("    def test_link_play_rows(self):   # the co-op rows: Link Play, then internet play and its two text rows under it\n"
           "        rows = build(BOTW, 'nvn')\n"
           "        self.assertEqual((rows['DM_COOP']['kind'], rows['DM_COOP']['default']), ('toggle', 'off'))\n"
           "        self.assertEqual((rows['DM_COOP_INTERNET']['kind'], rows['DM_COOP_INTERNET']['default'], rows['DM_COOP_INTERNET']['needs']), ('toggle', '0', 'DM_COOP=auto'))\n"
           "        for env, length in (('DM_COOP_ROOM', 19), ('DM_COOP_RELAY', 21)):\n"
           "            self.assertEqual((rows[env]['kind'], rows[env]['default'], rows[env]['length'], rows[env]['needs']), ('text', '', length, 'DM_COOP_INTERNET=1'))\n"
           "        envs = list(rows)\n"
           "        self.assertEqual(envs[envs.index('DM_COOP'):envs.index('DM_COOP') + 4], ['DM_COOP', 'DM_COOP_INTERNET', 'DM_COOP_ROOM', 'DM_COOP_RELAY'])\n")
ROWS = ("    def test_link_play_rows(self):   # the co-op rows: Link Play, then the join address, internet play and its two text rows under it\n"
        "        rows = build(BOTW, 'nvn')\n"
        "        self.assertEqual((rows['DM_COOP']['kind'], rows['DM_COOP']['default']), ('toggle', 'off'))\n"
        "        self.assertEqual((rows['DM_COOP_JOIN']['kind'], rows['DM_COOP_JOIN']['default'], rows['DM_COOP_JOIN']['length'], rows['DM_COOP_JOIN']['needs']), ('text', '', 21, 'DM_COOP=auto'))\n"
        "        self.assertEqual((rows['DM_COOP_INTERNET']['kind'], rows['DM_COOP_INTERNET']['default'], rows['DM_COOP_INTERNET']['needs']), ('toggle', '0', 'DM_COOP=auto'))\n"
        "        for env, length in (('DM_COOP_ROOM', 19), ('DM_COOP_RELAY', 21)):\n"
        "            self.assertEqual((rows[env]['kind'], rows[env]['default'], rows[env]['length'], rows[env]['needs']), ('text', '', length, 'DM_COOP_INTERNET=1'))\n"
        "        envs = list(rows)\n"
        "        self.assertEqual(envs[envs.index('DM_COOP'):envs.index('DM_COOP') + 5], ['DM_COOP', 'DM_COOP_JOIN', 'DM_COOP_INTERNET', 'DM_COOP_ROOM', 'DM_COOP_RELAY'])\n")
if ROWS_1D in text(tl): write(tl, text(tl).replace(ROWS_1D, ROWS))   # a tree with step 1d's rows: the join address joins them
ROWS_LAST = "        self.assertEqual(envs[envs.index('DM_COOP'):envs.index('DM_COOP') + 5], ['DM_COOP', 'DM_COOP_JOIN', 'DM_COOP_INTERNET', 'DM_COOP_ROOM', 'DM_COOP_RELAY'])\n"
INTERACT = ("\n    def test_interact_everywhere_row(self):   # an experimental switch under Link Play, after its internet rows\n"
            "        rows = build(BOTW, 'nvn')\n"
            "        self.assertEqual((rows['DM_COOP_INTERACT']['kind'], rows['DM_COOP_INTERACT']['default'], rows['DM_COOP_INTERACT']['needs']), ('toggle', '0', 'DM_COOP=auto'))\n"
            "        envs = list(rows)\n"
            "        self.assertEqual(envs.index('DM_COOP_INTERACT'), envs.index('DM_COOP_RELAY') + 1)\n")
edit(tl, "        self.assertEqual(conforms({'schema': 1, 'game': BOTW, 'settings': [row]}, schema, schema), [])\n"
         "        self.assertNotEqual(conforms({'schema': 1, 'game': BOTW, 'settings': [{**row, 'kind': 'slider'}]}, schema, schema), [])\n",
     "        self.assertEqual(conforms({'schema': settings.SCHEMA, 'game': BOTW, 'settings': [row]}, schema, schema), [])\n"
     "        self.assertNotEqual(conforms({'schema': settings.SCHEMA, 'game': BOTW, 'settings': [{**row, 'kind': 'slider'}]}, schema, schema), [])\n"
     "        text = {'env': 'DM_X', 'kind': 'text', 'section': 'Game', 'label': 'X', 'note': '', 'default': ''}\n"
     "        self.assertEqual(conforms({'schema': settings.SCHEMA, 'game': BOTW, 'settings': [{**text, 'length': 19}]}, schema, schema), [])\n"
     "        self.assertNotEqual(conforms({'schema': settings.SCHEMA, 'game': BOTW, 'settings': [text]}, schema, schema), [], 'a text row has a length')\n\n"
     + ROWS)
if INTERACT not in text(tl): edit(tl, ROWS_LAST, ROWS_LAST + INTERACT)
edit(tl, "                         ('DM_X\\ttoggle\\ton\\t0\\talways\\tGame\\tX\\n', 'toggle values'), ('DM_X\\ttoggle\\t-\\t2\\talways\\tGame\\tX\\n', 'defaults to 0 or 1')]:\n",
     "                         ('DM_X\\ttoggle\\ton\\t0\\talways\\tGame\\tX\\n', 'toggle values'), ('DM_X\\ttoggle\\t-\\t2\\talways\\tGame\\tX\\n', 'defaults to 0 or 1'),\n"
     "                         ('DM_Z\\ttext\\t0\\t\\talways\\tGame\\tZ\\n', 'longest length'), ('DM_Z\\ttext\\t2\\tabc\\talways\\tGame\\tZ\\n', 'at most 2')]:\n")

# ---- docs/env_switches.tsv (dm/tools/env_switches.py --check): the switches the module reads, in the table's name order
SWITCHES = {
    'DM_COOP': 'player\tLink Play between our builds (hle_coop_*.c): `auto` hosts on the LAN while the game hosts and finds LAN hosts with DNS-SD while it scans; '
               '`join:<IPv4>[:<port>]` joins one host by address (loopback, RFC 1918 or 169.254 only, any address with `DM_COOP_INTERNET=1`; port 47600 by default); '
               '`host:<port>` hosts on 127.0.0.1 only; `off` (the default) keeps Link Play offline. The launcher\'s "Link Play on this network" switch sets `auto` / `off`',
    'DM_COOP_DISCOVERY': 'switch\t`0` turns DNS-SD off under `DM_COOP=auto` (no advertising, no browsing): visitors join by address',
    'DM_COOP_INTERACT': 'player\t`1` turns on "Interact everywhere" (the launcher\'s switch, experimental): in Link Play the story areas, where the game forces Spectator '
                        'Mode, give players the permissions Palette Town gives with Spectator Mode off (hle_coop_interact.c, at an observation gate the profile places '
                        'on 2.0.1). Every player in a session needs the same setting: it is part of the mods fingerprint. Off by default',
    'DM_COOP_INTERNET': 'player\t`1` turns on internet play (the launcher\'s "Internet play"): links from any address, each proving `DM_COOP_ROOM`, every message tagged '
                        'against changes (not encrypted); `join:` takes public addresses. Off by default',
    'DM_COOP_JOIN': 'player\t`<IPv4>[:<port>]`: with `DM_COOP=auto`, joins that host instead of finding hosts with DNS-SD (as `DM_COOP=join:` does; a public address '
                    'needs `DM_COOP_INTERNET=1`). The launcher\'s "Join address"; empty by default',
    'DM_COOP_RELAY': 'player\t`<IPv4>[:<port>]` (port 47610 by default): with `DM_COOP_INTERNET=1` and `DM_COOP=auto`, a relay players run themselves (windows-native '
                     'tools/coop-relay): hosting opens the room there, scanning asks it for the room\'s host. The launcher\'s "Relay address"; empty by default',
    'DM_COOP_ROOM': 'player\tThe room code, `XXXX-XXXX-XXXX-XXXX` (16 letters and digits, 80 bits): with `DM_COOP_INTERNET=1` every link proves it. A visitor needs the '
                    'host\'s; a host without one gets a new one each session (in the log). The launcher\'s "Room code"; empty by default',
    'DM_COOP_TCP': 'switch\t`1` sends the game\'s Link Play datagrams over the control link instead of UDP (a TCP-only path such as an SSH tunnel)',
    'DM_UID': 'test\t`=<32 hex digits>`: the user ID Link Play announces for this instance while `DM_COOP` is set (its node\'s MAC address derives from it; '
              'two instances on one machine); unset, nxnative\'s constant. The account service keeps its own',
}
tsv = root / 'docs/env_switches.tsv'
lines = text(tsv).split('\n')
names = [l.split('\t', 1)[0] for l in lines]
for name, rest in SWITCHES.items():
    if name in names:   # an earlier version's text is replaced in place
        lines[names.index(name)] = f'{name}\t{rest}'
        continue
    at = next((i for i in range(1, len(lines)) if lines[i] and lines[i].split('\t', 1)[0] > name), len(lines) - 1 if lines[-1] == '' else len(lines))
    lines.insert(at, f'{name}\t{rest}'); names.insert(at, name)
write(tsv, '\n'.join(lines))

# ---- the local network permission and the Bonjour service type: the iPhone app (xcodegen) and the Mac app
edit(root / 'dm/ios/project.yml', '        LSSupportsOpeningDocumentsInPlace: true\n',
     '        LSSupportsOpeningDocumentsInPlace: true\n'
     f'        NSLocalNetworkUsageDescription: "{REASON}"   # Link Play (hle_coop_*.c): finding and joining towns on the LAN\n'
     f'        NSBonjourServices: [{SERVICE}]          # the DNS-SD type Link Play advertises and browses (iOS refuses others)\n')
edit(root / 'dm/mac/pack_app.py', "'DMRomfsMarker': a.marker or '', 'DMLanguage': a.lang or 'en-US'}\n",
     "'DMRomfsMarker': a.marker or '', 'DMLanguage': a.lang or 'en-US',\n"
     f"         'NSLocalNetworkUsageDescription': \"{REASON}\", 'NSBonjourServices': ['{SERVICE}']}}   # Link Play (hle_coop_*.c)\n")
print('applied to', root)
