import os
import tempfile
import unittest
from pathlib import Path

from ivxedit.checks import check
from ivxedit.model import Phoneme, load
from ivxedit.writer import SaveRefused, save, to_bytes
from tests.util import ROOT, ivx_bytes


class SaveTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_save_and_backup(self):
        data = ivx_bytes('sw')
        f, out = load(data), self.dir / 'sw.ivx'
        save(f, out)
        self.assertEqual(out.read_bytes(), data)
        f.phonemes.insert(1, Phoneme(b'\x01\x01', 0, 0, []))
        save(f, out)
        self.assertEqual((self.dir / 'sw.ivx.bak').read_bytes(), data)
        self.assertEqual(out.read_bytes(), to_bytes(f))
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ['sw.ivx', 'sw.ivx.bak'])

    def test_refuses_bin(self):
        target = ROOT / 'bin' / 'nonexistent' / 'ivxedit-test.ivx'
        with self.assertRaises(PermissionError):
            save(load(ivx_bytes('sw')), target)
        self.assertFalse(target.exists())

    def test_refused_save_writes_nothing(self):
        f = load(ivx_bytes('sw'))
        f.phonemes[1].items[0].value = 70000                   # A:'s va3, a 16-bit variable
        with self.assertRaises(SaveRefused) as cm:
            save(f, self.dir / 'x.ivx')
        self.assertTrue(cm.exception.problems)
        self.assertEqual(list(self.dir.iterdir()), [])

    def test_save_refuses_a_file_that_does_not_reload(self):
        f = load(ivx_bytes('sw'))
        ins = next(x for m in f.modules.values() for r in m.rules for x in r.instrs
                   if x.op == 0x100 and len(x.args) == 2)
        ins.args.append(0x5555)                                # one operand too many for an LD
        self.assertEqual(check(f), [])                         # check() cannot see this one
        with self.assertRaises(SaveRefused):
            save(f, self.dir / 'x.ivx')
        self.assertEqual(list(self.dir.iterdir()), [])


    def test_a_warning_does_not_stop_a_save(self):
        f = load(ivx_bytes('sw'))
        f.phonemes[2].name = f.phonemes[1].name
        self.assertTrue(check(f) and all(p.warning for p in check(f)))
        save(f, self.dir / 'x.ivx')
        self.assertEqual((self.dir / 'x.ivx').read_bytes(), to_bytes(f))

    @unittest.skipUnless(os.name == 'posix', 'permission bits are POSIX')
    def test_saved_file_mode(self):
        out = self.dir / 'sw.ivx'
        umask = os.umask(0o022)
        try:
            save(load(ivx_bytes('sw')), out)
            self.assertEqual(out.stat().st_mode & 0o777, 0o644)
            out.chmod(0o640)
            save(load(ivx_bytes('sw')), out)
            self.assertEqual(out.stat().st_mode & 0o777, 0o640)
        finally:
            os.umask(umask)

if __name__ == '__main__':
    unittest.main()
