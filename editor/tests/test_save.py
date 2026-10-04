import tempfile
import unittest
from pathlib import Path

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
        target = ROOT / 'bin' / 'ivxedit-test.ivx'
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


if __name__ == '__main__':
    unittest.main()
