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
        from PySide6.QtCore import Qt
        for lang in LANGS:
            with self.subTest(lang=lang), SlotErrors() as errors:
                self.win.open_file(ivx_path(lang))
                for item in self.win.items():
                    self.win.tree.setCurrentItem(item)
                    self.assertIs(self.win.tree.currentItem(), item)
                    kind, obj = item.data(0, Qt.ItemDataRole.UserRole)
                    if kind == 'header':
                        self.assertIs(self.win.stack.currentWidget(), self.win.header_view)
                    elif kind in ('params', 'phonemes', 'rules', 'chars'):
                        self.assertIs(self.win.stack.currentWidget(), self.win.table_view)
                    elif kind == 'phoneme':
                        self.assertIs(self.win.stack.currentWidget(), self.win.phoneme_view)
                    elif kind == 'module':
                        self.assertIs(self.win.stack.currentWidget(), self.win.rule_views[obj])
                    elif kind == 'lexicon':
                        self.assertIs(self.win.stack.currentWidget(), self.win.lexicon_view)
                    elif kind in ('ixlex', 'unknown'):
                        self.assertIs(self.win.stack.currentWidget(), self.win.text_view)
                self.assertEqual(errors, [])

    def test_tree_has_every_section(self):
        self.win.open_file(ivx_path('sw'))
        self.assertEqual(self.top_level(), [
            'Header', 'Parameters (29)', 'Phonemes (%d)' % len(self.win.f.phonemes), 'Rules',
            'Characters (256)', 'Lexicon (3370)', 'Unknown'])
        self.assertEqual(self.win.windowTitle(), 'IVX Editor — swrules.ivx')
        self.assertEqual(self.win.problems.text(), 'Problems (0)')
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

    def test_double_click_in_rules(self):
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        from PySide6.QtGui import QTextCursor
        w = self.win
        w.open_file(ivx_path('sw'))
        # Find a phoneme used in M0
        plus = next(ph for ph in w.f.phonemes if ph.name == b'\0+')
        module, i = next((m, idx) for m, idx in w.uses[plus] if m == 'M0')
        w.show_rule(module, i)
        view = w.rule_views[module]
        line_text = view.document().findBlockByNumber(i).text()
        # Find the '+' phoneme span in that rule
        plus_col = None
        for s, e, kind, obj in view.lines[i].spans:
            if kind == 'ph' and obj.ph is plus:
                plus_col = s + len('%5d  ' % i)
                break
        self.assertIsNotNone(plus_col, "phoneme '+' not found in rule")
        # Build cursor at that position and get the viewport rect
        block = view.document().findBlockByNumber(i)
        cursor = QTextCursor(block)
        cursor.setPosition(block.position() + plus_col)
        rect = view.cursorRect(cursor)
        pos = rect.center()
        # Double-click there
        with SlotErrors() as errors:
            QTest.mouseDClick(view.viewport(), Qt.MouseButton.LeftButton, pos=pos)
        # Should jump to the phoneme
        self.assertEqual(errors, [])
        self.assertIs(w.tree.currentItem(), w.ph_items[plus])
        # Test empty module: open sp, show M5 (empty), double-click at viewport center
        w.open_file(ivx_path('sp'))
        w.show_item(w.module_items['M5'])
        view = w.rule_views['M5']
        with SlotErrors() as errors:
            QTest.mouseDClick(view.viewport(), Qt.MouseButton.LeftButton, pos=view.viewport().rect().center())
        self.assertEqual(errors, [], "double-click in empty module should not raise")

    def test_table_row_jumps(self):
        from PySide6.QtCore import Qt
        w = self.win
        w.open_file(ivx_path('sw'))
        # Select Phonemes node and emit activated on row 3
        w.tree.setCurrentItem(next(t for t in w.items() if t.text(0).startswith('Phonemes')))
        w.table_view.table.activated.emit(w.table_view.model.index(3, 0))
        self.assertIs(w.tree.currentItem(), w.ph_items[w.f.phonemes[3]])
        # Select Rules node and emit activated on row 0 (first module)
        w.tree.setCurrentItem(next(t for t in w.items() if t.text(0) == 'Rules'))
        w.table_view.table.activated.emit(w.table_view.model.index(0, 0))
        first_module = sorted(w.f.modules)[0]
        self.assertIs(w.tree.currentItem(), w.module_items[first_module])


if __name__ == '__main__':
    unittest.main()
