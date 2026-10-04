"""The read-only GUI, offscreen (spec: Testing, 9)."""
import tempfile
import unittest
from pathlib import Path

from ivxedit.binfmt import IvxFormatError
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import LANGS, ivx_path


@needs_qt
class SmokeTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.addCleanup(self.win.close)

    def top_level(self):
        t = self.win.tree
        return [t.topLevelItem(k).text(0) for k in range(t.topLevelItemCount())]

    def test_every_node_of_every_file(self):
        for lang in LANGS:
            with self.subTest(lang=lang), SlotErrors() as errors:
                self.win.open_file(ivx_path(lang))
                for item in self.win.items():
                    self.win.tree.setCurrentItem(item)
                    self.assertIs(self.win.tree.currentItem(), item)
                self.assertEqual(errors, [])

    def test_tree_has_every_section(self):
        self.win.open_file(ivx_path('sw'))
        self.assertEqual(self.top_level(), [
            'Header', 'Parameters (29)', 'Phonemes (%d)' % len(self.win.f.phonemes), 'Rules',
            'Characters (256)', 'Lexicon (3370)', 'Unknown'])
        self.assertEqual(self.win.windowTitle(), 'IVX Editor — swrules.ivx')
        self.win.open_file(ivx_path('ge'))
        self.assertIn('Lexicon 2 (raw)', self.top_level())
        self.assertNotIn('Lexicon (3370)', self.top_level())

    def test_jumps(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        plus = next(ph for ph in w.f.phonemes if ph.name == b'\0+')
        w.show_phoneme(plus)
        self.assertIs(w.stack.currentWidget(), w.phoneme_view)
        module, i = w.uses[plus][0]
        w.phoneme_view.uses.itemActivated.emit(w.phoneme_view.uses.item(0))
        view = w.rule_views[module]
        self.assertIs(w.stack.currentWidget(), view)
        self.assertEqual(view.textCursor().blockNumber(), i)
        line = view.document().findBlockByNumber(i).text()
        ref = view.ref_at(i, line.index(' + ') + 1)
        self.assertIs(ref.ph, plus)
        w.show_phoneme(ref.ph)
        self.assertIs(w.tree.currentItem(), w.ph_items[plus])

    def test_rules_view_shows_instruction_form(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show_rule('M0', 0)
        text = w.rule_views['M0'].toPlainText().splitlines()
        self.assertEqual(len(text), len(w.f.modules['M0'].rules))
        self.assertIn('RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32', '\n'.join(text))

    def test_bad_file_raises(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'bad.ivx'
            p.write_bytes(bytes(100))
            with self.assertRaises(IvxFormatError):
                self.win.open_file(p)


if __name__ == '__main__':
    unittest.main()
