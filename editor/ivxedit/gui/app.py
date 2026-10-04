"""Start the editor: python editor/ivxedit.pyw [file.ivx]"""
import sys

from PySide6.QtCore import QSettings
from PySide6.QtWidgets import QApplication

from .mainwindow import MainWindow


def main(argv=None):
    argv = sys.argv if argv is None else argv
    app = QApplication.instance() or QApplication(argv)
    win = MainWindow(QSettings('infovox230', 'ivxedit'))
    win.show()
    if len(argv) > 1:
        win.open_path(argv[1])
    return app.exec()
