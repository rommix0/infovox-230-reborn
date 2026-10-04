import unittest

from ivxedit.checks import check
from ivxedit.model import PNAMES, ParamItem, PhRef, load
from ivxedit.rules import REPL, Instr, ph_positions
from tests.util import LANGS, ivx_bytes


def sw():
    return load(ivx_bytes('sw'))


class ChecksTest(unittest.TestCase):
    def test_shipped_files_pass(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                self.assertEqual(check(load(ivx_bytes(lang))), [])

    def test_deleted_phoneme_still_used(self):
        f = sw()
        ins = f.modules['M0'].rules[0].instrs[0]               # SEEK +
        self.assertIsInstance(ins.args[0], PhRef)
        f.phonemes.remove(ins.args[0].ph)
        probs = check(f)
        self.assertIn('M0 rule 0 (#32)', [p.where for p in probs])
        self.assertTrue(all('not in the inventory' in p.message for p in probs))
        self.assertIn("phoneme '+'", probs[0].message)

    def test_new_repl_word_with_carry(self):
        f = load(ivx_bytes('da'))
        rule, i = next((r, i) for m in f.modules.values() for r in m.rules
                       for i, x in enumerate(r.instrs) if x.legacy)
        old = rule.instrs[i]
        rule.instrs[i] = Instr(old.op, list(old.args))         # an edit: no longer legacy
        probs = check(f)
        self.assertEqual(len(probs), 1)
        self.assertIn('at most 21', probs[0].message)

    def test_repl_word_must_match_its_phonemes(self):
        f = sw()
        ins = next(x for m in f.modules.values() for r in m.rules for x in r.instrs
                   if x.op == REPL and len(x.args) > 1)
        ins.args.append(ins.args[1])
        self.assertTrue(any('is not for' in p.message for p in check(f)))

    def test_lexicon_order(self):
        f = sw()
        e = f.lexicon.entries
        e[1], e[2] = e[2], e[1]                                  # '!!' before '!'
        probs = check(f)
        self.assertEqual([p.where for p in probs], ["lexicon entry 2 ('!')"])

    def test_point_out_of_range(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        f1 = next(i for i in a.items if isinstance(i, ParamItem) and i.pid == PNAMES.index('F1'))
        f1.points[0] = (-1, 256)
        self.assertEqual([p.where for p in check(f)], ["phoneme 'A:'"])

    def test_item_order(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        a.items.reverse()
        self.assertTrue(any('signed-id order' in p.message for p in check(f)))

    def test_defaults_must_stay_first(self):
        f = sw()
        f.phonemes.append(f.phonemes.pop(0))
        self.assertIn('inventory', [p.where for p in check(f)])


if __name__ == '__main__':
    unittest.main()
