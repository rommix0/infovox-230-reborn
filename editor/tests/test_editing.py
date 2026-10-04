"""Editing in the window, offscreen (spec: GUI, step 4; Testing, 11)."""
import tempfile
import unittest
from pathlib import Path

from ivxedit.model import PNAMES, load
from ivxedit.writer import to_bytes
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_bytes, ivx_path


def settle():
    """run the queued view refreshes"""
    for _ in range(3):
        app().processEvents()


@needs_qt
class EditingTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.win.warn = lambda title, text: self.warnings.append(text)
        self.warnings = []
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(self._close)

    def _close(self):
        self.win.undo.setClean()                     # no unsaved-changes question at the end
        self.win.close()

    def node(self, kind):
        from ivxedit.gui.mainwindow import ROLE
        item = next(t for t in self.win.items() if t.data(0, ROLE)[0] == kind)
        self.win.tree.setCurrentItem(item)
        return item

    def cell(self, view_model, row, col, text):
        from PySide6.QtCore import Qt
        self.assertTrue(view_model.flags(view_model.index(row, col)) & Qt.ItemFlag.ItemIsEditable)
        view_model.setData(view_model.index(row, col), text)
        settle()

    def edit_everything(self):
        """one edit through each editor of the window"""
        w = self.win
        f = w.f
        self.node('header')
        field = w.header_view.widgets['Version']
        field.setText('edited in the window')
        field.editingFinished.emit()
        settle()
        self.node('params')
        self.cell(w.table_view.model, PNAMES.index('F2'), 5, '36')
        self.node('chars')
        self.cell(w.table_view.model, ord('#'), 2, '-3')
        self.cell(w.table_view.model, 0xC4, 4, 'AE')
        a = next(p for p in f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        pv = w.phoneme_view
        pv.name.setText('A2')
        pv.name.editingFinished.emit()
        settle()
        pv.bits.cellClicked.emit(0, 0)                         # feat bit 31
        settle()
        row = next(r for r in range(pv.items.rowCount()) if pv.items.item(r, 0).text() == 'F1')
        pv.items.item(row, 1).setText('150@-1')
        settle()
        pv.ask_item = lambda names: ('v9f', True)
        pv.add.click()
        settle()
        pv.items.setCurrentCell(0, 1)
        pv.remove.click()
        settle()
        self.node('phonemes')
        tv = w.table_view
        tv.select(3)
        tv.buttons['Copy'].click()                             # a copy after phoneme 3, then shown
        settle()
        self.node('phonemes')
        tv.select(len(f.phonemes) - 1)
        tv.buttons['New'].click()
        settle()
        self.node('lexicon')
        lv = w.lexicon_view
        self.cell(lv.model, 5, 2, "N'YTT")
        w.ask_text = lambda title, label: ('ÖVERALLT', True)
        lv.add.click()
        settle()
        lv.select(1)
        lv.remove.click()
        settle()

    def test_undo_restores_the_file(self):
        """spec Testing 11: a script of edits, then undo of all of them, saves byte-identical"""
        w = self.win
        data = ivx_bytes('sw')
        with SlotErrors() as errors:
            w.open_file(ivx_path('sw'))
            self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')
            self.edit_everything()
            self.assertEqual(errors, [])
        self.assertEqual(w.undo.count(), 14)
        self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx *')
        edited = to_bytes(w.f)
        g = load(edited)
        self.assertEqual(g.version, 'edited in the window')
        self.assertEqual(sum(p.name == b'A2' for p in g.phonemes), 1)
        self.assertEqual(len(g.phonemes), len(load(data).phonemes) + 2)
        self.assertIn((b'\xd6VERALLT', b'\xd6VERALLT'), g.lexicon.entries)
        with SlotErrors() as errors:
            while w.undo.canUndo():
                w.undo.undo()
                settle()
            self.assertEqual(to_bytes(w.f), data)
            self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')
            while w.undo.canRedo():
                w.undo.redo()
                settle()
            self.assertEqual(errors, [])
        self.assertEqual(to_bytes(w.f), edited)
        out = Path(self.tmp.name) / 'sw.ivx'
        w.ask_save_path = lambda: str(out)
        self.assertTrue(w.save())                              # bin/ is never written: Save As
        self.assertEqual(out.read_bytes(), edited)
        self.assertEqual((w.path, w.windowTitle()), (out, 'IVX Editor — sw.ivx'))

    def test_undo_shows_what_it_changed(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('params')
        self.cell(w.table_view.model, 0, 4, '9')
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.undo.undo()
        settle()
        self.assertEqual(w.tree.currentItem().text(0), 'Parameters (29)')
        self.assertEqual(w.table_view.model.rows[0][4], str(load(ivx_bytes('sw')).param(0)[2]))

    def test_rename_reaches_the_rules(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        plus = next(p for p in w.f.phonemes if p.name == b'\0+')
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 + B → S')
        w.show_phoneme(plus)
        w.phoneme_view.name.setText('PL')
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.tree.currentItem().text(0), 'PL')
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 PL B → S')
        w.undo.undo()
        settle()
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 + B → S')

    def test_refused_edits(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.phoneme_view.name.setText('ABC')
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.phoneme_view.name.text(), 'A:')
        self.assertIn('one or two characters', w.statusBar().currentMessage())
        w.phoneme_view.items.item(3, 1).setText('nonsense')
        settle()
        self.assertNotEqual(w.phoneme_view.items.item(3, 1).text(), 'nonsense')
        self.node('phonemes')
        w.table_view.select(w.f.phonemes.index(a))
        w.table_view.buttons['Delete'].click()
        settle()
        self.assertIn('used by', w.statusBar().currentMessage())
        self.assertEqual(len(self.warnings), 1)                # the full rule list in a dialog
        self.assertIn('used by', self.warnings[0])
        self.assertIn('M0 rule', self.warnings[0])
        self.node('header')
        hv = w.header_view
        shown = {k: hv.widgets[k].text() for k in ('Version', 'Flags')}
        for label, bad in (('Flags', 'zz'), ('Version', 'x' * 64), ('Version', '→')):
            hv.widgets[label].setText(bad)
            hv.widgets[label].editingFinished.emit()
            settle()
            self.assertEqual(hv.widgets[label].text(), shown[label])
        self.node('params')
        self.cell(w.table_view.model, 0, 3, '40')
        self.assertIn('A1 type must be 0..32', w.statusBar().currentMessage())
        self.assertEqual(w.undo.count(), 0)
        self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')

    def test_problems_panel(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.assertEqual(w.problems.text(), 'Problems (0)')
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.phoneme_view.name.setText('E:')                      # a duplicate name: a warning
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.problems.text(), 'Problems (1, 1 warning)')
        e = next(p for p in w.f.phonemes if p.name == b'E:' and p is not a)
        self.node('header')
        w.problem_list.itemActivated.emit(w.problem_list.item(0))
        self.assertIs(w.tree.currentItem(), w.ph_items[e])
        a.items[0].value = 70000                               # an error the editors cannot make
        w._changed()
        self.assertEqual(w.problems.text(), 'Problems (2, 1 warning)')
        out = Path(self.tmp.name) / 'x.ivx'
        self.assertFalse(w.write(out))
        self.assertFalse(out.exists())
        self.assertIn('Not saved: 1 problem', self.warnings[0])
        self.assertFalse(w.problem_dock.isHidden())

    def test_unsaved_changes(self):
        from PySide6.QtWidgets import QMessageBox
        b = QMessageBox.StandardButton
        w = self.win
        w.open_file(ivx_path('sw'))
        self.assertTrue(w.maybe_save())                        # nothing to save
        self.node('params')
        self.cell(w.table_view.model, 0, 4, '9')
        w.ask_unsaved = lambda: b.Cancel
        self.assertFalse(w.maybe_save())
        self.assertFalse(w.close())                            # Cancel keeps the window open
        out = Path(self.tmp.name) / 'sw.ivx'
        w.ask_save_path = lambda: str(out)
        w.ask_unsaved = lambda: b.Save
        self.assertTrue(w.maybe_save())
        self.assertTrue(out.exists() and w.undo.isClean())
        self.cell(w.table_view.model, 0, 4, '8')
        w.ask_unsaved = lambda: b.Discard
        self.assertTrue(w.maybe_save())
        w.ask_save_path = lambda: ''
        self.assertFalse(w.save_as())                          # the dialog was cancelled

    def test_lexicon_selection_follows_the_entry(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('lexicon')
        lv = w.lexicon_view
        lv.filter.setText('BORT')
        settle()
        row = lv.proxy.mapToSource(lv.proxy.index(0, 1)).row()
        lv.proxy.setData(lv.proxy.index(0, 1), 'ZZBORT')
        settle()
        k = lv.current()
        self.assertEqual(w.f.lexicon.entries[k][0], b'ZZBORT')
        self.assertNotEqual(k, row)
        self.assertEqual(w.tree.currentItem().text(0), 'Lexicon (3370)')


    def test_typing_in_cells(self):
        """real editing through the cell editors, which the window refreshes after the edit"""
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        from PySide6.QtWidgets import QLineEdit

        def type_into(view, text):
            editor = next(e for e in view.viewport().findChildren(QLineEdit) if e.isVisible())
            QTest.keyClicks(editor, text)
            QTest.keyClick(editor, Qt.Key.Key_Return)
            settle()
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show()
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        items = w.phoneme_view.items
        row = next(r for r in range(items.rowCount()) if items.item(r, 0).text() == 'F1')
        k = len(a.items[row].points)
        with SlotErrors() as errors:
            items.setCurrentCell(row, 1 + k)                   # the empty cell after the last point
            items.editItem(items.item(row, 1 + k))
            type_into(items, '99@6')
            self.node('params')
            t = w.table_view.table
            t.setCurrentIndex(t.model().index(PNAMES.index('F0'), 4))
            t.edit(t.currentIndex())
            type_into(t, '7')
        self.assertEqual(errors, [])
        self.assertEqual(a.items[row].points[-1], (6, 99))
        self.assertEqual(items.item(row, 1 + k).text(), '99@6')
        self.assertEqual(w.f.param(PNAMES.index('F0'))[2], 7)
        self.assertEqual(w.undo.count(), 2)

    def test_buttons_belong_to_their_table(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('phonemes')
        self.assertEqual(list(w.table_view.buttons), ['New', 'Copy', 'Delete'])
        self.node('chars')
        self.assertEqual(w.table_view.buttons, {})

if __name__ == '__main__':
    unittest.main()
