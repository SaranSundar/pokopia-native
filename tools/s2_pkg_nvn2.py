#!/usr/bin/env python3
"""Package Pokopia's Switch 2 shaders for nxnative's chain as the NVN2 records its shader_split.py writes: no lowering, the
chain's translator reads the Ampere code itself (nxnative dm/gfx/tools/dmsh_ampere.py; dmsh_xlate job arch sm86).

  s2_pkg_nvn2.py <corpus-ctl dir> <pkg out dir> <tables out dir>

Inputs: <corpus-ctl>/hostkeys.tsv and, per host key, <hostkey>.ctl (the 0xe0 control block) and <hostkey>.code (the code area
as the game uploads it, control +0x44 bytes: the 0x80-byte SPH of a graphics stage, the instructions, padding, the c1 constants
between control +0x40 and +0x44), both from s2_ctl_scan.py and s2_dumps.py.
Outputs, in the shape dm/gfx/tools/shader_split.py writes: <pkg>/<stage>_pokopia.bin record files ('BTCDBTCD' + 0x38 header with
the key's 8 bytes at +0x08, the control length at +0x28 and the code length at +0x2c, then the code area, then the control block),
<tables>/shader_map.tsv (package, index, stage, dm_ctl_sha16, source), shader_facts.tsv (compute shaders: the thread group at
control +0x58 and the shared memory at +0x64, as shader_split.py reads an NVN2 section) and an empty shader_pairs.tsv.
The host key is the record key: nxnative's keys_hash of an NVN2 section (the first 0x48 control bytes, +0x30..+0x33 as zero, then
the code area), which s2_ctl_scan.py / s2_dumps.py compute, so the host finds each shader's library by the key it computes at run time.
"""
import collections, json, pathlib, struct, sys

CTL_MAGIC = bytes.fromhex("34127698")
STAGE = {0: "vs", 1: "hs", 2: "ds", 3: "gs", 4: "ps", 5: "cs"}   # dump stage numbering (s2_ctl_scan.py) → shader_split.py's package stage
STAGE_NAME = {"vs": "vertex", "hs": "hull", "ds": "domain", "gs": "geometry", "ps": "pixel", "cs": "compute"}


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    ctl_dir, pkg, tables = (pathlib.Path(a) for a in sys.argv[1:4])
    pkg.mkdir(parents=True, exist_ok=True); tables.mkdir(parents=True, exist_ok=True)
    rows_in = [l.split("\t") for l in (ctl_dir / "hostkeys.tsv").read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    recs = collections.defaultdict(list); rows = []; facts = {}; stat = collections.Counter(); seen = set()
    for hostkey, corpuskey, stage, code_len, insn, group, shared, source in rows_in:
        if hostkey in seen:
            continue
        seen.add(hostkey)
        st = STAGE[int(stage)]
        cp = ctl_dir / f"{hostkey}.ctl"; kp = ctl_dir / f"{hostkey}.code"
        if not cp.is_file() or not kp.is_file():
            stat["no control or code file (rerun the corpus scan)"] += 1; continue
        ctl = cp.read_bytes(); code = kp.read_bytes()
        if len(ctl) < 0xe0 or ctl[:4] != CTL_MAGIC or struct.unpack_from("<I", ctl, 4)[0] != 0xc9:
            stat["not an NVN2 control block"] += 1; continue
        gpu_len = struct.unpack_from("<I", ctl, 0x44)[0]
        if len(code) != gpu_len:
            stat["code area length differs from control +0x44"] += 1
            if len(code) < gpu_len: continue
            code = code[:gpu_len]
        if CTL_MAGIC in code:
            stat["control magic inside code"] += 1; continue   # dmsh_build.py finds the control block by its magic after the header
        if st == "cs":
            x, y, z = struct.unpack_from("<3I", ctl, 0x58); sh = struct.unpack_from("<I", ctl, 0x64)[0]
            if not x * y * z:
                stat["compute group 0 in the control block"] += 1
            facts[hostkey] = {"local": [x, y, z], "shared": (sh + 15) // 16 * 16}
        rec = bytearray(b"BTCDBTCD" + bytes(0x30))
        rec[0x08:0x10] = bytes.fromhex(hostkey)
        struct.pack_into("<2I", rec, 0x28, len(ctl), len(code))
        rec += code + ctl
        assert rec.find(CTL_MAGIC, 0x38) == 0x38 + len(code)
        name = f"{st}_pokopia.bin"
        rows.append((name, len(recs[name]), STAGE_NAME[st], hostkey, source)); recs[name].append(bytes(rec)); stat[st] += 1
    for f in pkg.glob("*.bin"):
        f.unlink()
    for name, rr in recs.items():
        (pkg / name).write_bytes(b"".join(rr))
    with open(tables / "shader_map.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("# kind: evidence  Pokemon Pokopia GLSLC 0x00220015 containers (main + LINKDATA + RDB) and run-time dumps, keyed as nxnative keys NVN2 control sections (s2_ctl_scan.py, s2_dumps.py), packaged as NVN2 records by s2_pkg_nvn2.py\n")
        f.write("# column 5 records where each key first appears; no PC shader pairing supplied\n")
        f.write("package\tindex\tstage\tdm_ctl_sha16\tsource\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")
    with open(tables / "shader_facts.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("# Answer table for translation queries (s2_pkg_nvn2.py, as dm/gfx/tools/shader_split.py writes it): key <TAB> fact JSON. Only the thread group / shared memory of compute shaders, from the NVN2 control block (+0x58, +0x64).\n")
        for k in sorted(facts):
            f.write(k + "\t" + json.dumps(facts[k], sort_keys=True, separators=(",", ":")) + "\n")
    with open(tables / "shader_pairs.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("# (vertex key, pixel key) pairs: none known for Pokopia (s2_pkg_nvn2.py)\n")
    print("host keys", len(seen), "records", len(rows), dict(stat))


if __name__ == "__main__":
    main()
