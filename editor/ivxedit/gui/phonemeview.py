"""The phoneme view: name, feature bits, items and the rules that use the phoneme."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QTableWidget,
                               QTableWidgetItem, QVBoxLayout, QWidget)

from ..checks import rule_label
from ..info import item_name, ph_name
from ..model import VarItem
from .views import compact, mono


class PhonemeView(QWidget):
    def __init__(self, on_rule=None):
        super().__init__()
        self.on_rule = on_rule            # called with (module name, rule index)
        self.name = QLabel()
        self.bits = QTableWidget(2, 32)
        compact(self.bits)
        self.bits.setVerticalHeaderLabels(['feat', 'mask'])
        self.bits.setHorizontalHeaderLabels([str(b) for b in range(31, -1, -1)])
        self.bits.horizontalHeader().setDefaultSectionSize(24)
        self.bits.setFixedHeight(90)
        self.items = QTableWidget()
        compact(self.items)
        self.uses_label = QLabel()
        self.uses = QListWidget()
        self.uses.setFont(mono())
        self.uses.itemActivated.connect(self._jump)
        lower = QHBoxLayout()
        left, right = QVBoxLayout(), QVBoxLayout()
        left.addWidget(QLabel('Items (value@time; time in frames from the segment start)'))
        left.addWidget(self.items)
        right.addWidget(self.uses_label)
        right.addWidget(self.uses)
        lower.addLayout(left, 3)
        lower.addLayout(right, 1)
        lay = QVBoxLayout(self)
        lay.addWidget(self.name)
        lay.addWidget(self.bits)
        lay.addLayout(lower)

    def set_phoneme(self, ph, uses, modules):
        """uses: [(module name, rule index)]; modules: the file's modules, for labels"""
        what = 'defaults record' if ph.is_defaults else 'phoneme %s' % ph_name(ph.name)
        self.name.setText('%s    feat 0x%08x  mask 0x%08x' % (what, ph.feat, ph.mask))
        for row, v in enumerate((ph.feat, ph.mask)):
            for col in range(32):
                self.bits.setItem(row, col, QTableWidgetItem('●' if v >> (31 - col) & 1 else ''))
        width = max([1] + [len(it.points) for it in ph.items if not isinstance(it, VarItem)])
        self.items.clear()
        self.items.setRowCount(len(ph.items))
        self.items.setColumnCount(1 + width)
        self.items.setHorizontalHeaderLabels(['item'] + ['%d' % k for k in range(width)])
        for row, it in enumerate(ph.items):
            if isinstance(it, VarItem):
                cells = [item_name(it.vid), str(it.value)]
            else:
                cells = [item_name(it.pid)] + ['%d@%d' % (v, t) for t, v in it.points]
            for col, text in enumerate(cells):
                self.items.setItem(row, col, QTableWidgetItem(text))
        self.uses.clear()
        self.uses_label.setText('Used by %d rule%s' % (len(uses), '' if len(uses) == 1 else 's'))
        for mod, i in uses:
            item = QListWidgetItem(rule_label(mod, i, modules[mod].rules[i]))
            item.setData(Qt.ItemDataRole.UserRole, (mod, i))
            self.uses.addItem(item)

    def _jump(self, item):
        if self.on_rule is not None:
            self.on_rule(*item.data(Qt.ItemDataRole.UserRole))
