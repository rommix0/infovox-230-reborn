"""Views: header form, tables (parameters, characters, the phoneme and module summaries),
lexicon and text/hex. Editable cells and fields report edits through a callback; the
window turns them into undoable edits and refreshes the view."""
from PySide6.QtCore import QAbstractTableModel, QModelIndex, QSortFilterProxyModel, Qt
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (QAbstractItemView, QFormLayout, QHBoxLayout, QHeaderView, QLabel,
                               QLineEdit, QPlainTextEdit, QPushButton, QTableView, QVBoxLayout,
                               QWidget)


def mono():
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)


def compact(table):
    """monospaced, one text line per row, edited by double-click or typing (where a cell
    is editable at all)"""
    table.setEditTriggers(QAbstractItemView.EditTrigger.DoubleClicked
                          | QAbstractItemView.EditTrigger.EditKeyPressed
                          | QAbstractItemView.EditTrigger.AnyKeyPressed)
    table.setFont(mono())
    table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
    table.verticalHeader().setDefaultSectionSize(table.fontMetrics().height() + 6)


def plain(label):
    """a QLabel that shows its text as it is, never as rich text"""
    label.setTextFormat(Qt.TextFormat.PlainText)
    return label


class RowsModel(QAbstractTableModel):
    """a table of rows (lists of str); row_data[i] is kept for jumps. Cells for which
    editable(row, col) is true can be edited: on_edit(row, col, text) -> True if taken."""

    def __init__(self, headers, rows, row_data=None, editable=None, on_edit=None):
        super().__init__()
        self.headers, self.rows = headers, rows
        self.row_data = row_data if row_data is not None else [None] * len(rows)
        self.editable, self.on_edit = editable, on_edit

    def update(self, rows, row_data=None):
        """new contents; the view keeps its scroll position and current cell when the
        number of rows stays the same"""
        row_data = row_data if row_data is not None else [None] * len(rows)
        if len(rows) == len(self.rows):
            self.rows, self.row_data = rows, row_data
            if rows:
                self.dataChanged.emit(self.index(0, 0), self.index(len(rows) - 1, len(self.headers) - 1))
        else:
            self.beginResetModel()
            self.rows, self.row_data = rows, row_data
            self.endResetModel()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.headers)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if role in (Qt.ItemDataRole.DisplayRole, Qt.ItemDataRole.EditRole) and index.isValid():
            return self.rows[index.row()][index.column()]
        return None

    def flags(self, index):
        fl = super().flags(index)
        if self.editable is not None and index.isValid() and self.editable(index.row(), index.column()):
            fl |= Qt.ItemFlag.ItemIsEditable
        return fl

    def setData(self, index, value, role=Qt.ItemDataRole.EditRole):
        if role != Qt.ItemDataRole.EditRole or self.on_edit is None or not index.isValid():
            return False
        return bool(self.on_edit(index.row(), index.column(), str(value)))

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and orientation == Qt.Orientation.Horizontal:
            return self.headers[section]
        return None


class TableView(QWidget):
    """a title, buttons and a table; activated(row_data) on double-click or Enter in a
    cell that is not editable"""

    def __init__(self, on_activate=None):
        super().__init__()
        self.title = plain(QLabel())
        self.table = QTableView()
        compact(self.table)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.on_activate = on_activate
        self.table.activated.connect(self._activated)
        self.bar = QHBoxLayout()
        self.bar.addStretch(1)
        self.buttons = {}
        lay = QVBoxLayout(self)
        top = QHBoxLayout()
        top.addWidget(self.title, 1)
        top.addLayout(self.bar)
        lay.addLayout(top)
        lay.addWidget(self.table)
        self.model = None

    def set_rows(self, title, headers, rows, row_data=None, editable=None, on_edit=None, actions=()):
        """actions: [(button text, callback(row_data of the current row, or None))]. The same
        table shown again (same title and headers) keeps its scroll position."""
        rows = [[str(c) for c in r] for r in rows]
        same = self.model is not None and self.title.text() == title and self.model.headers == headers
        if same:
            scroll = self.table.verticalScrollBar().value()
            self.model.editable, self.model.on_edit = editable, on_edit
            self.model.update(rows, row_data)
            self.table.verticalScrollBar().setValue(scroll)
        else:
            self.title.setText(title)
            self.model = RowsModel(headers, rows, row_data, editable, on_edit)
            self.table.setModel(self.model)
            self.table.resizeColumnsToContents()
        self._actions(actions)

    def _actions(self, actions):
        if [a[0] for a in actions] == list(self.buttons):
            for text, fn in actions:
                self.buttons[text].callback = fn
            return
        for b in self.buttons.values():
            self.bar.removeWidget(b)
            b.setParent(None)
            b.deleteLater()
        self.buttons = {}
        for text, fn in actions:
            b = QPushButton(text)
            b.callback = fn
            b.clicked.connect(lambda checked=False, b=b: b.callback(self.current()))
            self.bar.addWidget(b)
            self.buttons[text] = b

    def current(self):
        """the row_data of the current row, or None"""
        i = self.table.currentIndex()
        return self.model.row_data[i.row()] if self.model is not None and i.isValid() else None

    def select(self, row):
        if self.model is not None and 0 <= row < len(self.model.rows):
            self.table.setCurrentIndex(self.model.index(row, 0))

    def _activated(self, index):
        if self.model.flags(index) & Qt.ItemFlag.ItemIsEditable:
            return
        data = self.model.row_data[index.row()]
        if self.on_activate is not None and data is not None:
            self.on_activate(data)


class HeaderView(QWidget):
    """label: value rows; a field with a key is a line edit: on_edit(key, text) when it
    is edited"""

    def __init__(self, on_edit=None):
        super().__init__()
        self.on_edit = on_edit
        self.form = QFormLayout(self)
        self.widgets, self.labels = {}, []

    def set_fields(self, fields):
        """fields: [(label, text, key or None)]"""
        if [f[0] for f in fields] == self.labels:
            for label, text, key in fields:
                w = self.widgets[label]
                if w.text() != text:
                    w.setText(text)
            return
        while self.form.rowCount():
            self.form.removeRow(0)
        self.widgets, self.labels = {}, [f[0] for f in fields]
        for label, text, key in fields:
            if key is None:
                w = plain(QLabel(text))
                w.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            else:
                w = QLineEdit(text)
                w.editingFinished.connect(lambda w=w, key=key: self._edited(key, w))
            self.widgets[label] = w
            self.form.addRow(label, w)

    def _edited(self, key, w):
        if self.on_edit is not None:
            self.on_edit(key, w.text())


class TextView(QPlainTextEdit):
    """read-only monospaced text (hex dumps, the Unknown view)"""

    def __init__(self):
        super().__init__()
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)


class LexiconView(QWidget):
    """the lexicon (H[3]) with a filter on the headword; headword and transcription are
    editable: on_edit(entry index, word, transcription); on_add(), on_remove(index)"""

    def __init__(self, on_edit=None, on_add=None, on_remove=None):
        super().__init__()
        self.on_edit, self.on_add, self.on_remove = on_edit, on_add, on_remove
        self.filter = QLineEdit()
        self.filter.setPlaceholderText('Filter headwords')
        self.note = plain(QLabel('As stored: the 7-bit national notation (Swedish [ = Ä, \\ = Ö, ] = Å)'))
        self.add = QPushButton('Add entry')
        self.add.clicked.connect(lambda: self.on_add and self.on_add())
        self.remove = QPushButton('Remove entry')
        self.remove.clicked.connect(self._remove)
        self.table = QTableView()
        compact(self.table)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.proxy = QSortFilterProxyModel()
        self.proxy.setFilterKeyColumn(1)
        self.proxy.setFilterCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self.table.setModel(self.proxy)
        self.filter.textChanged.connect(self.proxy.setFilterFixedString)
        self.model = None
        top = QHBoxLayout()
        top.addWidget(self.filter, 1)
        top.addWidget(self.add)
        top.addWidget(self.remove)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.note)
        lay.addWidget(self.table)

    def set_lexicon(self, lex):
        """show lex; shown again, the view keeps its filter and scroll position"""
        rows = [[str(i), o.decode('latin-1'), t.decode('latin-1')] for i, (o, t) in enumerate(lex.entries)]
        if self.model is None:
            self.model = RowsModel(['#', 'Headword', 'Transcription'], rows,
                                   editable=lambda r, c: c > 0, on_edit=self._edited)
            self.proxy.setSourceModel(self.model)
            self.table.setColumnWidth(0, 60)
            self.table.setColumnWidth(1, 220)
        else:
            scroll = self.table.verticalScrollBar().value()
            self.model.update(rows)
            self.table.verticalScrollBar().setValue(scroll)

    def reset(self):
        """forget the shown lexicon (another file was opened)"""
        self.model = None
        self.proxy.setSourceModel(None)
        self.filter.clear()

    def select(self, i):
        """make entry i current (clearing the filter if it hides the entry)"""
        if self.model is None or not 0 <= i < len(self.model.rows):
            return
        index = self.proxy.mapFromSource(self.model.index(i, 1))
        if not index.isValid():
            self.filter.clear()
            index = self.proxy.mapFromSource(self.model.index(i, 1))
        self.table.setCurrentIndex(index)
        self.table.scrollTo(index)

    def current(self):
        """the current entry's index, or None"""
        index = self.table.currentIndex()
        return self.proxy.mapToSource(index).row() if index.isValid() else None

    def _edited(self, row, col, text):
        _, word, trans = self.model.rows[row]
        word, trans = (text, trans) if col == 1 else (word, text)
        return self.on_edit is not None and self.on_edit(row, word, trans)

    def _remove(self):
        i = self.current()
        if i is not None and self.on_remove is not None:
            self.on_remove(i)
