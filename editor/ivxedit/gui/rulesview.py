"""The rules view: one module, one rule per line, in rule notation where it says the rule
exactly and in instruction form otherwise (those lines tinted); highlighted; double-click
a phoneme to jump to it."""
from PySide6.QtGui import (QColor, QSyntaxHighlighter, QTextBlockFormat, QTextCharFormat, QTextCursor,
                           QTextFormat)
from PySide6.QtWidgets import QPlainTextEdit, QTextEdit

from ..listing import rule_line
from .views import mono

PREFIX = '%5d  '          # the rule index, as in the Problems panel's "M0 rule 12"
COLOURS = {'op': '#1f5fbf', 'ph': '#a0400a', 'num': '#2a7f2a', 'feat': '#7a2a9a',
           'param': '#8a6d00', 'raw': '#c00000'}
FALLBACK = QColor('#f1ece0')   # background of a rule shown in instruction form for want of notation


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
        n = self.currentBlock().blockNumber()
        if n < len(self.view.lines):
            for s, e, kind, _ in self.view.lines[n].spans:
                self.setFormat(len(PREFIX % n) + s, e - s, self.formats[kind])


class RulesView(QPlainTextEdit):
    def __init__(self, on_phoneme=None):
        super().__init__()
        self.on_phoneme = on_phoneme      # called with a Phoneme
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.document().setUndoRedoEnabled(False)
        self.lines = []                   # the Line shown for each rule
        self.module = None
        self.notation = []                # per rule: its Line in notation, or None
        self.instruction_form = False     # show every rule in instruction form
        self._hl = _Highlighter(self.document(), self)

    def set_module(self, name, module, notation):
        """show module; notation: per rule, its notation Line or None (notation.notation_line)"""
        self.module, self.notation = module, notation
        self._fill()

    def set_instruction_form(self, on):
        if on != self.instruction_form:
            self.instruction_form = on
            self._fill()

    def _fill(self):
        rules = self.module.rules if self.module is not None else []
        self.lines = [n if n is not None and not self.instruction_form else rule_line(r)
                      for r, n in zip(rules, self.notation)]
        self.setPlainText('\n'.join(PREFIX % i + ln.text for i, ln in enumerate(self.lines)))
        tint = QTextBlockFormat()
        tint.setBackground(FALLBACK)
        for i, n in enumerate(self.notation):
            if n is None:
                QTextCursor(self.document().findBlockByNumber(i)).setBlockFormat(tint)

    def ref_at(self, rule, col):
        """the PhRef at column `col` of rule line `rule`, or None"""
        if not 0 <= rule < len(self.lines):
            return None
        col -= len(PREFIX % rule)
        for s, e, kind, obj in self.lines[rule].spans:
            if kind == 'ph' and s <= col < e:
                return obj
        return None

    def goto_rule(self, i):
        if not 0 <= i < len(self.lines):
            return
        cur = QTextCursor(self.document().findBlockByNumber(i))
        self.setTextCursor(cur)
        self.centerCursor()
        sel = QTextEdit.ExtraSelection()
        sel.format.setBackground(QColor('#fff3b0'))
        sel.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        sel.cursor = cur
        self.setExtraSelections([sel])

    def mouseDoubleClickEvent(self, event):
        cur = self.cursorForPosition(event.position().toPoint())
        ref = self.ref_at(cur.blockNumber(), cur.positionInBlock())
        if ref is not None and ref.ph is not None and self.on_phoneme is not None:
            self.on_phoneme(ref.ph)
        else:
            super().mouseDoubleClickEvent(event)
