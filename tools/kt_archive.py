"""kt_archive.py: the Koei Tecmo archive readers the shader tools need: the IDRK entries of RDB data files, and the older chunked zlib.
Copied unchanged from nxnative's dm/gfx/tools/shader_split.py (https://github.com/seedds/nxnative at c1df4dd; MIT, Copyright (c) 2026 seedds;
its licence text is in host/NOTICE.md), so the tools no longer load that file or anything it imports."""
import struct

IDRK, PDRK = b'IDRK0000', b'PDRK0000'

def idrk_entries(d):
    """Koei Tecmo RDB data files (Hyrule Warriors: Age of Calamity: asset/*.rdb.bin*, asset/data/*.file) are a run of IDRK entries: 'IDRK0000', u64 entry length, u64 stored length, u64 length,
    u32 0, u32 file KTID, u32 type KTID, u32 flags, a few parameter words, and the stored data at the end of the entry. Entries follow each other directly, some after padding (to 64 KB).
    ROMANCE OF THE THREE KINGDOMS 8 REMAKE's RDB (0002/root.rdb and system.rdb, indexed by .rdx) keeps the entries in .fdata files named by KTID, each starting with 'PDRK0000' and u64 0x10
    (its length), and in data/*.file as before; its entries have u32 1 or 0 where Age of Calamity's have 0, and pad to 16 bytes.
    → (start of the stored data, its end, flags) for each entry"""
    o = 0
    while o + 0x30 <= len(d):
        if d[o:o + 8] != IDRK:
            o = d.find(IDRK, o)
            if o < 0: return
        esz, csz = struct.unpack_from('<QQ', d, o + 8)
        yield o + esz - csz, o + esz, struct.unpack_from('<I', d, o + 0x2c)[0]
        o += esz

def idrk_data(d, a, b, flags, head=None):
    """An entry's data. Flag 0x100000 alone = zlib in chunks (Age of Calamity): u32 stored length + a zlib stream each, a zero length ends; the chunks inflate to at most 16 KB.
    Flag 0x400000 = chunks of another form (ROMANCE OF THE THREE KINGDOMS 8 REMAKE): u16 stored length, 8 bytes that the reader does not need, the stream; a zero length ends, and each chunk
    inflates to at most 16 KB (0x4000, which LZ4 is given as the most). The streams are LZ4 blocks when 0x100000 is set too, zlib otherwise: of that game's 51,655 entries, 49,620 have flags
    0x500000, 2,026 have 0x400000 and 9 are stored (0); all of them inflate to the length in their header. Anything else is stored. head: only the first bytes"""
    import zlib
    new, lz4b = flags & 0x400000, flags & 0x500000 == 0x500000
    if not flags & 0x500000: return d[a:min(b, a + head) if head else b]
    out = []
    while a + 4 <= b:
        if new: cs = struct.unpack_from('<H', d, a)[0]; a += 10
        else: cs = struct.unpack_from('<I', d, a)[0]; a += 4
        if not cs: break
        if lz4b:
            import lz4.block
            r = lz4.block.decompress(d[a:a + cs], uncompressed_size=0x4000)
            if head: return r[:head]
        elif head: return zlib.decompressobj().decompress(d[a:a + min(cs, 1024)], head)
        else: r = zlib.decompress(d[a:a + cs])
        out.append(r); a += cs
    return b''.join(out)

def kt_gz(d):
    """Koei Tecmo's older chunked zlib (Fire Emblem: Three Houses: the compressed entries of DATA1.bin and the patch*/…/*.gz files): u32 chunk size (0x10000), u32 chunk count, u32 inflated length,
    u32 stored length of each chunk, then the chunks at 0x80 alignment, each a u32 length + one zlib stream, or the plain bytes when the stored length equals the chunk's inflated length (a short last
    chunk, or one that does not compress). → the inflated data, or None when d is not in that form"""
    import zlib
    if len(d) < 16: return None
    cs, n, tot = struct.unpack_from('<III', d)
    if cs != 0x10000 or not n or n != (tot + cs - 1) // cs or 12 + 4 * n > len(d): return None
    sizes = struct.unpack_from(f'<{n}I', d, 12); o = (12 + 4 * n + 0x7f) & ~0x7f; out = []
    for i, s in enumerate(sizes):
        if o + s > len(d): return None
        if s == min(cs, tot - i * cs): out.append(bytes(d[o:o + s]))
        else:
            ln = struct.unpack_from('<I', d, o)[0]
            try: out.append(zlib.decompress(d[o + 4:o + 4 + ln]))
            except zlib.error: return None
        o = (o + s + 0x7f) & ~0x7f
    return b''.join(out)
