"""The rules view: one module, one rule per line, in rule notation where it says the rule
exactly and in instruction form otherwise (those lines tinted); highlighted; double-click
a phoneme to jump to it.

Rules are edited one line at a time (spec: GUI, step 5). Enter, F2 or a double-click opens
an editor over the rule's line; it takes notation or instruction form (edits.read_rule).
Insert opens one on a new line after the current rule, Ctrl+D copies the rule, Delete
removes it, Alt+Up and Alt+Down move it. The context menu has the same."""
from PySide6.QtCore import Qt
from PySide6.QtGui import (QAction, QColor, QKeySequence, QSyntaxHighlighter, QTextBlockFormat,
                           QTextCharFormat, QTextCursor, QTextFormat)
from PySide6.QtWidgets import QLineEdit, QPlainTextEdit, QTextEdit

from .. import edits
from ..listing import ParseError, rule_line
from .views import mono

PREFIX = '%5d  '          # the rule index, as in the Problems panel's "M0 rule 12"
NEW = '    +  '           # the prefix of the line a new rule is typed on
COLOURS = {'op': '#1f5fbf', 'ph': '#a0400a', 'num': '#2a7f2a', 'feat': '#7a2a9a',
           'param': '#8a6d00', 'raw': '#c00000'}
FALLBACK = QColor('#f1ece0')   # background of a rule shown in instruction form for want of notation
REFUSED = 'QLineEdit { background: #fde2e2; }'     # the editor's text does not read
# the context menu of the editor inserts these (NOTATION.md: Typing)
SYMBOLS = ('→', '→⁺', '∅', '↺', '…', '⟵', '·', '−', '␠', '‹›', '≤', '≥', '≠')


class _Highlighter(QSyntaxHighlighter):
    def __init__(self, doc, view):
        super().__init__(doc)
        self.view = view
        self.formats = {}
        for kind, colour in COLOURS.items():
            fmt = QTextCharFormat()
            fmt.setForeground(QColor(colour))
            if kind == 'ph':
                fmt.setFontUnderline(True)
            self.formats[kind] = fmt

    def highlightBlock(self, text):
        i = self.view.rule_at(self.currentBlock().blockNumber())
        if i is not None and i < len(self.view.lines):
            for s, e, kind, _ in self.view.lines[i].spans:
                self.setFormat(len(PREFIX % i) + s, e - s, self.formats[kind])


class RuleEditor(QLineEdit):
    """the editor over one line of the view: Enter commits, Escape cancels, and losing the
    focus commits too (a line that does not read stays open)"""

    def __init__(self, view, i, adding, text):
        super().__init__(text, view.viewport())
        self.view, self.i, self.adding = view, i, adding
        self.setFont(mono())
        self.textEdited.connect(lambda _: self.setStyleSheet(''))

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.view.commit()
        elif event.key() == Qt.Key.Key_Escape:
            self.view.close_editor()
        else:
            super().keyPressEvent(event)

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        if (event.reason() != Qt.FocusReason.PopupFocusReason and self.view.editor is self
                and self.view.isVisible()):          # not while the window closes
            self.view.commit()

    def contextMenuEvent(self, event):
        menu = self.createStandardContextMenu()
        symbols = menu.addMenu('Insert symbol')
        for s in SYMBOLS:
            symbols.addAction(s, lambda s=s: self.put(s))
        menu.exec(event.globalPos())
        menu.deleteLater()

    def put(self, symbol):
        self.insert(symbol)
        if symbol == '‹›':
            self.cursorBackward(False)              # between the quotes


class RulesView(QPlainTextEdit):
    def __init__(self, on_phoneme=None, edit=None, status=None):
        super().__init__()
        self.on_phoneme = on_phoneme      # called with a Phoneme
        self.edit = edit                  # the window's try_edit(fn, *args); None: read-only
        self.status = status or (lambda text: None)    # shows why a line does not read
        self.setReadOnly(True)
        self.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse
                                     | Qt.TextInteractionFlag.TextSelectableByKeyboard)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.document().setUndoRedoEnabled(False)
        self.f = self.name = self.module = None
        self.names = {}                   # listing.ph_names of the file, for reading lines
        self.rules = []                   # the rules shown
        self.lines = []                   # the Line shown for each rule
        self.notation = []                # per rule: its Line in notation, or None
        self.instruction_form = False     # show every rule in instruction form
        self.editor = None                # the open RuleEditor
        self.blank = None                 # the text line a new rule is typed on
        self._hl = _Highlighter(self.document(), self)
        self.rule_actions = []
        for text, key, slot in (('Edit rule', 'F2', lambda: self.edit_rule()),
                                ('Insert rule', 'Ins', self.insert_rule),
                                ('Copy rule', 'Ctrl+D', self.copy_rule),
                                ('Delete rule', 'Del', self.delete_rule),
                                ('Move rule up', 'Alt+Up', lambda: self.move_rule(-1)),
                                ('Move rule down', 'Alt+Down', lambda: self.move_rule(1))):
            a = QAction(text, self)
            a.setShortcut(QKeySequence(key))
            a.setShortcutContext(Qt.ShortcutContext.WidgetShortcut)
            a.triggered.connect(slot)
            self.addAction(a)
            self.rule_actions.append(a)

    def set_module(self, name, module, notation, f=None, names=None):
        """show module; notation: per rule, its notation Line or None (notation.notation_line).
        Its rules can be edited when f (the file) and names are given. Shown again with the
        same rules and lines, the view stays as it is (an open editor too)"""
        self.f, self.names = f, names or {}
        same = (module is self.module and list(module.rules) == self.rules
                and list(notation) == self.notation)
        self.name = name
        if not same:
            self.module, self.rules, self.notation = module, list(module.rules), list(notation)
            self.close_editor(refill=False)
            self._fill()

    def set_instruction_form(self, on):
        if on != self.instruction_form:
            self.instruction_form = on
            self._fill()

    def _fill(self):
        """the text from the rules, keeping the scroll position and the current line"""
        scroll, row = self.verticalScrollBar().value(), self.textCursor().blockNumber()
        self.lines = [n if n is not None and not self.instruction_form else rule_line(r)
                      for r, n in zip(self.rules, self.notation)]
        rows = [PREFIX % i + ln.text for i, ln in enumerate(self.lines)]
        if self.blank is not None:
            rows.insert(self.blank, NEW)
        self.setExtraSelections([])
        self.setPlainText('\n'.join(rows))
        tint = QTextBlockFormat()
        tint.setBackground(FALLBACK)
        for i, n in enumerate(self.notation):
            if n is None:
                QTextCursor(self.document().findBlockByNumber(self.row_of(i))).setBlockFormat(tint)
        self.setTextCursor(QTextCursor(self.document().findBlockByNumber(
            min(row, self.document().blockCount() - 1))))
        self.verticalScrollBar().setValue(scroll)

    def rule_at(self, row):
        """the index of the rule on text line row; None for the line of a new rule"""
        if self.blank is not None and row >= self.blank:
            if row == self.blank:
                return None
            row -= 1
        return row if 0 <= row < len(self.rules) else None

    def row_of(self, i):
        """the text line of rule i"""
        return i + (self.blank is not None and i >= self.blank)

    def current(self):
        """the index of the rule the cursor is on, or None"""
        return self.rule_at(self.textCursor().blockNumber())

    def ref_at(self, rule, col):
        """the PhRef at column `col` of the line of rule `rule`, or None"""
        if not 0 <= rule < len(self.lines):
            return None
        col -= len(PREFIX % rule)
        for s, e, kind, obj in self.lines[rule].spans:
            if kind == 'ph' and s <= col < e:
                return obj
        return None

    def goto_rule(self, i):
        """make rule i current and mark it; scroll it to the middle unless it is in view"""
        if not 0 <= i < len(self.lines):
            return
        cur = QTextCursor(self.document().findBlockByNumber(self.row_of(i)))
        r = self.cursorRect(cur)
        seen = r.top() >= 0 and r.bottom() <= self.viewport().height()
        self.setTextCursor(cur)
        if not seen:
            self.centerCursor()
        sel = QTextEdit.ExtraSelection()
        sel.format.setBackground(QColor('#fff3b0'))
        sel.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        sel.cursor = cur
        self.setExtraSelections([sel])

    # --- editing
    def edit_rule(self, i=None):
        """open the editor on rule i (default: the current rule), with the line as shown"""
        i = self.current() if i is None else i
        if self.edit is not None and i is not None and 0 <= i < len(self.lines):
            self._open(i, False, self.lines[i].text)

    def insert_rule(self):
        """open the editor on a new line after the current rule"""
        if self.edit is None or self.module is None:
            return
        i = self.current()
        at = len(self.rules) if i is None else i + 1
        if i is None and self.blank is not None:
            at = self.blank                         # on the line of a new rule: start again there
        self.close_editor(refill=False)
        self.blank = at
        self._fill()
        self._open(at, True, '')

    def _open(self, i, adding, text):
        if self.editor is not None and not adding:
            self.close_editor()
        self.setTextCursor(QTextCursor(self.document().findBlockByNumber(
            self.blank if adding else self.row_of(i))))
        self.editor = RuleEditor(self, i, adding, text)
        self._place()
        self.editor.show()
        self.editor.setFocus()

    def _place(self):
        """put the editor over its line, after the prefix"""
        ed = self.editor
        if ed is None:
            return
        block = self.document().findBlockByNumber(self.blank if ed.adding else self.row_of(ed.i))
        cur = QTextCursor(block)
        cur.setPosition(block.position() + len(PREFIX % ed.i))
        r = self.cursorRect(cur)
        h = max(r.height() + 6, ed.sizeHint().height())
        ed.setGeometry(r.left(), r.top() - (h - r.height()) // 2, self.viewport().width() - r.left(), h)

    def commit(self):
        """read the editor's line and make the edit. A line that does not read keeps the editor
        open, its cursor at the problem. -> False if the line or the edit is refused"""
        ed = self.editor
        if ed is None:
            return True
        if ed.adding and not ed.text().strip():     # nothing typed on the new line: no rule
            self.status('')
            self.close_editor()
            return True
        try:
            rule = edits.read_rule(ed.text(), self.names)
        except ParseError as e:
            self.status(str(e))
            ed.setStyleSheet(REFUSED)
            if e.pos is not None:
                ed.setCursorPosition(e.pos)
            return False
        self.status('')                     # the line reads: no message about an earlier try
        self.close_editor()                 # before the edit: the window then shows the new rules
        fn = edits.insert_rule if ed.adding else edits.set_rule
        return self.edit(fn, self.f, self.name, ed.i, rule) is not False

    def close_editor(self, refill=True):
        """drop the open editor (and the line of a new rule) without an edit"""
        ed, self.editor = self.editor, None
        if ed is not None:
            had = ed.hasFocus()                 # not after a click elsewhere: the focus stays there
            ed.hide()
            ed.deleteLater()
            if had:
                self.setFocus()
        if self.blank is not None:
            self.blank = None
            if refill:
                self._fill()

    def copy_rule(self):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.insert_rule, self.f, self.name, i + 1, edits.copy_rule(self.rules[i]))

    def delete_rule(self):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.delete_rule, self.f, self.name, i)

    def move_rule(self, step):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.move_rule, self.f, self.name, i, step)

    # --- events
    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter) and self.edit is not None:
            self.edit_rule()
        elif event.key() == Qt.Key.Key_Escape and self.editor is not None:
            self.close_editor()
        else:
            super().keyPressEvent(event)

    def contextMenuEvent(self, event):
        menu = self.createStandardContextMenu(event.pos())
        if self.edit is not None:
            if not self.textCursor().hasSelection():
                self.setTextCursor(self.cursorForPosition(event.pos()))
            menu.addSeparator()
            for a in self.rule_actions:
                menu.addAction(a)
        menu.exec(event.globalPos())
        menu.deleteLater()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._place()

    def scrollContentsBy(self, dx, dy):
        super().scrollContentsBy(dx, dy)
        self._place()

    def mouseDoubleClickEvent(self, event):
        cur = self.cursorForPosition(event.position().toPoint())
        i = self.rule_at(cur.blockNumber())
        ref = self.ref_at(i, cur.positionInBlock()) if i is not None else None
        if ref is not None and ref.ph is not None and self.on_phoneme is not None:
            self.on_phoneme(ref.ph)
        elif i is not None and self.edit is not None:
            self.edit_rule(i)
        else:
            super().mouseDoubleClickEvent(event)
