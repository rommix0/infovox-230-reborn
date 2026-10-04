"""The phoneme view: name, feature bits, items and the rules that use the phoneme. The
name, the bits (click to toggle) and the item cells are editable."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListWidget,
                               QListWidgetItem, QPushButton, QTableWidget, QTableWidgetItem,
                               QVBoxLayout, QWidget)

from .. import edits
from ..checks import rule_label
from ..info import item_name, ph_name
from ..model import PNAMES, VarItem
from .views import compact, mono, plain


def free_items(ph):
    """[(id, name)] of the items ph does not have: parameters, then variables"""
    have = {it.vid if isinstance(it, VarItem) else it.pid for it in ph.items}
    return [(i, item_name(i)) for i in list(range(len(PNAMES))) + list(range(0x80, 0x100)) if i not in have]


class PhonemeView(QWidget):
    def __init__(self, on_rule=None, edit=None):
        super().__init__()
        self.on_rule = on_rule            # called with (module name, rule index)
        self.edit = edit                  # edit(function, *args): make the edit and push it
        self.ph = None
        self.what = plain(QLabel())
        self.name = QLineEdit()
        self.name.setMaximumWidth(80)
        self.name.editingFinished.connect(self._rename)
        self.hex = plain(QLabel())
        self.bits = QTableWidget(2, 32)
        compact(self.bits)
        self.bits.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.bits.setVerticalHeaderLabels(['feat', 'mask'])
        self.bits.setHorizontalHeaderLabels([str(b) for b in range(31, -1, -1)])
        self.bits.horizontalHeader().setDefaultSectionSize(24)
        self.bits.setFixedHeight(90)
        self.bits.cellClicked.connect(self._toggle)
        self.items = QTableWidget()
        compact(self.items)
        self.items.itemChanged.connect(self._cell)
        self.add = QPushButton('Add item…')
        self.add.clicked.connect(self._add_item)
        self.remove = QPushButton('Remove item')
        self.remove.clicked.connect(self._remove_item)
        self.uses_label = plain(QLabel())
        self.uses = QListWidget()
        self.uses.setFont(mono())
        self.uses.itemActivated.connect(self._jump)
        top = QHBoxLayout()
        top.addWidget(self.what)
        top.addWidget(self.name)
        top.addWidget(self.hex, 1)
        buttons = QHBoxLayout()
        buttons.addWidget(plain(QLabel('Items (value@time; time in frames from the segment start)')), 1)
        buttons.addWidget(self.add)
        buttons.addWidget(self.remove)
        lower = QHBoxLayout()
        left, right = QVBoxLayout(), QVBoxLayout()
        left.addLayout(buttons)
        left.addWidget(self.items)
        right.addWidget(self.uses_label)
        right.addWidget(self.uses)
        lower.addLayout(left, 3)
        lower.addLayout(right, 1)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.bits)
        lay.addLayout(lower)

    def set_phoneme(self, ph, uses, modules):
        """uses: [(module name, rule index)]; modules: the file's modules, for labels. The same
        phoneme shown again keeps the current cell."""
        same, self.ph = self.ph is ph, ph
        self.what.setText('defaults record, name' if ph.is_defaults else 'phoneme')
        if self.name.text() != ph_name(ph.name) or not same:
            self.name.setText(ph_name(ph.name))
        self.hex.setText('feat 0x%08x  mask 0x%08x' % (ph.feat, ph.mask))
        for row, v in enumerate((ph.feat, ph.mask)):
            for col in range(32):
                self.bits.setItem(row, col, QTableWidgetItem('●' if v >> (31 - col) & 1 else ''))
        width = max([1] + [len(it.points) for it in ph.items if not isinstance(it, VarItem)])
        cur = (self.items.currentRow(), self.items.currentColumn()) if same else None
        self.items.blockSignals(True)
        self.items.clear()
        self.items.setRowCount(len(ph.items))
        self.items.setColumnCount(2 + width)            # one empty column to add a point in
        self.items.setHorizontalHeaderLabels(['item'] + ['%d' % k for k in range(width + 1)])
        for row, it in enumerate(ph.items):
            if isinstance(it, VarItem):
                cells = [item_name(it.vid), str(it.value)]
                last = 1
            else:
                cells = [item_name(it.pid)] + ['%d@%d' % (v, t) for t, v in it.points]
                last = 1 + len(it.points)              # the cell that appends a point
            for col in range(2 + width):
                cell = QTableWidgetItem(cells[col] if col < len(cells) else '')
                if col == 0 or col > last:
                    cell.setFlags(cell.flags() & ~Qt.ItemFlag.ItemIsEditable)
                self.items.setItem(row, col, cell)
        if cur is not None and cur[0] < self.items.rowCount():
            self.items.setCurrentCell(*cur)
        self.items.blockSignals(False)
        self.uses.clear()
        self.uses_label.setText('Used by %d rule%s' % (len(uses), '' if len(uses) == 1 else 's'))
        for mod, i in uses:
            item = QListWidgetItem(rule_label(mod, i, modules[mod].rules[i]))
            item.setData(Qt.ItemDataRole.UserRole, (mod, i))
            self.uses.addItem(item)

    def _rename(self):
        if self.ph is not None and self.edit is not None:
            if not self.edit(edits.rename_phoneme, self.ph, self.name.text()):
                self.name.setText(ph_name(self.ph.name))

    def _toggle(self, row, col):
        if self.ph is not None and self.edit is not None:
            which, bit = ('feat', 'mask')[row], 31 - col
            self.edit(edits.set_bit, self.ph, which, bit, not getattr(self.ph, which) >> bit & 1)

    def _cell(self, cell):
        if self.ph is None or self.edit is None or cell.column() == 0:
            return
        row, k = cell.row(), cell.column() - 1
        if not self.edit(edits.set_cell, self.ph, row, k, cell.text()):
            self._restore()

    def _restore(self):
        """put the model's values back into the cells after a refused edit"""
        self.items.blockSignals(True)
        for row, it in enumerate(self.ph.items):
            cells = [str(it.value)] if isinstance(it, VarItem) else ['%d@%d' % (v, t) for t, v in it.points]
            for col in range(1, self.items.columnCount()):
                cell = self.items.item(row, col)
                if cell is not None:
                    cell.setText(cells[col - 1] if col - 1 < len(cells) else '')
        self.items.blockSignals(False)

    def _add_item(self):
        if self.ph is None or self.edit is None:
            return
        choices = free_items(self.ph)
        name, ok = self.ask_item([n for _, n in choices])
        if ok:
            iid = dict((n, i) for i, n in choices)[name]
            self.edit(edits.add_item, self.ph, iid)

    def ask_item(self, names):
        """-> (chosen name, ok); a dialog, replaced in tests"""
        return QInputDialog.getItem(self, 'Add item', 'Parameter or variable:', names, 0, False)

    def _remove_item(self):
        row = self.items.currentRow()
        if self.ph is not None and self.edit is not None and 0 <= row < len(self.ph.items):
            self.edit(edits.remove_item, self.ph, row)

    def _jump(self, item):
        if self.on_rule is not None:
            self.on_rule(*item.data(Qt.ItemDataRole.UserRole))
