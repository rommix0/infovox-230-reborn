import unittest
from unittest import mock

from ivxedit import model
from ivxedit.binfmt import IvxFormatError
from ivxedit.info import (a1_text, char_flags, char_rows, flags_text, header_fields, hexdump,
                          item_name, ph_name, unknown_text)
from ivxedit.model import PNAMES, load
from ivxedit.rules import ph_positions
from ivxedit.xref import phoneme_uses
from tests.util import LANGS, ivx_bytes


class InfoTest(unittest.TestCase):
    def test_names(self):
        self.assertEqual(ph_name(b'\0+'), '+')
        self.assertEqual(ph_name(b'A:'), 'A:')
        self.assertEqual(item_name(5), 'F1')
        self.assertEqual(item_name(0xAC), 'vac')
        self.assertEqual(item_name(40), 'p40')

    def test_a1_text(self):
        self.assertEqual(a1_text(18 * 4), '18: approach → freq')     # spec: GUI, Parameters
        self.assertEqual(a1_text(0x80), '32: cosine, value remapped')
        self.assertEqual(a1_text(6), '0x6: unknown')

    def test_every_a1_in_the_files_has_a_name(self):
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            for i in range(len(PNAMES)):
                with self.subTest(lang=lang, param=PNAMES[i]):
                    self.assertNotIn('unknown', a1_text(f.param(i)[1]))

    def test_char_flags(self):
        # REFERENCE §5.2: codes -6..5 select a flag word; others make a letter by position
        self.assertEqual(char_flags(ord('a'), 0x40), 0x600)
        self.assertEqual(char_flags(ord('A'), 0x40), 0xA00)
        self.assertEqual(char_flags(0xE9, 0x40), 0x200)
        self.assertEqual(char_flags(ord('%'), 0), 0x100)             # code 0: symbol
        self.assertEqual(char_flags(ord('5'), -1), 0x80)             # code -1: digit
        self.assertEqual(char_flags(ord(' '), -5), 0x01 | 0x02)      # SP: blank added
        self.assertEqual(char_flags(ord('.'), 0), 0x110)             # '.': sentence end added
        self.assertEqual(char_flags(0x0D, -5), 0x05)
        self.assertEqual(flags_text(0x1020), '0x1020: 0x20, 0x1000')
        self.assertEqual(flags_text(0x600), '0x600: letter, lower case')

    def test_char_rows(self):
        sw, ge, am = (load(ivx_bytes(l)) for l in ('sw', 'ge', 'am'))
        rows = char_rows(sw)
        self.assertEqual(len(rows), 256)
        self.assertEqual(rows[ord('A')][:2], (0x41, 'A'))
        self.assertIsNotNone(rows[0xE5][4])                          # sw has H[14]
        self.assertEqual(char_rows(am)[0xE5][2:], (None, None, None))   # am: built-in tables
        self.assertEqual([char_rows(ge)[ch][4] for ch in (0x80, 0xC4)], ['"C', ''])   # without NUL padding
        self.assertEqual(rows[0x84][4], '{')

    def test_header_fields(self):
        fields = dict(header_fields(load(ivx_bytes('sw'))))
        self.assertTrue(fields['Copyright'].startswith('Copyright (C) Solna 1988'))
        self.assertEqual(fields['Build tag'], 'SWRU981118')
        self.assertEqual(fields['Lexicon name'], 'SWULEX.IVX')
        self.assertEqual(fields['Word 0x6E (2 × parameters)'], '58')

    def test_hexdump(self):
        self.assertEqual(hexdump(b'AB\0', 0x10), '000010  41 42 00' + ' ' * 39 + '  AB.')
        self.assertEqual(len(hexdump(bytes(40)).splitlines()), 3)

    def test_unknown_text(self):
        text = unknown_text(load(ivx_bytes('da')))
        self.assertIn('  H[2]   0x0000db00', text)
        self.assertIn('H[2] points 0x1800 bytes into the indexed lexicon', text)


class XrefTest(unittest.TestCase):
    def test_uses_count_every_rule_once(self):
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            uses = phoneme_uses(f)
            with self.subTest(lang=lang):
                for ph, refs in uses.items():
                    self.assertEqual(len(refs), len(set(refs)))
                expect = set()
                for name, m in f.modules.items():
                    for i, r in enumerate(m.rules):
                        for ins in r.instrs:
                            for k in ph_positions(ins):
                                if ins.args[k].ph is not None:
                                    expect.add((id(ins.args[k].ph), name, i))
                got = {(id(ph), n, i) for ph, refs in uses.items() for n, i in refs}
                self.assertEqual(got, expect)

    def test_swedish_plus(self):
        f = load(ivx_bytes('sw'))
        plus = next(ph for ph in f.phonemes if ph.name == b'\0+')
        rule = f.modules['M0'].rules[next(i for n, i in phoneme_uses(f)[plus] if n == 'M0')]
        self.assertTrue(any(ins.args and getattr(ins.args[0], 'ph', None) is plus for ins in rule.instrs))


class LoadErrorsTest(unittest.TestCase):
    def test_load_raises_only_ivx_format_error(self):
        for exc in (KeyError('k'), OverflowError('o'), RecursionError('r'), IndexError('i')):
            with self.subTest(exc=type(exc).__name__), \
                    mock.patch.object(model, '_load', side_effect=exc):
                with self.assertRaises(IvxFormatError):
                    load(b'')


if __name__ == '__main__':
    unittest.main()
