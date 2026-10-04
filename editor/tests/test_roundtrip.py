import unittest

from ivxedit.model import PNAMES, load
from ivxedit.writer import to_bytes
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import


class RoundTripTest(unittest.TestCase):
    def test_all_files_byte_identical(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                self.assertEqual(to_bytes(load(data)), data)

    def test_header_matches_ivx_py(self):
        (ivx,) = tools_import('ivx')
        for lang in LANGS:
            with self.subTest(lang=lang):
                self.assertEqual(load(ivx_bytes(lang)).version, ivx.Ivx(str(ivx_path(lang))).version)

    def test_parameter_tables(self):
        # REFERENCE §5.3: Swedish A3 is F1 3, F2 35, F3 59, F0 -3
        f = load(ivx_bytes('sw'))
        self.assertEqual(f.ntotal, 29)
        self.assertEqual([f.param(PNAMES.index(n))[3] for n in ('F1', 'F2', 'F3', 'F0')],
                         [3, 35, 59, -3])

    def test_sections(self):
        files = {lang: load(ivx_bytes(lang)) for lang in LANGS}
        self.assertEqual(sorted(l for l, f in files.items() if f.lexicon), ['bl', 'sw'])
        self.assertEqual(sorted(l for l, f in files.items() if f.ixlex is not None),
                         sorted(set(LANGS) - {'bl', 'sw'}))
        self.assertEqual(sorted(l for l, f in files.items() if f.xlat_hi is not None),
                         ['bl', 'da', 'du', 'fi', 'fr', 'ge', 'ic', 'sw'])
        self.assertEqual({l: f.end_pad for l, f in files.items() if f.end_pad},
                         {'am': 80, 'it': 64, 'no': 32, 'sp': 48})
        self.assertEqual(sorted(l for l, f in files.items() if f.pads.get('INV')),
                         ['da', 'fr', 'ge', 'it', 'no', 'sw'])
        self.assertEqual({l: f.h2_rel for l, f in files.items() if f.h2_rel is not None},
                         {'da': 0x1800})
        self.assertEqual(sorted(files['fi'].modules), ['M0', 'M1', 'M2', 'M5'])

    def test_lexicon(self):
        lex = load(ivx_bytes('sw')).lexicon.entries
        self.assertEqual(lex[0], (b' ', b'es'))                # REFERENCE §5.7
        self.assertIn((b'%', b"PROS'ENT"), lex)
        self.assertEqual(len(lex), 3370)


if __name__ == '__main__':
    unittest.main()
