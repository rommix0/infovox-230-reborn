import unittest

from ivxedit.checks import check, errors
from ivxedit.model import PNAMES, ParamItem, PhRef, Phoneme, VarItem, load
from ivxedit.rules import REPL, Instr
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

    def test_inventory_size_limit(self):
        f = sw()
        size = lambda: 2 + sum(len(p.to_bytes()) for p in f.phonemes)
        new = lambda: Phoneme(bytes([1 + len(f.phonemes) // 250, 1 + len(f.phonemes) % 250]), 0, 0, [])
        while size() + 12 <= 0x8000:
            f.phonemes.append(new())
        self.assertEqual(check(f), [])
        f.phonemes.append(new())
        self.assertIn('inventory', [p.where for p in check(f)])

    def test_required_modules(self):
        f = sw()
        del f.modules['M5']
        probs = check(f)
        self.assertTrue(any(p.where == 'modules' and p.message.startswith('M5') for p in probs))
        f = sw()
        del f.modules['M4']
        self.assertTrue(any(p.where == 'modules' and 'M3 needs M4' in p.message for p in check(f)))

    def test_repl_count_byte(self):
        f = sw()
        ins = next(x for m in f.modules.values() for r in m.rules for x in r.instrs
                   if x.op == REPL and x.args[0] == 0x10C and len(x.args) == 2)
        ins.args[0] = 0x10D
        self.assertTrue(any('is not for' in p.message for p in check(f)))

    def test_malformed_repl_does_not_raise(self):
        f = sw()
        ins = next(x for m in f.modules.values() for r in m.rules for x in r.instrs if x.op == REPL)
        ins.args = []
        self.assertTrue(check(f))


    def test_names(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        for bad in (b'\0\0', b'A\0', b'ABC', 'A:'):
            a.name = bad
            with self.subTest(name=bad):
                self.assertEqual([(p.target, p.warning) for p in check(f)], [(('phoneme', a), False)])
        e = next(p for p in f.phonemes if p.name == b'E:')
        a.name = b'E:'                          # a duplicate: the warning is on the later one
        probs = check(f)
        self.assertEqual([(p.target, p.warning) for p in probs], [(('phoneme', e), True)])
        self.assertIn("also phoneme %d's" % f.phonemes.index(a), probs[0].message)
        self.assertEqual(errors(probs), [])
        a.name = b'\0@'                         # the defaults record's name
        probs = check(f)
        self.assertEqual([(p.target, p.warning) for p in probs], [(('phoneme', a), True)])
        self.assertIn("also phoneme 0's", probs[0].message)

    def test_build_tag(self):
        f = sw()
        f.head = b'XX' + f.head[2:]
        self.assertEqual([p.target for p in check(f)], [('header',)])

    def test_phoneme_id_past_0x7fff(self):
        f = sw()
        ins = f.modules['M0'].rules[0].instrs[0]                 # SEEK +
        ins.args[0] = PhRef(ins.args[0].ph, 0x8000)
        self.assertEqual([p.target for p in check(f)], [('rule', 'M0', 0)])
        self.assertIn('signed 16-bit', check(f)[0].message)

    def test_rule_length(self):
        f = sw()
        f.modules['M1'].rules[3].instrs += [Instr(0x10, [5])] * 0x3FFF
        self.assertEqual([p.target for p in check(f)], [('rule', 'M1', 3)])

    def test_operand_range(self):
        f = sw()
        f.modules['M2'].rules[0].instrs.append(Instr(0x10, [0x10000]))
        self.assertEqual([p.message for p in check(f)], ['an operand does not fit 16 bits'])

    def test_table_sizes(self):
        for attr, value in (('class_lo', bytes(0x7F)), ('class_lo', bytearray(0x80)),
                            ('xlat_hi', bytes(0x101)), ('class_hi', list(bytes(0x80)))):
            f = sw()
            setattr(f, attr, value)
            with self.subTest(attr=attr, value=type(value)):
                self.assertEqual([p.target for p in check(f)], [('chars',)])
        f = sw()
        f.tables[2] = f.tables[2][:31]
        self.assertEqual([p.target for p in check(f)], [('params',)])
        f = sw()
        f.tables[1][5] = -1
        self.assertEqual([p.target for p in check(f)], [('params',)])

    def test_check_never_raises(self):
        f = sw()
        f.phonemes[1].name = None
        f.phonemes[2].items.append('junk')
        f.phonemes[3].items[0] = VarItem(0x90, 'x')
        f.lexicon.entries[5] = ('A', 'B')
        f.modules['M0'].rules[0].instrs.append(Instr(None, []))
        probs = check(f)
        self.assertEqual({p.target for p in probs if p.target[0] != 'rule'},
                         {('phoneme', f.phonemes[k]) for k in (1, 2, 3)} | {('lexicon', 5)})

    def test_targets(self):
        f = sw()
        f.lexicon.entries[1], f.lexicon.entries[2] = f.lexicon.entries[2], f.lexicon.entries[1]
        del f.modules['M5']
        f.phonemes.append(f.phonemes.pop(0))
        self.assertEqual(sorted(p.target for p in check(f)), [('lexicon', 2), ('phonemes',), ('rules',)])

if __name__ == '__main__':
    unittest.main()
