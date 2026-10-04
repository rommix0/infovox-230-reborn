import random
import unittest

from ivxedit.listing import MNEMONICS, rule_line
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.rules import Instr, Rule
from tests.util import LANGS, ivx_bytes

A = Phoneme(b'A:', 0, 0, [])
PLUS = Phoneme(b'\0+', 0, 0, [])
SPACE = Phoneme(b'\0 ', 0, 0, [])


def line(*instrs, iterative=False):
    return rule_line(Rule(iterative, list(instrs))).text


class ListingTest(unittest.TestCase):
    def test_swedish_rule_32(self):
        # REFERENCE §5.6: swrules M0 rule #32
        f = load(ivx_bytes('sw'))
        texts = [rule_line(r).text for r in f.modules['M0'].rules]
        self.assertIn('RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32', texts)

    def test_operand_kinds(self):
        self.assertEqual(line(Instr(0x40, [PhRef(A)]), Instr(0x10, [7]), iterative=True),
                         'RULE* SEEK A: ; NEXT #7')
        self.assertEqual(line(Instr(0x4C, [PhRef(SPACE)]), Instr(0x4C, [PhRef(None)]),
                              Instr(0x4C, [PhRef(A, 4)]), Instr(0x4C, [0x7000])),
                         'RULE R ␠ ; R <end> ; R A:+4 ; R #7000')
        self.assertEqual(line(Instr(0xC4, [0x0010, 0, 0x0110, 0])), 'RULE SETF [00100000/01100000]')
        self.assertEqual(line(Instr(0xBC, [0x010C, PhRef(A)])), 'RULE REPL del 1 -> A:')
        self.assertEqual(line(Instr(0xBC, [0x0314] + [PhRef(A)] * 23)),
                         'RULE REPL del 2 -> ' + ' '.join(['A:'] * 23))      # a carrying word
        self.assertEqual(line(Instr(0x150, [0xFFFB]), Instr(0x188, [3, 2])), 'RULE A= -5 ; A*=frac 3/2^2')

    def test_ld_st(self):
        self.assertEqual(line(Instr(0x100, [0x0005, 0x0000])), 'RULE LD F1@0')
        self.assertEqual(line(Instr(0x108, [0x4005, 0xFC80])), 'RULE STu F1@-2.hi t=64')
        self.assertEqual(line(Instr(0x10C, [0x80AC])), 'RULE ST vac t=-128')
        self.assertEqual(line(Instr(0x100, [0x0005, 0x0203])), 'RULE LD F1@#0203')

    def test_patterns(self):
        self.assertEqual(line(Instr(0x68, [0x8004, PhRef(PLUS)])), 'RULE FOCUSP { + }')
        self.assertEqual(line(Instr(0x6C, [0x037C, 0x400A, 0x0001, 0, 0x0003, 0])),
                         'RULE RP 3,124 { [00010000/00030000] }')
        self.assertEqual(line(Instr(0x68, [0xA00A, PhRef(PLUS), 0xC018, 10, 0x0500])),
                         'RULE FOCUSP { + <vc0 < 10> <F1 -> R0> }')
        self.assertEqual(line(Instr(0x68, [0x2006, 0x05C6, 0x0533])),
                         'RULE FOCUSP { <F1.last.hi -> R1> <#0533> }')
        # header bit 13 with no tests: the parts do not give the header, so it is raw
        self.assertEqual(line(Instr(0x68, [0xA004, PhRef(PLUS)])), 'RULE FOCUSP { #a004 + }')

    def test_unshaped_instructions_are_raw(self):
        self.assertEqual(line(Instr(0x40, [1, 2])), 'RULE SEEK #0001 #0002')
        self.assertEqual(line(Instr(0x1234, [PhRef(A)])), 'RULE ?1234 A:')
        self.assertEqual(line(Instr(0xBC, [0x0124, PhRef(A)])), 'RULE REPL #0124 A:')

    def test_spans(self):
        ln = rule_line(Rule(False, [Instr(0x40, [PhRef(PLUS)]), Instr(0x10, [32])]))
        self.assertEqual([(ln.text[s:e], k) for s, e, k, _ in ln.spans],
                         [('RULE', 'op'), ('SEEK', 'op'), ('+', 'ph'), ('NEXT', 'op'), ('#32', 'num')])
        self.assertIs(ln.spans[2][3].ph, PLUS)

    def test_every_shipped_rule_has_typed_operands(self):
        n = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            for name, m in f.modules.items():
                for i, r in enumerate(m.rules):
                    ln = rule_line(r)
                    n += 1
                    kinds = {k for _, _, k, _ in ln.spans}
                    bad = [ln.text[s:e] for s, e, k, _ in ln.spans
                           if k == 'raw' or (k == 'op' and ln.text[s] == '?')]
                    with self.subTest(lang=lang, module=name, rule=i):
                        self.assertEqual(bad, [])
                        self.assertNotIn('#', ''.join(ln.text[s:e] for s, e, k, _ in ln.spans if k == 'param'))
                        self.assertTrue(kinds <= {'op', 'ph', 'num', 'feat', 'param'})
        self.assertEqual(n, 15564)

    def test_never_raises(self):
        rng = random.Random(1)
        ops = list(MNEMONICS) + [0x02, 0x1234]
        for _ in range(3000):
            args = [rng.choice([rng.randrange(0x10000), PhRef(A), PhRef(None), -1, 0x12345])
                    for _ in range(rng.randrange(6))]
            rule_line(Rule(False, [Instr(rng.choice(ops), args)]))


if __name__ == '__main__':
    unittest.main()
