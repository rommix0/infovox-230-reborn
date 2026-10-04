"""Edits (spec: GUI, steps 4 and 5): each edit changes what it says, undo restores the bytes
exactly, and the edited file saves and loads again."""
import re
import unittest

from ivxedit import edits
from ivxedit.checks import check, errors
from ivxedit.edits import EditError
from ivxedit.info import a1_text, header_fields
from ivxedit.listing import ParseError, ph_names, rule_line, words
from ivxedit.model import PNAMES, ParamItem, VarItem, load
from ivxedit.notation import notation_line
from ivxedit.writer import to_bytes
from ivxedit.xref import phoneme_uses
from tests.util import LANGS, ivx_bytes


def sw():
    return load(ivx_bytes('sw'))


def ph(f, name):
    return next(p for p in f.phonemes if p.name == name)


def unused(f):
    """a phoneme no rule refers to"""
    uses = phoneme_uses(f)
    return next(p for p in f.phonemes[1:] if p not in uses)


def script(f):
    """one edit of each kind that applies to f, as functions of the model"""
    out = [lambda: edits.set_header_text(f, 'version', 'edited'),
           lambda: edits.set_header_text(f, 'copyright', ''),
           lambda: edits.set_header_text(f, 'lexicon name', 'X.IVX'),
           lambda: edits.set_header_flags(f, '0x1234'),
           lambda: edits.set_param(f, PNAMES.index('F1'), 'A1', '19'),
           lambda: edits.set_param(f, PNAMES.index('F2'), 'A2', '7'),
           lambda: edits.set_param(f, PNAMES.index('F0'), 'A3', '-12'),
           lambda: edits.set_class(f, ord('#'), '-3'),
           lambda: edits.rename_phoneme(f.phonemes[1], 'Q'),
           lambda: edits.set_bit(f.phonemes[2], 'feat', 31, not f.phonemes[2].feat >> 31),
           lambda: edits.set_bit(f.phonemes[2], 'mask', 0, not f.phonemes[2].mask & 1),
           lambda: edits.add_item(f.phonemes[3], 0x9F if all(
               getattr(it, 'vid', None) != 0x9F for it in f.phonemes[3].items) else 0x9E),
           lambda: edits.set_cell(f.phonemes[3], 0, 0, '-7' if isinstance(f.phonemes[3].items[0], VarItem)
                                  else '9@1'),
           lambda: edits.remove_item(f.phonemes[4], 0),
           lambda: edits.add_phoneme(f, 1, edits.free_name(f)),
           lambda: edits.add_phoneme(f, len(f.phonemes), 'Z9', f.phonemes[5]),
           lambda: edits.delete_phoneme(f, unused(f))]
    if f.class_hi is not None:
        out += [lambda: edits.set_class(f, 0xC4, '5'), lambda: edits.set_xlat(f, 0xC4, 'A:')]
    if f.lexicon is not None:
        out += [lambda: edits.add_entry(f.lexicon, 'ZZTOP', "S'ITS")[0],
                lambda: edits.set_entry(f.lexicon, 5, 'AAA', 'A')[0],
                lambda: edits.remove_entry(f.lexicon, 1)]
    return out


class EditTest(unittest.TestCase):
    def test_undo_restores_every_file(self):
        """every edit, applied in order and then undone in reverse, gives the original bytes"""
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                f = load(data)
                done = []
                for make in script(f):
                    e = make()
                    self.assertIsNotNone(e)
                    e.apply()
                    done.append(e)
                self.assertEqual(errors(check(f)), [])
                edited = to_bytes(f)
                self.assertNotEqual(edited, data)
                self.assertEqual(to_bytes(load(edited)), edited)        # saves and loads again
                for e in reversed(done):
                    e.revert()
                self.assertEqual(to_bytes(f), data)
                for e in done:                                          # redo
                    e.apply()
                self.assertEqual(to_bytes(f), edited)

    def test_header(self):
        f = sw()
        edits.set_header_text(f, 'version', 'v2 ÅÄÖ').apply()
        edits.set_header_flags(f, '0x00010002').apply()
        fields = dict(header_fields(load(to_bytes(f))))
        self.assertEqual(fields['Version'], 'v2 ÅÄÖ')
        self.assertEqual(fields['Flags'], '0x00010002')
        self.assertEqual(f.header[0x80 + 6:0xC0], bytes(0x3A))
        for field, text in (('version', 'x' * 64), ('lexicon name', 'ABCDEFGHIJKL'), ('copyright', 'a\0b'),
                            ('version', '→')):
            with self.subTest(field=field, text=text), self.assertRaises(EditError):
                edits.set_header_text(f, field, text)
        self.assertIsNone(edits.set_header_text(f, 'version', 'v2 ÅÄÖ'))

    def test_header_text_unchanged_is_no_edit(self):
        """the text a view shows, set again, changes nothing (icrules keeps bytes after the NUL)"""
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            shown = dict(header_fields(f))
            for label, field in (('Version', 'version'), ('Copyright', 'copyright'),
                                 ('Lexicon name', 'lexicon name')):
                with self.subTest(lang=lang, field=field):
                    self.assertIsNone(edits.set_header_text(f, field, shown[label]))

    def test_params(self):
        f = sw()
        i = PNAMES.index('F0')
        for column, text, want in (('A1', '18: approach → freq', 72), ('A1', '0', 0), ('A2', '255', 255),
                                   ('A3', '-5', -5), ('A3', '0x10', 16)):
            edits.set_param(f, i, column, text).apply()
            self.assertEqual(f.param(i)[1 + edits.PARAM_COLUMNS.index(column)], want)
        edits.set_param(f, i, 'A1', '0x10').apply()             # hex 16 is a valid type, stored as 16 * 4
        self.assertEqual(f.param(i)[1], 64)
        g = sw()
        self.assertIsNone(edits.set_param(g, i, 'A1', a1_text(g.param(i)[1])))   # the displayed form
        for column, text in (('A1', '33'), ('A1', 'x'), ('A2', '256'), ('A2', '-1'), ('A3', '40000'),
                             ('A3', '²'), ('A1', '0x48'), ('A1', '7.5'), ('A1', '1x')):
            with self.subTest(column=column, text=text), self.assertRaises(EditError):
                edits.set_param(f, i, column, text)

    def test_characters(self):
        f = sw()
        edits.set_class(f, 0x41, '-6').apply()
        edits.set_class(f, 0xE5, '127').apply()
        edits.set_xlat(f, 0xE5, 'a').apply()
        g = load(to_bytes(f))
        self.assertEqual((g.class_lo[0x41], g.class_hi[0x65], g.xlat_hi[0xCA:0xCC]), (0xFA, 127, b'a\0'))
        for fn, args in ((edits.set_class, (0x41, '128')), (edits.set_xlat, (0x41, 'a')),
                         (edits.set_xlat, (0xE5, 'abc'))):
            with self.subTest(args=args), self.assertRaises(EditError):
                fn(f, *args)
        it = load(ivx_bytes('it'))                                   # no H[14]
        with self.assertRaises(EditError):
            edits.set_class(it, 0xC4, '1')
        with self.assertRaises(EditError):
            edits.set_xlat(it, 0xC4, 'a')

    def test_phoneme_name(self):
        f = sw()
        a = ph(f, b'A:')
        edits.rename_phoneme(a, 'Ä').apply()
        self.assertEqual(a.name, b'\0\xc4')
        for text in ('', 'ABC', 'a\0', '→'):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.rename_phoneme(a, text)
        self.assertIsNone(edits.rename_phoneme(a, 'Ä'))

    def test_points_and_variables(self):
        f = sw()
        a = ph(f, b'A:')
        row = next(k for k, it in enumerate(a.items) if isinstance(it, ParamItem) and len(it.points) == 2)
        before = list(a.items[row].points)
        e = edits.set_cell(a, row, 1, '200@3')
        e.apply()
        self.assertEqual(a.items[row].points, [before[0], (3, 200)])
        self.assertEqual(e.old[row].points, before)                   # the old list is untouched
        edits.set_cell(a, row, 2, '201@4').apply()                   # appends
        edits.set_cell(a, row, 0, '').apply()                        # removes point 0
        self.assertEqual(a.items[row].points, [(3, 200), (4, 201)])
        self.assertIsNone(edits.set_cell(a, row, 0, '200@3'))
        for k, text in ((0, '256@0'), (0, '1@128'), (0, '1'), (5, '1@1')):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.set_cell(a, row, k, text)
        var = next(k for k, it in enumerate(a.items) if isinstance(it, VarItem))
        edits.set_cell(a, var, 0, '-300').apply()
        self.assertEqual(a.items[var].value, -300)
        with self.assertRaises(EditError):
            edits.set_cell(a, var, 0, '40000')
        self.assertEqual(errors(check(f)), [])

    def test_items_keep_their_order(self):
        f = sw()
        a = ph(f, b'A:')
        have = {getattr(it, 'vid', getattr(it, 'pid', None)) for it in a.items}
        new = [iid for iid in (PNAMES.index('XX'), PNAMES.index('B4'), 0x81, 0x90, 0xFE, 100)
               if iid not in have]
        self.assertGreaterEqual(len(new), 4)
        for iid in new:
            edits.add_item(a, iid).apply()
        self.assertEqual(errors(check(f)), [])
        with self.assertRaises(EditError):
            edits.add_item(a, new[0])
        n = len(a.items)
        edits.remove_item(a, 0).apply()
        self.assertEqual(len(a.items), n - 1)
        one = next(k for k, it in enumerate(a.items) if isinstance(it, ParamItem) and len(it.points) == 1)
        with self.assertRaises(EditError):
            edits.set_cell(a, one, 0, '')

    def test_inventory(self):
        f = sw()
        data = to_bytes(f)
        name = edits.free_name(f)
        e = edits.add_phoneme(f, 1, name)
        e.apply()
        new = f.phonemes[1]
        self.assertEqual((new.name, e.target), (name.encode(), ('phoneme', new)))
        g = load(to_bytes(f))                       # every id moved; the rules still name the same phonemes
        self.assertEqual([r.label for r in g.modules['M0'].rules], [r.label for r in f.modules['M0'].rules])
        self.assertEqual(len(g.phonemes), len(f.phonemes))
        edits.delete_phoneme(f, new).apply()
        self.assertEqual(to_bytes(f), data)
        with self.assertRaises(EditError) as cm:
            edits.delete_phoneme(f, ph(f, b'A:'))
        self.assertIn('used by', str(cm.exception))
        with self.assertRaises(EditError):
            edits.delete_phoneme(f, f.phonemes[0])
        with self.assertRaises(EditError):
            edits.add_phoneme(f, 0, 'X')
        copy = edits.add_phoneme(f, 3, 'Z9', ph(f, b'A:'))
        copy.apply()
        self.assertEqual(f.phonemes[3].to_bytes()[2:], ph(f, b'A:').to_bytes()[2:].replace(b'A:', b'Z9', 1))

    def test_lexicon_keeps_its_order(self):
        f = sw()
        lex = f.lexicon
        e, k = edits.add_entry(lex, 'BORT', 'B\'ORT')                  # a third BORT goes after the two
        e.apply()
        words = [o for o, _ in lex.entries]
        self.assertEqual(words[k - 2:k + 1], [b'BORT'] * 3)
        self.assertEqual(lex.entries[k], (b'BORT', b"B'ORT"))
        e, k = edits.set_entry(lex, 5, 'ZZZZ', 'Z')
        e.apply()
        self.assertEqual(lex.entries[k], (b'ZZZZ', b'Z'))
        self.assertTrue(lex.entries[k - 1][0] <= b'ZZZZ' < lex.entries[k + 1][0])
        e, j = edits.set_entry(lex, k, 'ZZZZ', 'Y')                   # same headword: stays put
        e.apply()
        self.assertEqual(j, k)
        self.assertEqual(edits.set_entry(lex, k, 'ZZZZ', 'Y'), (None, k))
        edits.remove_entry(lex, 0).apply()
        self.assertEqual(errors(check(f)), [])
        self.assertEqual(load(to_bytes(f)).lexicon.entries, lex.entries)    # through the writer
        for word, trans in (('', 'X'), ('A\x7f', 'X'), ('A', 'B\0'), ('→', 'X')):
            with self.subTest(word=word), self.assertRaises(EditError):
                edits.add_entry(lex, word, trans)

    def test_numbers(self):
        for text, want in (('5', 5), (' -5 ', -5), ('0x1F', 31), ('-0x10', -16), ('+7', 7)):
            self.assertEqual(edits.number(text, -100, 100, 'n'), want)
        for text in ('', '5a', '0x', '²', '1' * 11, '5.0', '0x123456789'):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.number(text, -10 ** 12, 10 ** 12, 'n')


class RuleEditTest(unittest.TestCase):
    def test_read_rule(self):
        # notation or instruction form, by the first word; columns count the leading spaces
        f = sw()
        names = ph_names(f.phonemes)
        rule = edits.read_rule('#1 L → M / _ O', names)
        self.assertEqual(rule_line(rule).text, 'RULE SEEK L ; R O ; REPL del 1 -> M ; NEXT #1')
        self.assertEqual(words(edits.read_rule('  RULE SEEK L ; R O ; REPL del 1 -> M ; NEXT #1 ', names)),
                         words(rule))
        for text in ('  #1 L → QQ / _ O', '  RULE SEEK L ; R QQ ; NEXT #1'):
            with self.subTest(text=text), self.assertRaises(ParseError) as cm:
                edits.read_rule(text, names)
            self.assertEqual(cm.exception.pos, text.index('QQ'))

    def test_rule_edits_undo(self):
        # spec: Testing, 11, for rules: each edit does what it says; undo restores the bytes
        f = sw()
        names, m, before = ph_names(f.phonemes), f.modules['M0'], to_bytes(f)
        r0, r1, n = m.rules[0], m.rules[1], len(m.rules)
        done = []

        def do(e, target):
            self.assertEqual(e.target, target)
            e.apply()
            done.append(e)
        do(edits.insert_rule(f, 'M0', 0, edits.read_rule('#1 L → M / _ O', names)), ('rule', 'M0', 0))
        self.assertEqual(notation_line(m.rules[0], names).text, '#1 L → M / _ O')
        do(edits.set_rule(f, 'M0', 0, edits.read_rule('#2 L → M / _ A', names)), ('rule', 'M0', 0))
        self.assertIsNone(edits.set_rule(f, 'M0', 0, edits.read_rule('#2 L → M / _ A', names)))
        do(edits.move_rule(f, 'M0', 0, 1), ('rule', 'M0', 1))
        self.assertIs(m.rules[0], r0)
        self.assertIsNone(edits.move_rule(f, 'M0', 0, -1))
        self.assertIsNone(edits.move_rule(f, 'M0', n, 1))
        do(edits.insert_rule(f, 'M0', 3, edits.copy_rule(r1)), ('rule', 'M0', 3))
        self.assertEqual(words(m.rules[3]), words(r1))
        do(edits.delete_rule(f, 'M0', n + 1), ('rule', 'M0', n))
        self.assertEqual(len(m.rules), n + 1)
        self.assertEqual(errors(check(f)), [])
        again = load(to_bytes(f))
        self.assertEqual([rule_line(x).text for x in again.modules['M0'].rules],
                         [rule_line(x).text for x in m.rules])
        for e in reversed(done):
            e.revert()
        self.assertEqual(to_bytes(f), before)

    def test_rule_refusals(self):
        f = sw()
        rule = edits.read_rule('#1 L → M / _ O', ph_names(f.phonemes))
        n = len(f.modules['M0'].rules)
        for call in (lambda: edits.set_rule(f, 'M0', n, rule), lambda: edits.set_rule(f, 'M9', 0, rule),
                     lambda: edits.insert_rule(f, 'M0', n + 1, rule), lambda: edits.insert_rule(f, 'M0', -1, rule),
                     lambda: edits.delete_rule(f, 'M0', n), lambda: edits.move_rule(f, 'M0', -1, 1)):
            with self.assertRaises(EditError):
                call()
        self.assertIsNotNone(edits.insert_rule(f, 'M0', n, rule))

    def test_carrying_repl_words(self):
        # a shipped carrying REPL word stays exempt while it is unchanged; a copy is new and refused
        f = load(ivx_bytes('da'))
        names = ph_names(f.phonemes)
        name, i, r = next((n, i, r) for n, m in sorted(f.modules.items()) for i, r in enumerate(m.rules)
                          if any(x.legacy for x in r.instrs))
        text = rule_line(r).text
        self.assertFalse(any(x.legacy for x in edits.read_rule(text, names).instrs))
        relabelled = re.sub(r'NEXT #[0-9]+$', 'NEXT #9999', text)
        self.assertNotEqual(relabelled, text)
        edits.set_rule(f, name, i, edits.read_rule(relabelled, names)).apply()
        self.assertEqual(check(f), [])
        edits.insert_rule(f, name, i + 1, edits.copy_rule(f.modules[name].rules[i])).apply()
        self.assertEqual([p.target for p in check(f)], [('rule', name, i + 1)])


if __name__ == '__main__':
    unittest.main()
