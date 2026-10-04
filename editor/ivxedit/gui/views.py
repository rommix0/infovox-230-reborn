"""Read-only views: header, tables (parameters, characters, the phoneme and module
summaries), lexicon and text/hex."""
from PySide6.QtCore import QAbstractTableModel, QModelIndex, QSortFilterProxyModel, Qt
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (QAbstractItemView, QFormLayout, QHeaderView, QLabel, QLineEdit,
                               QPlainTextEdit, QTableView, QVBoxLayout, QWidget)


def mono():
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)


def compact(table):
    """read-only, monospaced, one text line per row"""
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.setFont(mono())
    table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
    table.verticalHeader().setDefaultSectionSize(table.fontMetrics().height() + 6)


class RowsModel(QAbstractTableModel):
    """a read-only table of rows (lists of str); row_data[i] is kept for jumps"""

    def __init__(self, headers, rows, row_data=None):
        super().__init__()
        self.headers, self.rows = headers, rows
        self.row_data = row_data if row_data is not None else [None] * len(rows)

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.headers)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and index.isValid():
            return self.rows[index.row()][index.column()]
        return None

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and orientation == Qt.Orientation.Horizontal:
            return self.headers[section]
        return None


class TableView(QWidget):
    """a title and a read-only table; activated(row_data) on double-click or Enter"""

    def __init__(self, on_activate=None):
        super().__init__()
        self.title = QLabel()
        self.table = QTableView()
        compact(self.table)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.on_activate = on_activate
        self.table.activated.connect(self._activated)
        lay = QVBoxLayout(self)
        lay.addWidget(self.title)
        lay.addWidget(self.table)
        self.model = None

    def set_rows(self, title, headers, rows, row_data=None):
        self.title.setText(title)
        self.model = RowsModel(headers, [[str(c) for c in r] for r in rows], row_data)
        self.table.setModel(self.model)
        self.table.resizeColumnsToContents()

    def _activated(self, index):
        data = self.model.row_data[index.row()]
        if self.on_activate is not None and data is not None:
            self.on_activate(data)


class HeaderView(QWidget):
    def __init__(self):
        super().__init__()
        self.form = QFormLayout(self)

    def set_fields(self, fields):
        while self.form.rowCount():
            self.form.removeRow(0)
        for label, text in fields:
            value = QLabel(text)
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.form.addRow(label, value)


class TextView(QPlainTextEdit):
    """read-only monospaced text (hex dumps, the Unknown view)"""

    def __init__(self):
        super().__init__()
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)


class LexiconView(QWidget):
    """the lexicon (H[3]) with a filter on the headword"""

    def __init__(self):
        super().__init__()
        self.filter = QLineEdit()
        self.filter.setPlaceholderText('Filter headwords')
        self.table = QTableView()
        compact(self.table)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.proxy = QSortFilterProxyModel()
        self.proxy.setFilterKeyColumn(1)
        self.proxy.setFilterCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self.table.setModel(self.proxy)
        self.filter.textChanged.connect(self.proxy.setFilterFixedString)
        lay = QVBoxLayout(self)
        lay.addWidget(self.filter)
        lay.addWidget(self.table)

    def set_lexicon(self, lex):
        rows = [[str(i), o.decode('latin-1'), t.decode('latin-1')] for i, (o, t) in enumerate(lex.entries)]
        self.model = RowsModel(['#', 'Headword', 'Transcription'], rows)
        self.proxy.setSourceModel(self.model)
        self.filter.clear()
        self.table.setColumnWidth(0, 60)
        self.table.setColumnWidth(1, 220)
