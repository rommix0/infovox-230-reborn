"""Editing rules in the window, offscreen (spec: GUI, step 5; Testing, 11)."""
import unittest

from ivxedit.listing import rule_line
from ivxedit.notation import notation_line
from ivxedit.writer import to_bytes
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_path


def settle():
    """run the queued view refreshes"""
    for _ in range(3):
        app().processEvents()


@needs_qt
class RuleEditingTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.win.warn = lambda title, text: None
        self.addCleanup(self._close)
        self.win.open_file(ivx_path('sw'))
        self.before = to_bytes(self.win.f)
        self.win.show_rule('M0', 0)
        self.view = self.win.rule_views['M0']
        self.m = self.win.f.modules['M0']

    def _close(self):
        self.win.undo.setClean()                     # no unsaved-changes question at the end
        self.win.close()

    def key(self, widget, key):
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        QTest.keyClick(widget, getattr(Qt.Key, 'Key_' + key))
        settle()

    def type_line(self, text):
        """the open editor's text becomes text, then Enter"""
        self.assertIsNotNone(self.view.editor)
        self.view.editor.setText(text)
        self.key(self.view.editor, 'Return')

    def action(self, text):
        next(a for a in self.view.rule_actions if a.text() == text).trigger()
        settle()

    def test_insert_and_edit(self):
        # a rule added in notation (spec: Testing, 8, in the window), then edited in
        # instruction form; undo restores the file
        w, v, m = self.win, self.view, self.m
        n = len(m.rules)
        v.goto_rule(0)
        with SlotErrors() as errs:
            v.insert_rule()
            self.assertEqual(v.document().blockCount(), n + 1)     # the line the rule is typed on
            self.assertEqual(v.editor.text(), '')
            self.type_line('#1 L -> M / _ O')
            self.assertIsNone(v.editor)
            self.assertEqual(len(m.rules), n + 1)
            self.assertEqual(notation_line(m.rules[1], w.names).text, '#1 L → M / _ O')
            self.assertEqual((v.current(), v.lines[1].text), (1, '#1 L → M / _ O'))
            self.assertEqual(w.module_items['M0'].text(0), 'M0 (%d)' % (n + 1))
            self.assertIn(('M0', 1), w.uses[next(p for p in w.f.phonemes if p.name == b'\0L')])
            self.assertTrue(w.windowTitle().endswith(' *'))
            v.edit_rule(1)
            self.assertEqual(v.editor.text(), '#1 L → M / _ O')
            self.type_line('RULE SEEK L ; R A ; REPL del 1 -> M ; NEXT #2')
            self.assertEqual(rule_line(m.rules[1]).text, 'RULE SEEK L ; R A ; REPL del 1 -> M ; NEXT #2')
            self.assertEqual(v.lines[1].text, '#2 L → M / _ A')
            w.undo.undo()
            w.undo.undo()
            settle()
        self.assertEqual(errs, [])
        self.assertEqual(to_bytes(w.f), self.before)
        self.assertEqual(v.document().blockCount(), n)
        self.assertEqual(w.module_items['M0'].text(0), 'M0 (%d)' % n)

    def test_a_line_that_does_not_read(self):
        # the editor stays open at the problem, the status bar says why, nothing changes
        w, v = self.win, self.view
        text = v.lines[0].text
        with SlotErrors() as errs:
            self.key(v, 'Return')                                  # edits the current rule
            self.assertEqual(v.editor.text(), text)
            self.type_line('#1 L → QQ / _ O')
            self.assertIsNotNone(v.editor)
            self.assertEqual(v.editor.cursorPosition(), '#1 L → QQ / _ O'.index('QQ'))
            self.assertIn("no phoneme 'QQ'", w.statusBar().currentMessage())
            self.key(v.editor, 'Escape')
        self.assertEqual(errs, [])
        self.assertIsNone(v.editor)
        self.assertEqual((w.undo.count(), v.lines[0].text), (0, text))
        self.assertEqual(to_bytes(w.f), self.before)

    def test_unchanged_line_is_no_edit(self):
        v = self.view
        v.edit_rule(0)
        self.key(v.editor, 'Return')
        self.assertIsNone(v.editor)
        self.assertEqual(self.win.undo.count(), 0)

    def test_escape_drops_a_new_line(self):
        v, n = self.view, len(self.m.rules)
        v.insert_rule()
        self.key(v.editor, 'Escape')
        self.assertEqual((v.editor, v.document().blockCount(), self.win.undo.count()), (None, n, 0))

    def test_copy_move_delete(self):
        w, v, m = self.win, self.view, self.m
        old = list(m.rules)
        v.goto_rule(0)
        with SlotErrors() as errs:
            for text in ('Copy rule', 'Move rule down', 'Move rule down', 'Delete rule', 'Move rule up'):
                self.action(text)
        self.assertEqual(errs, [])
        self.assertEqual(m.rules[:4], [old[0], old[1], old[3], old[2]])
        self.assertEqual(m.rules[4:], old[4:])
        self.assertEqual(v.current(), 2)
        while w.undo.canUndo():
            w.undo.undo()
        settle()
        self.assertEqual(to_bytes(w.f), self.before)

    def test_double_click_edits_the_rule(self):
        from PySide6.QtCore import Qt
        from PySide6.QtGui import QTextCursor
        from PySide6.QtTest import QTest
        v = self.view
        cur = QTextCursor(v.document().findBlockByNumber(3))
        cur.setPosition(cur.position() + 1)                        # in the rule number
        QTest.mouseDClick(v.viewport(), Qt.MouseButton.LeftButton, pos=v.cursorRect(cur).center())
        self.assertIsNotNone(v.editor)
        self.assertEqual((v.editor.i, v.editor.text()), (3, v.lines[3].text))

    def test_instruction_form_keeps_the_place(self):
        w, v = self.win, self.view
        w.show()
        w.show_rule('M0', 300)
        settle()
        place = (v.verticalScrollBar().value(), v.textCursor().blockNumber())
        self.assertGreater(place[0], 0)
        for on in (True, False):
            w.instr_action.setChecked(on)
            self.assertEqual((v.verticalScrollBar().value(), v.textCursor().blockNumber()), place)


if __name__ == '__main__':
    unittest.main()
