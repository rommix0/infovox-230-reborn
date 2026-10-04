"""Qt test helpers: an offscreen QApplication, a skip when PySide6 is missing, and a
catcher for exceptions raised inside slots (Qt prints them and carries on)."""
import os
import sys
import tempfile
import unittest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
try:
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication
except ImportError:
    QApplication = None

needs_qt = unittest.skipIf(QApplication is None,
                           'PySide6 is not installed: pip install -r editor/requirements.txt')


def app():
    return QApplication.instance() or QApplication([])


def temp_settings(test):
    """QSettings in a temporary INI file, removed after the test"""
    d = tempfile.TemporaryDirectory()
    test.addCleanup(d.cleanup)
    return QSettings(os.path.join(d.name, 'ivxedit.ini'), QSettings.Format.IniFormat)


class SlotErrors:
    """with SlotErrors() as errs: ... -> errs holds the exceptions slots raised"""

    def __enter__(self):
        self.errors, self._old = [], sys.excepthook
        sys.excepthook = lambda t, v, tb: self.errors.append(v)
        return self.errors

    def __exit__(self, *exc):
        sys.excepthook = self._old
        return False
