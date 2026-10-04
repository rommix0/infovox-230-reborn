"""The Speak panel: text, options, Speak, A/B, and the signal view."""
import tempfile
from pathlib import Path

from PySide6.QtCore import QThread, QUrl, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QHBoxLayout, QLabel, QLineEdit,
                               QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget)

from ..speak import Options, SpeakError, find_ivxsay, model_bytes, speak_bytes
from .waveview import SignalView

RATES = (8000, 11025, 16000, 22050, 32000, 44100, 48000)


class SpeakJob(QThread):
    """runs the speak calls off the GUI thread; done([(title, Speech)]) or failed(message)"""
    done = Signal(list)
    failed = Signal(str)

    def __init__(self, calls, model):
        super().__init__()
        self.calls = calls          # [(title, function returning Speech)]
        self.model = model          # the model the job speaks (results for another are dropped)

    def run(self):
        try:
            self.done.emit([(title, call()) for title, call in self.calls])
        except SpeakError as e:
            self.failed.emit(str(e))
        except Exception as e:     # any other failure: report it, keep the editor running
            self.failed.emit('%s: %s' % (type(e).__name__, e))


def _spin(lo, hi, special):
    s = QSpinBox()
    s.setRange(lo - 1, hi)
    s.setSpecialValueText(special)
    s.setValue(lo - 1)
    return s


class SpeakPanel(QWidget):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.model = self.original = None
        self.job = None
        self.autoplay = True
        self._player, self._plays, self.results = None, 0, []
        self._tmp = None            # a TemporaryDirectory for the played WAVs, made on first play
        self.text = QLineEdit()
        self.text.setPlaceholderText('Text to speak')
        self.text.returnPressed.connect(self.speak)
        self.button = QPushButton('▶ Speak')
        self.button.clicked.connect(self.speak)
        self.ab = QCheckBox('A/B')
        self.ab.setToolTip('Also speak the file as it was opened, shown above the edited result')
        self.voice, self.rate, self.pitch = _spin(0, 9, 'voice'), _spin(0, 100, 'rate'), _spin(0, 100, 'pitch')
        self.fd, self.fps = _spin(0, 100, 'FD'), _spin(2, 1000, 'frames/s')
        self.fs = QComboBox()
        for r in RATES:
            self.fs.addItem('%d Hz' % r, r)
        self.fs.setCurrentIndex(RATES.index(22050))
        self.status = QLabel()
        self.view = SignalView()
        top = QHBoxLayout()
        top.addWidget(self.text, 1)
        top.addWidget(self.button)
        top.addWidget(self.ab)
        opts = QHBoxLayout()
        for w in (self.voice, self.rate, self.pitch, self.fd, self.fps, self.fs):
            opts.addWidget(w)
        opts.addWidget(self.status, 1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.view)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addLayout(opts)
        lay.addWidget(scroll, 1)

    def set_model(self, f, original):
        """f: the model to speak; original: the bytes of the file as opened (for A/B)"""
        self.model, self.original = f, original

    def options(self):
        def val(spin):
            return None if spin.value() < spin.minimum() + 1 else spin.value()
        return Options(val(self.voice), val(self.rate), val(self.pitch), val(self.fd),
                       val(self.fps), self.fs.currentData())

    def ivxsay(self):
        return find_ivxsay(self.settings.value('ivxsay') or None)

    def speak(self):
        if self.model is None or (self.job is not None and self.job.isRunning()):
            return
        exe = self.ivxsay()
        if exe is None:
            self.status.setText('ivxsay not found: run make in the repository root, or set it in File ▸ ivxsay…')
            return
        try:
            edited = model_bytes(self.model)   # here, not in the job: edits may follow at once
        except SpeakError as e:
            self.status.setText(str(e))
            return
        text, opts, orig = self.text.text(), self.options(), self.original
        calls = []
        if self.ab.isChecked():
            calls.append(('A: as opened', lambda: speak_bytes(orig, text, opts, exe)))
        calls.append(('B: edited' if self.ab.isChecked() else 'Edited',
                      lambda: speak_bytes(edited, text, opts, exe)))
        self.status.setText('Speaking…')
        self.button.setEnabled(False)
        self.job = SpeakJob(calls, self.model)
        self.job.done.connect(self._done)
        self.job.failed.connect(self._failed)
        self.job.start()

    def _done(self, results):
        self.button.setEnabled(True)
        if self.sender() is not None and self.sender().model is not self.model:
            self.status.setText('')                # spoken with a file that is no longer open
            return
        self.results = results
        self.view.set_results(results)
        sp = results[-1][1]
        self.status.setText('%.2f s, %d phonemes' % (len(sp.samples) / sp.fs, len(sp.phonemes)))
        if self.autoplay:
            self.play(sp)

    def _failed(self, message):
        self.button.setEnabled(True)
        if self.sender() is not None and self.sender().model is not self.model:
            self.status.setText('')
            return
        self.status.setText(message)

    def play(self, speech):
        """play a Speech's WAV through Qt Multimedia (silently nothing if it is unavailable)"""
        try:
            from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
        except ImportError:
            return
        if self._player is None:
            self._player = QMediaPlayer(self)
            self._player.setAudioOutput(QAudioOutput(self._player))
        self._player.stop()
        if self._tmp is None:
            self._tmp = tempfile.TemporaryDirectory(prefix='ivxedit-play-', ignore_cleanup_errors=True)
        self._plays += 1                    # a new file each time: the old one may be open
        path = Path(self._tmp.name) / ('play%d.wav' % self._plays)
        path.write_bytes(speech.wav)
        self._player.setSource(QUrl.fromLocalFile(str(path)))
        self._player.play()

    def cleanup(self):
        """wait for a running job, stop playing and remove the played WAVs"""
        if self.job is not None and self.job.isRunning():
            self.job.done.disconnect(self._done)      # no slot may run on a closing panel
            self.job.failed.disconnect(self._failed)
            self.job.wait()
        if self._player is not None:
            self._player.stop()
            self._player.setSource(QUrl())
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None
