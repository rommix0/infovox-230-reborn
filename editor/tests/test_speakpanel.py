"""The Speak panel, offscreen (spec: Testing, 10)."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_path, ivxsay

SAY = ivxsay()


def wait_for(panel):
    panel.job.wait(120000)
    for _ in range(20):
        app().processEvents()
        if panel.button.isEnabled():
            return


@needs_qt
class SpeakPanelTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.settings = temp_settings(self)
        self.win = MainWindow(self.settings)
        self.addCleanup(self.win.close)
        self.panel = self.win.speak
        self.panel.autoplay = False

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_speak_matches_ivxsay(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        p = self.panel
        p.text.setText('God morgon.')
        p.ab.setChecked(True)
        with SlotErrors() as errors:
            p.button.click()
            wait_for(p)
        self.assertEqual(errors, [])
        self.assertEqual([t for t, _ in p.results], ['A: as opened', 'B: edited'])
        with tempfile.TemporaryDirectory() as d:
            txt, wav = Path(d) / 't.txt', Path(d) / 'o.wav'
            txt.write_bytes('God morgon.'.encode('utf-8'))
            r = subprocess.run([str(SAY), '-l', str(ivx_path('sw')), '-i', str(txt), '-o', str(wav),
                                '-E', '-s', '22050'], capture_output=True, check=True)
            direct = wav.read_bytes()
        from ivxedit.speak import parse_events
        events = parse_events(r.stderr)[0]
        for title, sp in p.results:
            self.assertEqual(sp.wav, direct, title)
        self.assertEqual([row.speech.events for row in p.view.rows], [events, events])
        self.assertEqual(len(p.view.rows), 2)

    def test_missing_ivxsay(self):
        self.settings.setValue('ivxsay', str(Path(tempfile.gettempdir()) / 'no-such-ivxsay.exe'))
        self.win.open_file(ivx_path('sw'))
        self.panel.text.setText('God morgon.')
        with mock.patch('ivxedit.speak.BUILDS', ()):
            self.panel.button.click()
        self.assertIsNone(self.panel.job)
        self.assertIn('run make', self.panel.status.text())

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_refused_model_reports(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        del self.win.f.modules['M0']           # M0 is required (checks)
        self.panel.text.setText('God morgon.')
        self.panel.button.click()
        wait_for(self.panel)
        self.assertIn('M0', self.panel.status.text())

    def test_options(self):
        p = self.panel
        self.assertEqual(p.options().args(), ['-s', '22050'])
        p.voice.setValue(3)
        p.fps.setValue(150)
        p.fs.setCurrentIndex(p.fs.findData(16000))
        self.assertEqual(p.options().args(), ['-s', '16000', '-v', '3', '-F', '150'])


if __name__ == '__main__':
    unittest.main()
