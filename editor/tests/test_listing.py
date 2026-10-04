import random
import unittest

from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, ph_text, rule_line, words
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.rules import Instr, Rule, rep_split
from tests.fuzz import edit, ph_ids, reads_back
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

    def test_semicolon_phoneme_is_not_the_separator(self):
        # the phoneme named ';' (b'\0;') used to collide with the ' ; ' instruction separator
        f = load(ivx_bytes('sw'))
        by = {bytes(p.name).lstrip(b'\0'): p for p in f.phonemes}
        a, semi, r, b = (PhRef(by[n]) for n in (b'A', b';', b'R', b'B'))
        one = line(Instr(0xBC, [0x010C, a]), Instr(0x4C, [b]), Instr(0x10, [5]))
        two = line(Instr(0xBC, [0x0130, a, semi, r, b]), Instr(0x10, [5]))
        self.assertNotEqual(one, two)
        self.assertEqual(two, 'RULE REPL del 1 -> A ⁏ R B ; NEXT #5')
        # only the exact name ';' is escaped
        self.assertEqual(ph_text(PhRef(Phoneme(b'\0;', 0, 0, []))), '⁏')
        self.assertEqual(ph_text(PhRef(Phoneme(b';1', 0, 0, []))), ';1')

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
        self.assertEqual(line(Instr(0xBC, [0x0001])), 'RULE REPL #0001')

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

    def test_repl_words_print_distinctly(self):
        texts = set()
        for w in range(0x2000):
            d, n = rep_split(w)
            text = line(Instr(0xBC, [w] + [PhRef(A)] * n))
            texts.add(text)
        self.assertEqual(len(texts), 0x2000)

    def test_ld_st_words_print_distinctly(self):
        rng = random.Random(2)
        texts_by_args = {}
        for _ in range(3000):
            op = rng.choice([0x100, 0x104, 0x108, 0x10C])
            spec = rng.randrange(0x10000)
            index = rng.randrange(0x10000)
            args = [spec] if spec & 0x80 else [spec, index]
            text = line(Instr(op, args))
            key = (op, tuple(args))
            if key in texts_by_args:
                self.assertEqual(text, texts_by_args[key], msg=f"Duplicate text for {key}")
            else:
                texts_by_args[key] = text
        # Check no two distinct (op, args) give the same text
        texts = {}
        for (op, args), text in texts_by_args.items():
            if text not in texts:
                texts[text] = (op, args)
            else:
                self.fail(f"Text '{text}' produced by both {texts[text]} and {(op, args)}")

    def test_never_raises(self):
        rng = random.Random(1)
        ops = list(MNEMONICS) + [0x02, 0x1234]
        for _ in range(3000):
            args = [rng.choice([rng.randrange(0x10000), PhRef(A), PhRef(None), -1, 0x12345])
                    for _ in range(rng.randrange(6))]
            rule_line(Rule(False, [Instr(rng.choice(ops), args)]))


NAMES = ph_names([A, PLUS, SPACE, Phoneme(b'\0;', 0, 0, [])])


def again(rule):
    """rule -> its line -> the rule read back"""
    return parse_rule(rule_line(rule).text, NAMES)


class ParseTest(unittest.TestCase):
    def test_every_shipped_rule_reads_back(self):
        # spec: Testing, 5
        n = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for name, m in f.modules.items():
                for i, r in enumerate(m.rules):
                    n += 1
                    with self.subTest(lang=lang, module=name, rule=i):
                        self.assertEqual(words(parse_rule(rule_line(r).text, names)), words(r))
        self.assertEqual(n, 15564)

    def test_operand_kinds_read_back(self):
        for ins in (Instr(0x40, [PhRef(A)]), Instr(0x4C, [PhRef(SPACE)]), Instr(0x4C, [PhRef(None)]),
                    Instr(0x4C, [PhRef(A, 4)]), Instr(0x4C, [0x7000]), Instr(0xC4, [0x0010, 0, 0x0110, 0]),
                    Instr(0xBC, [0x010C, PhRef(A)]), Instr(0xBC, [0x0314] + [PhRef(A)] * 23),
                    Instr(0x150, [0xFFFB]), Instr(0x188, [3, 2]), Instr(0x100, [0x0005, 0x0000]),
                    Instr(0x108, [0x4005, 0xFC80]), Instr(0x10C, [0x80AC]), Instr(0x100, [0x0005, 0x0203]),
                    Instr(0x68, [0x8004, PhRef(PLUS)]), Instr(0x6C, [0x037C, 0x400A, 0x0001, 0, 0x0003, 0]),
                    Instr(0x68, [0xA00A, PhRef(PLUS), 0xC018, 10, 0x0500]),
                    Instr(0x68, [0x2006, 0x05C6, 0x0533]), Instr(0x68, [0xA004, PhRef(PLUS)]),
                    Instr(0xBC, [0x0130, PhRef(A), PhRef(NAMES['⁏']), PhRef(PLUS), PhRef(SPACE)]),
                    Instr(0xCC, []), Instr(0x10, [7])):
            for iterative in (False, True):
                rule = Rule(iterative, [ins, Instr(0x10, [32])])
                with self.subTest(line=rule_line(rule).text):
                    self.assertEqual(words(again(rule)), words(rule))

    def test_refusals(self):
        for text in ('', 'RULE ', 'RULE  SEEK A:', 'RULE SEEK A: ', 'RULE SEEK', 'RULE SEEK Q',
                     'RULE SEEK A: A:', 'RULE NEXT', 'RULE NEXT #70000', 'RULE A= 70000', 'RULE ?1234',
                     'RULE FROB', 'RULE REPL del 0 ->' + ' A:' * 64, 'RULE REPL del 300 -> A:', 'RULE REPL #0124 A:',
                     'RULE LD F1', 'RULE LD v80@0', 'RULE LD F1@64', 'RULE LD Q1@0', 'RULE ST vac t=0',
                     'RULE RP 300,0 { + }', 'RULE FOCUSP { #8004 }', 'RULE FOCUSP { <F1 ~ 5> }',
                     'RULE SETF [0010/0110]', 'RULE A= +', 'RULE SEEK A:+x', 'RULE* ', 'rule SEEK +'):
            with self.subTest(text=text):
                with self.assertRaises(ParseError):
                    parse_rule(text, NAMES)

    def test_error_column(self):
        with self.assertRaises(ParseError) as cm:
            parse_rule('RULE SEEK A: ; R Q ; NEXT #1', NAMES)
        self.assertEqual(cm.exception.pos, 17)
        self.assertIn("no phoneme 'Q'", str(cm.exception))

    def test_random_edits(self):
        # spec: Testing, 7: an edited line gives a ParseError or a rule whose bytes read back
        rng = random.Random(7)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names, ph_id = ph_names(f.phonemes), ph_ids(f)
            rules = [r for m in f.modules.values() for r in m.rules]
            for _ in range(400):
                text = edit(rng, rule_line(rng.choice(rules)).text)
                try:
                    rule = parse_rule(text, names)
                except ParseError:
                    continue
                with self.subTest(lang=lang, text=text):
                    self.assertTrue(reads_back(rule, ph_id))


if __name__ == '__main__':
    unittest.main()
