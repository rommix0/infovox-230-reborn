"""Rule notation (spec: Rule notation; Testing, 6-8)."""
import random
import sys
import unittest

from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, rule_line, words
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.notation import notation_line, parse_notation
from ivxedit.rules import Instr, Rule, rep_carries
from ivxedit.speak import speak_model
from tests.fuzz import edit, ph_ids, reads_back
from tests.util import LANGS, ROOT, ivx_bytes, ivxsay

# rules in notation over the 12 files, per module: the test fails below these (raise them
# as coverage grows)
MINIMUM = {'M0': 5691, 'M1': 1404, 'M2': 3881, 'M3': 878, 'M4': 608, 'M5': 3064}
SAY = ivxsay()

PH = {n: Phoneme(n.encode().rjust(2, b'\0'), 0, 0, []) for n in ('A', 'B', 'S', '+', '1', ' ', '[', '#', '*')}
NAMES = ph_names(PH.values())


def r(text):
    """an instruction-form line over the phonemes in PH"""
    return parse_rule(text, NAMES)


def nota(text):
    return notation_line(r(text), NAMES).text


def sw():
    f = load(ivx_bytes('sw'))
    return f, ph_names(f.phonemes)


class ExamplesTest(unittest.TestCase):
    def test_spec_examples(self):
        # spec: Rule notation, the swrules.ivx examples
        f, names = sw()
        texts = {rule_line(x).text: getattr(notation_line(x, names), 'text', None) for x in f.modules['M0'].rules}
        self.assertEqual(texts['RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32'], '#32 + B → S')
        self.assertEqual(texts['RULE SCAN 1 ; FOCUS A ; REPL del 1 -> A ; SETF [00100000/01100000] ; NEXT #32'],
                         '#32 A → A [+b20 −b24] / 1 _')
        self.assertEqual(texts['RULE SEEK A ; R D ; R R ; R E ; R S ; R S ; CPREV ; LP 3,0 { [00010000/00010000] } ;'
                               ' REPL del 0 -> # ; NEXT #818'], '#818 ∅ → # / [+b16]{3} ⟵ _ A D R E S S')

    def test_notation_md_examples(self):
        # NOTATION.md's examples: each notation line, then its instruction form
        f, names = sw()
        doc = (ROOT / 'editor' / 'ivxedit' / 'NOTATION.md').read_text(encoding='utf-8')
        lines = doc.split('```text\n')[1].split('```')[0].splitlines()
        self.assertEqual(len(lines), 20)
        for text, instr in zip(lines[::2], lines[1::2]):
            with self.subTest(text=text):
                self.assertEqual(notation_line(parse_rule(instr.strip(), names), names).text, text)

    def test_elements(self):
        self.assertEqual(nota('RULE SEEK A ; R? B ; RF [00010000/00030000] ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ (B) [+b16 −b17]')
        self.assertEqual(nota('RULE SEEK A ; RP 0,127 { B } ; RP 2,0 { + } ; RP 1,5 { 1 } ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ B* +{2} 1{1,5}')
        self.assertEqual(nota('RULE SEEK A ; RP 1,0 { B [00010000/00010000] <F1 < 50> <DR -> R1> } ; RP 1,0 { } ;'
                              ' REPL del 1 -> S ; NEXT #1'), '#1 A → S / _ B[+b16]<F1 < 50><DR → R1> ·')
        self.assertEqual(nota('RULE SEEK A ; RF [00010000/00000000] ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ [V=00010000 M=00000000]')
        self.assertEqual(nota('RULE SEEK ␠ ; R [ ; R # ; R * ; REPL del 4 -> + ; NEXT #1'), '#1 ␠ ‹[› # ‹*› → +')

    def test_every_encoding_has_its_own_spelling(self):
        # spec: Rule notation, "Every encoding choice has its own spelling"
        pairs = [
            ('RULE SEEK A ; R B ; REPL+ del 1 -> S ; NEXT #1', '#1 B →⁺ S / A _'),
            ('RULE SCAN A ; FOCUS B ; REPL del 1 -> S ; NEXT #1', '#1 B → S / A _'),
            ('RULE FOCUS A ; REPL del 1 -> S ; NEXT #1', '#1 A → S / ^ _'),
            ('RULE SEEK A ; REPL del 1 -> S ; NEXT #1', '#1 A → S'),
            ('RULE FOCUSP { A } ; REPL del 1 -> S ; NEXT #1', '#1 A{1} → S / ^ _'),
            ('RULE SEEK A ; RP 1,0 { B } ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ B{1}'),
            ('RULE SEEK A ; R B ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ B'),
            ('RULE SEEK A ; SCAN B ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ … B'),
            ('RULE SCAN B ; SEEK A ; REPL del 1 -> S ; NEXT #1', '#1 A → S / B … _'),
            ('RULE SEEK A ; UNWRAP 1 ; NEXT #1', '#1 A → ∅ ↺'),
            ('RULE SEEK A ; REPL del 1 -> ; NEXT #1', '#1 A → ∅'),
            ('RULE SEEK A ; SETF [00010000/00010000] ; NEXT #1', '#1 A → [+b16]'),
            ('RULE SEEK A ; R B ; SETF+ [00010000/00010000] ; NEXT #1', '#1 B →⁺ [+b16] / A _'),
            ('RULE SEEK A ; R B ; REPL del 1 -> S ; SETF+ [00010000/00010000] ; NEXT #1',
             '#1 A → S ; B →⁺ [+b16]'),
            ('RULE* SEEK A ; REPL del 1 -> S ; NEXT #1', '*#1 A → S'),
            ('RULE SEEK A ; REPL del 1 -> S', 'A → S once'),
            ('RULE* SEEK A ; REPL del 1 -> S', '* A → S once'),
            ('RULE SEEK A ; A= 5 ; ST vac ; NEXT #1', '#1 A → { vac := 5 }'),
            ('RULE SEEK A ; A= 5 ; STu F1@0 ; ST vac ; NEXT #1', '#1 A → { F1@0 :=u 5; vac := A }'),
            ('RULE SEEK A ; LD F1@-1.hi t=2 ; A=B ; A*=frac 3/2^2 ; STs F1@0 ; NEXT #1',
             '#1 A → { B = F1@-1.hi t=2; A=B; A*=frac 3/2^2; F1@0 :=s A }'),
            ('RULE SEEK A ; CPREV ; L B ; L? 1 ; LP 0,127 { + } ; REPL del 1 -> S ; NEXT #1',
             '#1 A → S / +* (1) B ⟵ _'),
        ]
        for text, want in pairs:
            with self.subTest(text=text):
                self.assertEqual(nota(text), want)
                self.assertEqual(words(parse_notation(want, NAMES)), words(r(text)))
        self.assertEqual(len({w for _, w in pairs}), len(pairs))

    def test_not_in_notation(self):
        for text in ('RULE GEMIN #1', 'RULE SEEK A ; CHEAD ; R B ; REPL del 1 -> S ; NEXT #1',
                     'RULE SEEK A ; R <end> ; REPL del 1 -> S ; NEXT #1',
                     'RULE SEEK A ; REPL del 2 -> ' + 'S ' * 22 + '; NEXT #1',     # 12*n carries
                     'RULE SEEK A ; REPL del 2 -> S ; NEXT #1',          # deletes more than it matched
                     'RULE SEEK A ; L B ; NEXT #1', 'RULE SEEK A ; NEXT #1', 'RULE R A ; R B ; NEXT #1'):
            with self.subTest(text=text):
                self.assertIsNone(notation_line(r(text), NAMES))

    def test_never_raises(self):
        # hand-built rules, whatever their operands: a Line or None
        rng = random.Random(1)
        ops = list(MNEMONICS)
        for _ in range(3000):
            instrs = [Instr(rng.choice(ops), [rng.choice([rng.randrange(0x10000), PhRef(PH['A']), PhRef(None), -1])
                                              for _ in range(rng.randrange(6))]) for _ in range(rng.randrange(1, 5))]
            notation_line(Rule(False, instrs), NAMES)

    def test_refusals(self):
        for text in ('', '#1', '#1 A', '#1 A →', '#1 A → S /', '#1 A → S / _ _', '#1 A → Q',
                     'A → S', '#1 A → S once', '#1 A B → [+b1]', '#1 A → [+b32]', '#1 A → [+b1 −b1]',
                     '#1 A(B) → S', '#1 (A) → S', '#1 A* → S', '#1 A → S / ^ … B _', '#1 ∅ → S',
                     '#1 A → S / … B _', '#1 B[+b1] → S', '#1 A → S ; B → S', '#1 A →⁺ S',
                     '#1 A → S / B ⟵ C _', '#1 A → { A = 5 }', '#1 A → { LD F1@0 }', '#1 A → { F1 := 5 }',
                     '#1 A → S / _ B{300}', '#99999 A → S', '#1 A → ∅ ↺ [+b1]', '#1 ∅ → ∅ ↺ / _ A',
                     '#1 A → ' + 'S ' * 22,
                     '#' + '9' * 5000 + ' A → S', '#1 A → S / _ B{' + '9' * 5000 + '}',
                     '#1 A → [+b' + '9' * 5000 + ']'):
            with self.subTest(text=text):
                with self.assertRaises(ParseError):
                    parse_notation(text, NAMES)


class ShippedTest(unittest.TestCase):
    def test_coverage(self):
        # spec: Testing, 6: every rule shown in notation reads back; coverage per module
        done, total = dict.fromkeys(MINIMUM, 0), dict.fromkeys(MINIMUM, 0)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for name, m in f.modules.items():
                for i, rule in enumerate(m.rules):
                    total[name] += 1
                    line = notation_line(rule, names)
                    if line is not None:
                        done[name] += 1
                        with self.subTest(lang=lang, module=name, rule=i):
                            self.assertEqual(words(parse_notation(line.text, names)), words(rule))
        print('\nrules in notation: ' + ', '.join('%s %d/%d' % (n, done[n], total[n]) for n in sorted(total)),
              file=sys.stderr)
        for name in MINIMUM:
            self.assertGreaterEqual(done[name], MINIMUM[name], name)

    def test_what_is_not_in_notation(self):
        # the 38 shipped rules in instruction form: GEMIN, a REPL word whose count carries,
        # a reference to the inventory's terminator, CHEAD or CTAIL
        why = {}
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for m in f.modules.values():
                for rule in m.rules:
                    if notation_line(rule, names) is None:
                        ops = {i.op for i in rule.instrs}
                        reason = ('GEMIN' if 0xC8 in ops else 'CHEAD/CTAIL' if ops & {0xD0, 0xD4} else
                                  'carry' if any(i.op in (0xB8, 0xBC) and rep_carries(i.args[0]) for i in rule.instrs)
                                  else 'terminator' if any(isinstance(a, PhRef) and a.ph is None
                                                           for i in rule.instrs for a in i.args) else 'other')
                        why[reason] = why.get(reason, 0) + 1
        self.assertEqual(why, {'GEMIN': 23, 'CHEAD/CTAIL': 2, 'carry': 5, 'terminator': 8})

    def test_random_edits(self):
        # spec: Testing, 7: an edited line gives a ParseError or a rule whose bytes read back
        rng = random.Random(8)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names, ph_id = ph_names(f.phonemes), ph_ids(f)
            rules = [x for m in f.modules.values() for x in m.rules]
            n = 0
            while n < 400:
                line = notation_line(rng.choice(rules), names)
                if line is None:
                    continue
                n += 1
                text = edit(rng, line.text)
                try:
                    rule = parse_notation(text, names)
                except ParseError:
                    continue
                with self.subTest(lang=lang, text=text):
                    self.assertTrue(reads_back(rule, ph_id))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class EndToEndTest(unittest.TestCase):
    def test_a_rule_added_in_notation(self):
        # spec: Testing, 8: an M0 respelling changes what ivxsay reports
        f, names = sw()

        def phonemes(word):
            return [e.value for e in speak_model(f, word, exe=SAY).phonemes]
        self.assertEqual(phonemes('pilo'), ['', 'Ph', 'I:', 'L', 'O', ',', '.'])
        f.modules['M0'].rules.insert(0, parse_notation('#1 L → M / _ O', names))
        self.assertEqual(phonemes('pilo'), ['', 'Ph', 'I:', 'M', 'O', ',', '.'])
        self.assertEqual(phonemes('pila'), ['', 'Ph', 'I:', 'L', 'A', ',', '.'])


if __name__ == '__main__':
    unittest.main()
