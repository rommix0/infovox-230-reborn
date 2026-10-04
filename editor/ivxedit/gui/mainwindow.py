"""The main window: section tree, the view for the selected item, the Speak panel, the
Problems panel; editing with undo, Save and Save As."""
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QAction, QKeySequence, QUndoCommand, QUndoStack
from PySide6.QtWidgets import (QDockWidget, QFileDialog, QInputDialog, QLabel, QListWidget,
                               QListWidgetItem, QMainWindow, QMessageBox, QSplitter,
                               QStackedWidget, QToolButton, QTreeWidget, QTreeWidgetItem)

from .. import edits
from ..binfmt import IvxFormatError
from ..checks import check, errors
from ..edits import EditError
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..listing import ph_names
from ..model import PNAMES, load
from ..notation import notation_line
from ..writer import BIN, SaveRefused, save
from ..xref import phoneme_uses
from .phonemeview import PhonemeView
from .rulesview import RulesView
from .speakpanel import SpeakPanel
from .views import HeaderView, LexiconView, TableView, TextView

ROLE = Qt.ItemDataRole.UserRole
# the header fields the Header view edits: label -> the key _header_edit takes
HEADER_KEYS = {'Version': 'version', 'Copyright': 'copyright', 'Flags': 'flags',
               'Lexicon name': 'lexicon name'}


class EditCommand(QUndoCommand):
    """an edits.Edit on the undo stack; undo and redo tell the window what they changed"""

    def __init__(self, edit, window):
        super().__init__(edit.text)
        self.edit, self.window = edit, window

    def redo(self):
        self.edit.apply()
        self.window.target = self.edit.target

    def undo(self):
        self.edit.revert()
        self.window.target = self.edit.target


class MainWindow(QMainWindow):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.f = self.path = None
        self.uses = {}
        self.ph_items = {}            # Phoneme -> its tree item
        self.module_items = {}        # module name -> its tree item
        self.rule_views = {}          # module name -> RulesView, per open file
        self.names = {}               # ph_names of the open file
        self.notation = {}            # module name -> [notation Line or None] per rule
        self.target = None            # what the last edit, undo or redo changed (Edit.target)
        self.shape = None             # the names and counts the tree shows
        self.refusal = ''             # why the last edit was refused (try_edit)
        self.undo = QUndoStack(self)
        self.undo.indexChanged.connect(self._changed)
        self.undo.cleanChanged.connect(lambda clean: self._title())
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.currentItemChanged.connect(lambda cur, prev: self.show_item(cur))
        self.stack = QStackedWidget()
        self.header_view = HeaderView(self._header_edit)
        self.table_view = TableView(self._activate)
        self.phoneme_view = PhonemeView(self.show_rule, self.try_edit)
        self.lexicon_view = LexiconView(self._lex_edit, self._lex_add, self._lex_remove)
        self.text_view = TextView()
        for w in (self.header_view, self.table_view, self.phoneme_view, self.lexicon_view, self.text_view):
            self.stack.addWidget(w)
        self.speak = SpeakPanel(settings)
        right = QSplitter(Qt.Orientation.Vertical)
        right.addWidget(self.stack)
        right.addWidget(self.speak)
        right.setSizes([500, 300])
        main = QSplitter(Qt.Orientation.Horizontal)
        main.addWidget(self.tree)
        main.addWidget(right)
        main.setSizes([220, 900])
        self.setCentralWidget(main)
        self.problem_list = QListWidget()
        self.problem_list.itemActivated.connect(lambda item: self.show_target(item.data(ROLE)))
        self.problem_dock = QDockWidget('Problems', self)
        self.problem_dock.setObjectName('problems')
        self.problem_dock.setWidget(self.problem_list)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self.problem_dock)
        self.problem_dock.hide()
        self.problems = QToolButton()
        self.problems.setAutoRaise(True)
        self.problems.clicked.connect(lambda: self.show_problems(not self.problem_dock.isVisible()))
        self.statusBar().addWidget(self.problems)
        self.coverage = QLabel()      # the shown module's rules in notation
        self.statusBar().addWidget(self.coverage)
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setSingleShot(True)
        self._refresh_timer.timeout.connect(self.refresh)
        self._menus()
        self.resize(1200, 800)
        self._title()

    def _menus(self):
        m = self.menuBar().addMenu('&File')
        for text, key, slot in (('&Open…', QKeySequence.StandardKey.Open, self.open_dialog),
                                ('&Save', QKeySequence.StandardKey.Save, self.save),
                                ('Save &As…', QKeySequence.StandardKey.SaveAs, self.save_as),
                                ('&ivxsay…', None, self.ivxsay_dialog),
                                ('&Quit', QKeySequence.StandardKey.Quit, self.close)):
            a = QAction(text, self)
            if key is not None:
                a.setShortcut(key)
            a.triggered.connect(slot)
            m.addAction(a)
        m = self.menuBar().addMenu('&Edit')
        undo, redo = self.undo.createUndoAction(self, '&Undo'), self.undo.createRedoAction(self, '&Redo')
        undo.setShortcut(QKeySequence.StandardKey.Undo)
        redo.setShortcut(QKeySequence.StandardKey.Redo)
        m.addAction(undo)
        m.addAction(redo)
        self.instr_action = QAction('&Instruction form', self)
        self.instr_action.setCheckable(True)
        self.instr_action.setShortcut(QKeySequence('Ctrl+I'))
        self.instr_action.toggled.connect(self.set_instruction_form)
        view = self.menuBar().addMenu('&View')
        view.addAction(self.instr_action)
        view.addAction(self.problem_dock.toggleViewAction())

    def _title(self):
        name = ' — %s%s' % (self.path.name, '' if self.undo.isClean() else ' *') if self.path else ''
        self.setWindowTitle('IVX Editor' + name)

    # --- files
    def open_dialog(self):
        if not self.maybe_save():
            return
        path, _ = QFileDialog.getOpenFileName(self, 'Open', self.settings.value('dir', ''),
                                              'Infovox language files (*.ivx);;All files (*)')
        if path:
            self.settings.setValue('dir', str(Path(path).parent))
            self.open_path(path)

    def open_path(self, path):
        """open_file with a message box instead of an exception"""
        try:
            self.open_file(path)
        except (OSError, IvxFormatError) as e:
            QMessageBox.critical(self, 'Open', 'Cannot open %s:\n%s' % (path, e))

    def open_file(self, path):
        """load path into the window, dropping any unsaved edits; raises OSError or IvxFormatError"""
        data = Path(path).read_bytes()
        f = load(data)
        uses = phoneme_uses(f)
        self.f, self.path = f, Path(path)
        self.uses = uses
        self.names, self.notation = ph_names(f.phonemes), {}
        self._drop_rule_views()
        self.lexicon_view.reset()
        self.speak.set_model(f, data)
        self.undo.clear()
        self.target, self.shape = None, self._shape()
        self._problems()
        self._fill_tree()
        self._title()

    def ivxsay_dialog(self):
        path, _ = QFileDialog.getOpenFileName(self, 'ivxsay to run', '', 'ivxsay (ivxsay*)')
        if path:
            self.settings.setValue('ivxsay', path)

    def save(self):
        """save to the open file; a file in bin/ is saved elsewhere (Save As). -> True if saved"""
        if self.f is None:
            return False
        if BIN in self.path.resolve().parents:
            return self.save_as()
        return self.write(self.path)

    def save_as(self):
        if self.f is None:
            return False
        path = self.ask_save_path()
        return bool(path) and self.write(Path(path))

    def ask_save_path(self):
        """the path to save to, or '' (a dialog; replaced in tests)"""
        start = self.settings.value('save_dir', '') or str(Path.home())
        path, _ = QFileDialog.getSaveFileName(self, 'Save As', str(Path(start) / self.path.name),
                                              'Infovox language files (*.ivx)')
        if path:
            self.settings.setValue('save_dir', str(Path(path).parent))
        return path

    def write(self, path):
        """save the model to path; on failure say why. -> True if saved"""
        try:
            save(self.f, path)
        except SaveRefused as e:
            self.show_problems()
            self.warn('Save', 'Not saved: %d problem(s).\n\n%s' % (
                len(e.problems), '\n'.join(str(p) for p in e.problems[:10])))
            return False
        except OSError as e:
            self.warn('Save', 'Not saved: %s' % e)
            return False
        self.path = Path(path)
        self.undo.setClean()
        self._title()
        self.statusBar().showMessage('Saved %s' % self.path, 5000)
        return True

    def warn(self, title, text):
        """a message box (replaced in tests)"""
        QMessageBox.warning(self, title, text)

    def ask_unsaved(self):
        """Save, Discard or Cancel for unsaved changes (a dialog; replaced in tests)"""
        b = QMessageBox.StandardButton
        return QMessageBox.question(self, 'Unsaved changes', '%s has unsaved changes.' % self.path.name,
                                    b.Save | b.Discard | b.Cancel, b.Save)

    def maybe_save(self):
        """-> True if the window may drop the open file: no unsaved changes, saved, or discarded"""
        if self.f is None or self.undo.isClean():
            return True
        answer = self.ask_unsaved()
        if answer == QMessageBox.StandardButton.Save:
            return self.save()
        return answer == QMessageBox.StandardButton.Discard

    # --- editing
    def try_edit(self, fn, *args):
        """make an edit with an edits function and push it; an EditError is shown in the
        status bar. -> the extra value of a function that returns (Edit, extra), else True;
        False if refused"""
        try:
            result = fn(*args)
        except EditError as e:
            self.refusal = str(e)
            self.statusBar().showMessage(self.refusal, 10000)
            return False
        extra = True
        if isinstance(result, tuple):
            result, extra = result
        if result is not None:
            self.undo.push(EditCommand(result, self))
        return extra

    def _shape(self):
        lex = self.f.lexicon
        return (tuple((id(p), p.name) for p in self.f.phonemes), None if lex is None else len(lex.entries))

    def _changed(self):
        """after an edit, undo or redo: problems, title, the tree's names and counts, and
        (after this signal) the shown view"""
        shape = self._shape()
        if shape[0] != self.shape[0]:                 # phoneme names: the rule lines change
            self.names, self.notation = ph_names(self.f.phonemes), {}
            self._drop_rule_views()
        if shape != self.shape:
            self.shape = shape
            self._fill_tree(self._current())
        self._problems()
        self._title()
        self._refresh_timer.start(0)                  # not inside the signal of the edited widget

    def refresh(self):
        """show the current item again with the model's values, then the edit's target"""
        target, self.target = self.target, None
        self.show_item(self.tree.currentItem())
        if target is not None:
            self.show_target(target)

    def _problems(self):
        problems = check(self.f)
        n = len(errors(problems))
        self.problems.setText('Problems (%d)' % len(problems) if n == len(problems) else
                              'Problems (%d, %d warning%s)' % (len(problems), len(problems) - n,
                                                               '' if len(problems) - n == 1 else 's'))
        self.problem_list.clear()
        for p in problems:
            item = QListWidgetItem(str(p))
            item.setData(ROLE, p.target)
            self.problem_list.addItem(item)

    def show_problems(self, on=True):
        self.problem_dock.setVisible(on)
        if on:
            self.resizeDocks([self.problem_dock], [140], Qt.Orientation.Vertical)

    def _drop_rule_views(self):
        for v in self.rule_views.values():
            self.stack.removeWidget(v)
            v.deleteLater()
        self.rule_views = {}

    def _header_edit(self, key, text):
        if key == 'flags':
            ok = self.try_edit(edits.set_header_flags, self.f, text)
        else:
            ok = self.try_edit(edits.set_header_text, self.f, key, text)
        if not ok:                                    # refused: the field shows the model's value again
            self.show_item(self.tree.currentItem())

    def _lex_edit(self, i, word, trans):
        k = self.try_edit(edits.set_entry, self.f.lexicon, i, word, trans)
        return k is not False

    def _lex_add(self):
        word, ok = self.ask_text('Add entry', 'Headword:')
        if ok and word:
            self.try_edit(edits.add_entry, self.f.lexicon, word, word)

    def _lex_remove(self, i):
        self.try_edit(edits.remove_entry, self.f.lexicon, i)

    def ask_text(self, title, label):
        """-> (text, ok) (a dialog; replaced in tests)"""
        return QInputDialog.getText(self, title, label)

    def _phoneme_action(self, action, ph):
        f = self.f
        if action == 'delete':
            if ph is not None and not self.try_edit(edits.delete_phoneme, f, ph):
                self.warn('Delete phoneme', self.refusal)     # the rule list is too long for the status bar
            return
        at = len(f.phonemes) if ph is None else f.phonemes.index(ph) + 1
        model = ph if action == 'copy' and ph is not None and not ph.is_defaults else None
        self.try_edit(edits.add_phoneme, f, at, edits.free_name(f), model)

    # --- the tree
    def _node(self, parent, text, kind, obj=None):
        item = QTreeWidgetItem([text])
        item.setData(0, ROLE, (kind, obj))
        (parent.addChild if isinstance(parent, QTreeWidgetItem) else parent.addTopLevelItem)(item)
        return item

    def _current(self):
        item = self.tree.currentItem()
        return item.data(0, ROLE) if item is not None else None

    def _fill_tree(self, select=None):
        """rebuild the tree; select the item for (kind, obj), else the first"""
        f, t = self.f, self.tree
        expanded = {self.tree.topLevelItem(k).data(0, ROLE)[0] for k in range(t.topLevelItemCount())
                    if t.topLevelItem(k).isExpanded()}
        t.blockSignals(True)
        t.clear()
        self.ph_items, self.module_items = {}, {}
        self._node(t, 'Header', 'header')
        self._node(t, 'Parameters (%d)' % len(PNAMES), 'params')
        phs = self._node(t, 'Phonemes (%d)' % len(f.phonemes), 'phonemes')
        for ph in f.phonemes:
            label = 'defaults' if ph.is_defaults else ph_name(ph.name)
            self.ph_items[ph] = self._node(phs, label, 'phoneme', ph)
        rules = self._node(t, 'Rules', 'rules')
        for name in sorted(f.modules):
            self.module_items[name] = self._node(
                rules, '%s (%d)' % (name, len(f.modules[name].rules)), 'module', name)
        self._node(t, 'Characters (256)', 'chars')
        if f.lexicon is not None:
            self._node(t, 'Lexicon (%d)' % len(f.lexicon.entries), 'lexicon')
        if f.ixlex is not None:
            self._node(t, 'Lexicon 2 (raw)', 'ixlex')
        self._node(t, 'Unknown', 'unknown')
        for item in self.items():
            if item.data(0, ROLE)[0] in expanded:
                item.setExpanded(True)
        found = next((it for it in self.items() if select is not None and it.data(0, ROLE)[0] == select[0]
                      and it.data(0, ROLE)[1] is select[1]), None)
        if found is None and select is not None and select[0] == 'phoneme':
            found = self._top('phonemes')                  # the phoneme is gone (deleted)
        t.setCurrentItem(found if found is not None else t.topLevelItem(0))
        t.blockSignals(False)
        self.show_item(t.currentItem())

    def _top(self, kind):
        return next((self.tree.topLevelItem(k) for k in range(self.tree.topLevelItemCount())
                     if self.tree.topLevelItem(k).data(0, ROLE)[0] == kind), None)

    def items(self):
        """every tree item, depth first"""
        out = []

        def walk(item):
            out.append(item)
            for k in range(item.childCount()):
                walk(item.child(k))
        for k in range(self.tree.topLevelItemCount()):
            walk(self.tree.topLevelItem(k))
        return out

    # --- views
    def show_item(self, item):
        if item is None or self.f is None:
            return
        kind, obj = item.data(0, ROLE)
        f = self.f
        self.coverage.setText('')
        if kind == 'header':
            self.header_view.set_fields([(label, text, HEADER_KEYS.get(label))
                                         for label, text in header_fields(f)])
            self.stack.setCurrentWidget(self.header_view)
        elif kind == 'params':
            rows = []
            for i, name in enumerate(PNAMES):
                a0, a1, a2, a3 = f.param(i)
                rows.append([i, name, '0x%04x' % a0, a1_text(a1), a2, a3])
            self._table('Parameters: A0 name slot, A1 type → handler, A2 rate, A3 offset (REFERENCE §5.3)',
                        ['#', 'Name', 'A0', 'A1 type', 'A2', 'A3'], rows,
                        editable=lambda r, c: c >= 3,
                        on_edit=lambda r, c, text: self.try_edit(
                            edits.set_param, f, r, edits.PARAM_COLUMNS[c - 3], text))
        elif kind == 'phonemes':
            rows = [[i, 'defaults' if ph.is_defaults else ph_name(ph.name), '0x%08x' % ph.feat,
                     '0x%08x' % ph.mask, len(ph.items), len(self.uses.get(ph, []))]
                    for i, ph in enumerate(f.phonemes)]
            self._table('Phoneme inventory', ['#', 'Name', 'feat', 'mask', 'Items', 'Used by'],
                        rows, [('phoneme', ph) for ph in f.phonemes],
                        actions=[(text, lambda data, a=a: self._phoneme_action(a, data and data[1]))
                                 for text, a in (('New', 'new'), ('Copy', 'copy'), ('Delete', 'delete'))])
        elif kind == 'phoneme':
            self.phoneme_view.set_phoneme(obj, self.uses.get(obj, []), f.modules)
            self.stack.setCurrentWidget(self.phoneme_view)
        elif kind == 'rules':
            rows = [[n, len(m.rules), self._in_notation(n), sum(r.iterative for r in m.rules),
                     '0x%04x' % m.lead] for n, m in sorted(f.modules.items())]
            self._table('Rule modules (REFERENCE §5.5)',
                        ['Module', 'Rules', 'In notation', 'RULE*', 'Lead word'],
                        rows, [('module', n) for n in sorted(f.modules)])
        elif kind == 'module':
            self.stack.setCurrentWidget(self._rules_view(obj))
            self.coverage.setText('%s: %s / %s rules in notation' % (
                obj, format(self._in_notation(obj), ','), format(len(f.modules[obj].rules), ',')))
        elif kind == 'chars':
            rows = [['0x%02x' % ch, c, '' if code is None else code,
                     '(built-in)' if fl is None else flags_text(fl), '' if xl is None else xl]
                    for ch, c, code, fl, xl in char_rows(f)]
            self._table('Characters: class code and flags (REFERENCE §5.2), transliteration (H[14])',
                        ['Byte', 'Char', 'Class', 'Flags', 'Translit.'], rows,
                        editable=lambda r, c: (c == 2 and (r < 0x80 or f.class_hi is not None))
                        or (c == 4 and r >= 0x80 and f.xlat_hi is not None),
                        on_edit=lambda r, c, text: self.try_edit(
                            edits.set_class if c == 2 else edits.set_xlat, f, r, text))
        elif kind == 'lexicon':
            self.lexicon_view.set_lexicon(f.lexicon)
            self.stack.setCurrentWidget(self.lexicon_view)
        elif kind == 'ixlex':
            self._text('Indexed lexicon (H[13] + 0x80 to the end of the data), not decoded\n\n'
                       + hexdump(f.ixlex))
        elif kind == 'unknown':
            self._text(unknown_text(f))

    def _table(self, title, headers, rows, row_data=None, editable=None, on_edit=None, actions=()):
        self.table_view.set_rows(title, headers, rows, row_data, editable, on_edit, actions)
        self.stack.setCurrentWidget(self.table_view)

    def _text(self, text):
        self.text_view.setPlainText(text)
        self.stack.setCurrentWidget(self.text_view)

    def _notation(self, name):
        """[notation Line or None] for each rule of module `name`, made once per file"""
        if name not in self.notation:
            self.notation[name] = [notation_line(r, self.names) for r in self.f.modules[name].rules]
        return self.notation[name]

    def _in_notation(self, name):
        return sum(n is not None for n in self._notation(name))

    def _rules_view(self, name):
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme)
            v.set_instruction_form(self.instr_action.isChecked())
            v.set_module(name, self.f.modules[name], self._notation(name))
            self.stack.addWidget(v)
            self.rule_views[name] = v
        return self.rule_views[name]

    def set_instruction_form(self, on):
        """View ▸ Instruction form: every rule in instruction form, or notation where it exists"""
        for v in self.rule_views.values():
            v.set_instruction_form(on)

    def _activate(self, data):
        kind, obj = data
        if kind == 'phoneme':
            self.show_phoneme(obj)
        elif kind == 'module':
            self.tree.setCurrentItem(self.module_items[obj])

    # --- jumps
    def show_phoneme(self, ph):
        self.tree.setCurrentItem(self.ph_items[ph])

    def show_rule(self, module, i):
        self.tree.setCurrentItem(self.module_items[module])
        self._rules_view(module).goto_rule(i)

    def show_target(self, target):
        """go to a Problem's or an Edit's target"""
        if not target or self.f is None:
            return
        kind = target[0]
        if kind == 'phoneme' and target[1] in self.ph_items:
            self.show_phoneme(target[1])
        elif kind == 'rule':
            self.show_rule(target[1], target[2])
        elif kind == 'lexicon':
            self.tree.setCurrentItem(self._top('lexicon'))
            self.lexicon_view.select(target[1])
        elif kind != 'phoneme' and self._top(kind) is not None:
            self.tree.setCurrentItem(self._top(kind))

    def closeEvent(self, event):
        if not self.maybe_save():
            event.ignore()
            return
        self.speak.cleanup()
        super().closeEvent(event)
