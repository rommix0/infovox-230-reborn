import struct
import unittest

from ivxedit.rules import (Instr, decode_module, encode_module, ph_positions, rep_carries,
                           rep_split)
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import

SECTIONS = {'M0': 9, 'M1': 10, 'M2': 12, 'M3': 5, 'M4': 6, 'M5': 11}


def modules_of(lang):
    """(name, section index, body bytes, start) for each module the file has"""
    b = ivx_bytes(lang)[0x200:]
    H = struct.unpack_from('>16I', b, 0)
    return [(name, sec, b, H[sec]) for name, sec in SECTIONS.items() if H[sec]]


class RulesTest(unittest.TestCase):
    def test_rep_split(self):
        self.assertEqual(rep_split(0x0224), (2, 3))       # del 2, 3 phonemes
        self.assertEqual(rep_split(0x0314), (2, 23))      # 12*23 = 276 carries
        self.assertFalse(rep_carries(0x0224))
        self.assertTrue(rep_carries(0x0314))

    def test_ph_positions(self):
        self.assertEqual(ph_positions(Instr(0x40, [0x12D2])), [0])            # SEEK ph
        self.assertEqual(ph_positions(Instr(0xBC, [0x0118, 1, 2])), [1, 2])   # REPL del 1 -> 2
        self.assertEqual(ph_positions(Instr(0x68, [0x8004, 7])), [1])         # FOCUSP {ph}
        self.assertEqual(ph_positions(Instr(0x68, [0x4006, 0, 0, 0, 0])), [])  # FOCUSP {features}
        self.assertEqual(ph_positions(Instr(0x6C, [0x0100, 0x8004, 7])), [2])  # RP {1,0}{ph}
        self.assertEqual(ph_positions(Instr(0x150, [5])), [])                 # A = 5

    def test_every_module_round_trips(self):
        total = 0
        for lang in LANGS:
            for name, sec, b, start in modules_of(lang):
                with self.subTest(lang=lang, module=name):
                    m, end = decode_module(b, start)
                    self.assertEqual(encode_module(m, None), b[start:end])
                    self.assertEqual(m.end, [0x04, 0x00])
                    total += len(m.rules)
        self.assertEqual(total, 15564)

    def test_rule_counts_match_ivxdasm(self):
        ivx, ivxdasm = tools_import('ivx', 'ivxdasm')
        for lang in LANGS:
            x = ivx.Ivx(str(ivx_path(lang)))
            for name, sec, b, start in modules_of(lang):
                with self.subTest(lang=lang, module=name):
                    n = ivxdasm.check(x, sec)[0]
                    self.assertEqual(len(decode_module(b, start)[0].rules), n)

    def test_label_is_the_next_operand(self):
        name, sec, b, start = next(m for m in modules_of('sw') if m[0] == 'M0')
        rule = decode_module(b, start)[0].rules[0]          # SEEK + ; R B ; REPL ; NEXT #32
        self.assertEqual(rule.label, 32)
        self.assertEqual([i.op for i in rule.instrs], [0x40, 0x4C, 0xBC, 0x10])

    def test_legacy_repl_words(self):
        found = {}
        for lang in LANGS:
            for name, sec, b, start in modules_of(lang):
                for r in decode_module(b, start)[0].rules:
                    for ins in r.instrs:
                        if ins.legacy:
                            found.setdefault(lang, []).append(ins.args[0])
        self.assertEqual({k: sorted(v) for k, v in found.items()},
                         {'da': [0x314, 0x320, 0x32C, 0x32C], 'ic': [0x1108]})


if __name__ == '__main__':
    unittest.main()
