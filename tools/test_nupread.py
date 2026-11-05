"""test_nupread.py: nupread.py and nxs2.py's .nup unpacking against containers made here byte by byte as the NUP specification lays them out
(format 1.0: superblock, table of contents, packages, directories, files, children, string pool, 16-aligned extents; raw and zstd).
    python tools/test_nupread.py        (zstd cases need Python's zstandard module; they are skipped without it)"""
import hashlib, pathlib, struct, sys, tempfile, unittest

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import nupread  # noqa: E402

try:
    import zstandard
except ImportError:
    zstandard = None


def make_nup(path, packages, codec=0, flags=0):
    """packages: (title_id, version, kind, package flags, tree); a tree is {name: bytes or a subtree}. Children sorted bytewise, as producers must."""
    dirs, files = [], []

    def add_dir(name, tree):
        i = len(dirs); dirs.append([name, []])
        for k in sorted(tree, key=lambda s: s.encode()):
            v = tree[k]
            if isinstance(v, dict): dirs[i][1].append(('d', add_dir(k, v)))
            else: files.append((k, v)); dirs[i][1].append(('f', len(files) - 1))
        return i

    recs = []
    for title, ver, kind, pflags, tree in packages:
        root = add_dir('', tree)
        sub = {dirs[i][0]: i for t, i in dirs[root][1] if t == 'd'}
        recs.append((title, root, sub.get('exefs', 0xFFFFFFFF), sub.get('romfs', 0xFFFFFFFF), ver, kind, pflags))
    nd, nf = len(dirs), len(files)
    pool = bytearray()

    def s(name):
        off = len(pool); pool.extend(name.encode()); return off, len(name.encode())

    children, dir_recs = [], b''
    for name, kids in dirs:
        off, ln = s(name)
        dir_recs += struct.pack('<IIIIQ', off, ln, len(children), len(kids), 0)
        children += [i if t == 'd' else nd + i for t, i in kids]
    names = [s(name) for name, _ in files]
    head = 0x30 + 0x40 * len(recs) + 0x18 * nd + 0x50 * nf + 4 * len(children)
    toc_size = head + len(pool)
    blob_off = (0x80 + toc_size + 15) & ~15
    blob, file_recs = bytearray(), b''
    for (name, data), (off, ln) in zip(files, names):
        stored, c = data, 0
        if codec == 1 and data:
            z = zstandard.ZstdCompressor(level=3, write_content_size=True).compress(data)
            if len(z) < len(data): stored, c = z, 1
        at = blob_off + len(blob) if stored else 0
        rec = struct.pack('<IIQQQ', off, ln, len(data), at, len(stored)) + hashlib.sha256(data).digest() + bytes([c]) + bytes(15)
        file_recs += rec
        blob += stored
        blob += bytes(-len(blob) % 16)
    pkg_recs = b''.join(struct.pack('<QIIIIBBHIQI20s', t, r, e, ro, v, k, f, 0, 0, 0, 0, bytes(20)) for t, r, e, ro, v, k, f in recs)
    toc = b'TOC\x01' + struct.pack('<IQQQQQ', len(recs), nd, nf, head, len(pool), 0) + pkg_recs + dir_recs + file_recs + \
        struct.pack(f'<{len(children)}I', *children) + bytes(pool)
    assert len(toc) == toc_size
    uuid = hashlib.sha256(b''.join(struct.pack('<QIB', t, v, k) for t, _, _, _, v, k, _ in recs)).digest()[:16]
    size = blob_off + len(blob)
    sb = b'NUP\x01' + struct.pack('<HHQQ', 0x80, 1, 0x80, toc_size) + hashlib.sha256(toc).digest() + struct.pack('<QQII', blob_off, size, len(recs), flags) + uuid + bytes(32)
    pathlib.Path(path).write_bytes(sb + toc + bytes(blob_off - 0x80 - toc_size) + bytes(blob))


BASE = {'exefs': {'main': b'base main', 'main.npdm': b'npdm'}, 'romfs': {'Data': {'a.bin': b'A' * 5000, 'only_base.bin': b'base'}, 'top.txt': b'top'}}
UPDATE = {'exefs': {'main': b'update main', 'sdk': b'sdk'}, 'romfs': {'Data': {'a.bin': b'B' * 7000, 'new.bin': b''}, 'top.txt': b'top v2'}}
PKGS = [(0x0400A3A01EE4C000, 0, 0, 0, BASE), (0x0400A3A01EE4C800, 655360, 1, 0, UPDATE)]


class Reader(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.d = pathlib.Path(self.tmp.name)

    def tearDown(self): self.tmp.cleanup()

    def check_roundtrip(self, codec):
        p = self.d / 'g.nup'; make_nup(p, PKGS, codec)
        with nupread.Nup(p) as n:
            self.assertEqual([(q['kind'], q['version']) for q in n.packages], [('base', 0), ('update', 655360)])
            upd = n.packages[1]
            self.assertEqual(sorted(r for r, _ in n.walk(upd['romfs'])), ['Data/a.bin', 'Data/new.bin', 'top.txt'])
            for rel, fi in n.walk(upd['romfs']): n.extract_file(fi, self.d / 'out' / rel)
            for rel, fi in n.walk(upd['exefs']): n.extract_file(fi, self.d / 'ex' / rel)
        self.assertEqual((self.d / 'out/Data/a.bin').read_bytes(), b'B' * 7000)
        self.assertEqual((self.d / 'out/Data/new.bin').read_bytes(), b'')
        self.assertEqual((self.d / 'ex/main').read_bytes(), b'update main')
        self.assertFalse(list(self.d.rglob('*.part')))

    def test_raw(self): self.check_roundtrip(0)

    @unittest.skipUnless(zstandard, 'zstandard is not installed')
    def test_zstd(self):
        self.check_roundtrip(1)
        with nupread.Nup(self.d / 'g.nup') as n:
            self.assertIn(1, {f[6] for f in n.files}, 'the A/B files compress, so some extents are zstd')

    def expect(self, data, msg, extract=False):
        p = self.d / 'bad.nup'; p.write_bytes(data)
        with self.assertRaisesRegex(nupread.NupError, msg):
            with nupread.Nup(p) as n:
                if extract:
                    for q in n.packages:
                        for tree in (q['exefs'], q['romfs']):
                            for rel, fi in n.walk(tree): n.extract_file(fi, self.d / 'x' / str(tree) / rel)

    def test_damage(self):
        p = self.d / 'g.nup'; make_nup(p, PKGS); good = bytearray(p.read_bytes())
        self.expect(b'NOPE' + bytes(good[4:]), 'not a NUP container')
        self.expect(bytes(good[:-16]), 'does not match the file')
        toc = bytearray(good); toc[0x80 + 0x40] ^= 1; self.expect(bytes(toc), 'hash mismatch')
        flags = bytearray(good); flags[0x4C] = 2; self.expect(bytes(flags), 'newer NUP version')
        blob_off = struct.unpack_from('<Q', good, 0x38)[0]
        body = bytearray(good); body[blob_off] ^= 0xFF; self.expect(bytes(body), 'checksum', extract=True)

    def test_names(self):
        for bad in ('..', 'a/b', 'a\\b', 'x\x01'):
            p = self.d / 'n.nup'; make_nup(p, [(1, 0, 0, 0, {'romfs': {bad: b'x'}})])
            with self.assertRaisesRegex(nupread.NupError, 'cannot be used as a file name'):
                with nupread.Nup(p) as n: list(n.walk(n.packages[0]['romfs']))

    def test_unknown_codec_and_kind(self):
        p = self.d / 'g.nup'; make_nup(p, [(1, 0, 7, 0, {'romfs': {'f': b'x'}})])
        self.expect(p.read_bytes(), 'package kind 7')


class Unpack(unittest.TestCase):
    """nxs2.nup_unpack: the program the recipe knows, its RomFS with the base's files under the update's, kept between builds."""
    def test_unpack(self):
        import nxs2
        with tempfile.TemporaryDirectory() as t:
            d = pathlib.Path(t); p = d / 'Pokopia.nup'; make_nup(p, PKGS); out = d / 'Pokopia_native'; out.mkdir()
            builds, build_id = nxs2.builds, nxs2.build_id
            nxs2.builds = lambda: [('2.0.1', '2.0.1', {'main': 'upd'})]
            nxs2.build_id = lambda f: 'upd' if f.read_bytes() == b'update main' else 'base'
            try:
                dump = nxs2.nup_unpack(p, out)
                root = dump / 'updates/655360'
                self.assertEqual((root / 'romfs/Data/a.bin').read_bytes(), b'B' * 7000)       # the update's own
                self.assertEqual((root / 'romfs/Data/only_base.bin').read_bytes(), b'base')    # the base's, under it
                self.assertEqual((root / 'romfs/top.txt').read_bytes(), b'top v2')
                self.assertFalse((dump / 'base/romfs').exists(), 'only the chosen program\'s RomFS is unpacked')
                (root / 'romfs/top.txt').write_bytes(b'touched')
                self.assertEqual(nxs2.nup_unpack(p, out), dump)                                # the same .nup: nothing unpacked again
                self.assertEqual((root / 'romfs/top.txt').read_bytes(), b'touched')
                nxs2.builds = lambda: [('9.9.9', '9.9.9', {'main': 'other'})]
                p.write_bytes(p.read_bytes()); import os; os.utime(p, (1, 1))
                with self.assertRaisesRegex(nxs2.Fail, 'this recipe is for'): nxs2.nup_unpack(p, out)
            finally:
                nxs2.builds, nxs2.build_id = builds, build_id


if __name__ == '__main__':
    unittest.main(verbosity=2)
