import struct
import unittest

from ivxedit.binfmt import INV
from ivxedit.model import (PNAMES, ParamItem, PhRef, VarItem, decode_inventory,
                           resolve_refs)
from ivxedit.rules import decode_module, ph_positions
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import


def body(lang):
    return ivx_bytes(lang)[0x200:]


class InventoryTest(unittest.TestCase):
    def test_records_round_trip(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                b = body(lang)
                phs, offs, end = decode_inventory(b)
                self.assertEqual(b''.join(p.to_bytes() for p in phs) + b'\0\0', b[INV:end])
                self.assertTrue(phs[0].is_defaults)
                self.assertFalse(any(p.is_defaults for p in phs[1:]))
                self.assertEqual(offs[0], 0)

    def test_counts_match_ivx_py(self):
        (ivx,) = tools_import('ivx')
        for lang in LANGS:
            with self.subTest(lang=lang):
                phs = decode_inventory(body(lang))[0]
                self.assertEqual(len(phs), len(ivx.Ivx(str(ivx_path(lang))).chain()))

    def test_swedish_long_a(self):
        # REFERENCE §5.4: A0=24@0 F1=165@*/165@2 F2=87@*/83@2 ... FD=100@* va5=40 vac=110 vad=30
        phs = decode_inventory(body('sw'))[0]
        a = next(p for p in phs if p.name == b'A:')
        params = {PNAMES[i.pid]: i.points for i in a.items if isinstance(i, ParamItem)}
        vars_ = {i.vid: i.value for i in a.items if isinstance(i, VarItem)}
        self.assertEqual(params['A0'], [(0, 24)])
        self.assertEqual(params['F1'], [(-1, 165), (2, 165)])
        self.assertEqual(params['F2'], [(-1, 87), (2, 83)])
        self.assertEqual(params['FD'], [(-1, 100)])
        self.assertEqual((vars_[0xA5], vars_[0xAC], vars_[0xAD]), (40, 110, 30))

    def test_one_char_names_have_a_leading_nul(self):
        names = [p.name for p in decode_inventory(body('sw'))[0]]
        self.assertIn(b'\x00+', names)

    def test_resolve_refs(self):
        terminator_refs = {}
        for lang in LANGS:
            with self.subTest(lang=lang):
                b = body(lang)
                H = struct.unpack_from('>16I', b, 0)
                phs, offs, end = decode_inventory(b)
                mods = {s: decode_module(b, H[s])[0] for s in (5, 6, 9, 10, 11, 12) if H[s]}
                resolve_refs(mods, phs, offs, end - 2 - INV)
                for m in mods.values():
                    for r in m.rules:
                        for ins in r.instrs:
                            for k in ph_positions(ins):
                                a = ins.args[k]
                                self.assertIsInstance(a, PhRef)
                                self.assertEqual(a.delta, 0)
                                if a.ph is None:
                                    terminator_refs[lang] = terminator_refs.get(lang, 0) + 1
        self.assertEqual(terminator_refs, {'fr': 1, 'it': 7})


if __name__ == '__main__':
    unittest.main()
