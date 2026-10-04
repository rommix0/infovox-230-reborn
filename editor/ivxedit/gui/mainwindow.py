"""The main window: section tree, the view for the selected item, the Speak panel."""
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (QFileDialog, QLabel, QMainWindow, QMessageBox, QSplitter,
                               QStackedWidget, QTreeWidget, QTreeWidgetItem)

from ..binfmt import IvxFormatError
from ..checks import check
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..listing import ph_names
from ..model import PNAMES, load
from ..notation import notation_line
from ..xref import phoneme_uses
from .phonemeview import PhonemeView
from .rulesview import RulesView
from .speakpanel import SpeakPanel
from .views import HeaderView, LexiconView, TableView, TextView

ROLE = Qt.ItemDataRole.UserRole


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
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.currentItemChanged.connect(lambda cur, prev: self.show_item(cur))
        self.stack = QStackedWidget()
        self.header_view = HeaderView()
        self.table_view = TableView(self._activate)
        self.phoneme_view = PhonemeView(self.show_rule)
        self.lexicon_view = LexiconView()
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
        self.problems = QLabel()
        self.statusBar().addWidget(self.problems)
        self.coverage = QLabel()      # the shown module's rules in notation
        self.statusBar().addWidget(self.coverage)
        self._menus()
        self.resize(1200, 800)
        self._title()

    def _menus(self):
        m = self.menuBar().addMenu('&File')
        for text, key, slot in (('&Open…', QKeySequence.StandardKey.Open, self.open_dialog),
                                ('&ivxsay…', None, self.ivxsay_dialog),
                                ('&Quit', QKeySequence.StandardKey.Quit, self.close)):
            a = QAction(text, self)
            if key is not None:
                a.setShortcut(key)
            a.triggered.connect(slot)
            m.addAction(a)
        self.instr_action = QAction('&Instruction form', self)
        self.instr_action.setCheckable(True)
        self.instr_action.setShortcut(QKeySequence('Ctrl+I'))
        self.instr_action.toggled.connect(self.set_instruction_form)
        self.menuBar().addMenu('&View').addAction(self.instr_action)

    def _title(self):
        self.setWindowTitle('IVX Editor' + (' — %s' % self.path.name if self.path else ''))

    # --- files
    def open_dialog(self):
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
        """load path into the window; raises OSError or IvxFormatError"""
        data = Path(path).read_bytes()
        f = load(data)
        uses = phoneme_uses(f)
        problems = check(f)
        self.f, self.path = f, Path(path)
        self.uses = uses
        self.names, self.notation = ph_names(f.phonemes), {}
        for v in self.rule_views.values():
            self.stack.removeWidget(v)
            v.deleteLater()
        self.rule_views = {}
        self.speak.set_model(f, data)
        self.problems.setText('Problems (%d)' % len(problems))
        self.problems.setToolTip('\n'.join(str(p) for p in problems))
        self._fill_tree()
        self._title()

    def ivxsay_dialog(self):
        path, _ = QFileDialog.getOpenFileName(self, 'ivxsay to run', '', 'ivxsay (ivxsay*)')
        if path:
            self.settings.setValue('ivxsay', path)

    # --- the tree
    def _node(self, parent, text, kind, obj=None):
        item = QTreeWidgetItem([text])
        item.setData(0, ROLE, (kind, obj))
        (parent.addChild if isinstance(parent, QTreeWidgetItem) else parent.addTopLevelItem)(item)
        return item

    def _fill_tree(self):
        f, t = self.f, self.tree
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
        t.blockSignals(False)
        t.setCurrentItem(t.topLevelItem(0))

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
            self.header_view.set_fields(header_fields(f))
            self.stack.setCurrentWidget(self.header_view)
        elif kind == 'params':
            rows = []
            for i, name in enumerate(PNAMES):
                a0, a1, a2, a3 = f.param(i)
                rows.append([i, name, '0x%04x' % a0, a1_text(a1), a2, a3])
            self._table('Parameters: A0 name slot, A1 type → handler, A2 rate, A3 offset (REFERENCE §5.3)',
                        ['#', 'Name', 'A0', 'A1 type', 'A2', 'A3'], rows)
        elif kind == 'phonemes':
            rows = [[i, 'defaults' if ph.is_defaults else ph_name(ph.name), '0x%08x' % ph.feat,
                     '0x%08x' % ph.mask, len(ph.items), len(self.uses.get(ph, []))]
                    for i, ph in enumerate(f.phonemes)]
            self._table('Phoneme inventory', ['#', 'Name', 'feat', 'mask', 'Items', 'Used by'],
                        rows, [('phoneme', ph) for ph in f.phonemes])
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
                        ['Byte', 'Char', 'Class', 'Flags', 'Translit.'], rows)
        elif kind == 'lexicon':
            self.lexicon_view.set_lexicon(f.lexicon)
            self.stack.setCurrentWidget(self.lexicon_view)
        elif kind == 'ixlex':
            self._text('Indexed lexicon (H[13] + 0x80 to the end of the data), not decoded\n\n'
                       + hexdump(f.ixlex))
        elif kind == 'unknown':
            self._text(unknown_text(f))

    def _table(self, title, headers, rows, row_data=None):
        self.table_view.set_rows(title, headers, rows, row_data)
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

    def closeEvent(self, event):
        self.speak.cleanup()
        super().closeEvent(event)
