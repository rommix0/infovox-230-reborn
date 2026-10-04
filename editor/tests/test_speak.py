"""Speak without the GUI (spec: Testing, 10) and the signal helpers."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np

from ivxedit import dsp
from ivxedit.model import load
from ivxedit.speak import (Event, Options, SpeakError, find_ivxsay, parse_events, read_wav,
                           speak_bytes, speak_model)
from tests.util import LANGS, ivx_bytes, ivx_path, ivxsay, lang_texts

SAY = ivxsay()


def direct(lang, text):
    """ivxsay run by hand on bin/<lang>rules.ivx -> (WAV bytes, stderr)"""
    with tempfile.TemporaryDirectory() as d:
        txt, wav = Path(d) / 't.txt', Path(d) / 'o.wav'
        txt.write_bytes(text.encode('utf-8'))
        r = subprocess.run([str(SAY), '-l', str(ivx_path(lang)), '-i', str(txt), '-o', str(wav),
                            '-E', '-s', '22050'], capture_output=True, check=True)
        return wav.read_bytes(), r.stderr


class EventsTest(unittest.TestCase):
    def test_parse_events(self):
        err = (b'     0.000 phoneme \0 \r\n     0.100 phoneme G \r\n     0.210 phoneme O:\r\n'
               b'     1.000 mark    7\r\nivxsay: something\r\n     2.640 done    0\r\n')
        events, other = parse_events(err)
        self.assertEqual(events, [Event(0.0, 'phoneme', ''), Event(0.1, 'phoneme', 'G'),
                                  Event(0.21, 'phoneme', 'O:'), Event(1.0, 'mark', '7'),
                                  Event(2.64, 'done', '0')])
        self.assertEqual(other, ['ivxsay: something'])

    def test_options(self):
        self.assertEqual(Options().args(), ['-s', '22050'])
        self.assertEqual(Options(voice=2, rate=50, pitch=40, fd=30, fps=120, fs=16000).args(),
                         ['-s', '16000', '-v', '2', '-r', '50', '-p', '40', '-f', '30', '-F', '120'])

    def test_find_ivxsay(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / 'ivxsay.exe'
            exe.write_bytes(b'')
            with mock.patch('ivxedit.speak.BUILDS', ()):
                self.assertEqual(find_ivxsay(str(exe)), exe)
                self.assertIsNone(find_ivxsay(str(Path(d) / 'missing.exe')))
                self.assertIsNone(find_ivxsay())
            with mock.patch('ivxedit.speak.BUILDS', (exe,)):
                self.assertEqual(find_ivxsay(str(Path(d) / 'missing.exe')), exe)

    def test_missing_ivxsay(self):
        with mock.patch('ivxedit.speak.BUILDS', ()):
            with self.assertRaisesRegex(SpeakError, 'run make'):
                speak_bytes(ivx_bytes('sw'), 'God morgon.')

    def test_model_with_problems_is_refused(self):
        f = load(ivx_bytes('sw'))
        f.phonemes.remove(next(ph for ph in f.phonemes if ph.name == b'\0+'))   # rules use '+'
        with self.assertRaisesRegex(SpeakError, 'problem'):
            speak_model(f, 'God morgon.', exe=Path('never-run'))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class SpeakTest(unittest.TestCase):
    def test_same_as_ivxsay_in_every_language(self):
        texts = lang_texts()
        for lang in LANGS:
            text = texts['%srules.ivx' % lang].decode('cp1252')
            with self.subTest(lang=lang):
                sp = speak_model(load(ivx_bytes(lang)), text, exe=SAY)
                wav, err = direct(lang, text)
                self.assertEqual(sp.wav, wav)
                self.assertEqual(sp.events, parse_events(err)[0])
                self.assertEqual(sp.fs, 22050)
                self.assertEqual(len(sp.samples), (len(wav) - 44) // 2)
                self.assertTrue(sp.phonemes)

    def test_failure_is_reported(self):
        with self.assertRaisesRegex(SpeakError, 'ivxsay failed'):
            speak_bytes(b'not an ivx file', 'God morgon.', exe=SAY)

    def test_empty_text(self):
        with self.assertRaisesRegex(SpeakError, 'no text'):
            speak_bytes(ivx_bytes('sw'), '  ', exe=SAY)


class DspTest(unittest.TestCase):
    def test_spectrogram_peak(self):
        fs = 22050
        x = (8000 * np.sin(2 * np.pi * 1000 * np.arange(fs) / fs)).astype(np.int16)
        db, hop = dsp.spectrogram(x, fs)
        self.assertAlmostEqual(hop, 44 / fs)
        self.assertEqual(db.shape, (-(-fs // 44), dsp.NFFT // 2 + 1))
        self.assertEqual(int(db[100].argmax()), round(1000 / (fs / dsp.NFFT)))

    def test_levels(self):
        db = np.array([[0.0, -10.0], [-100.0, -35.0]])            # 2 frames, 2 bins
        lv = dsp.levels(db, range_db=70, fs=22050, fmax=None)
        self.assertEqual(lv.dtype, np.uint8)
        self.assertEqual(lv.shape, (2, 2))                          # [bins, frames]
        self.assertEqual(lv[1, 0], 255)                             # bin 0 at the bottom
        self.assertEqual(lv[1, 1], 0)

    def test_levels_crop(self):
        db = np.zeros((3, dsp.NFFT // 2 + 1))
        self.assertEqual(dsp.levels(db, fs=16000, fmax=8000).shape[0], dsp.NFFT // 2 + 1)
        self.assertEqual(dsp.levels(db, fs=32000, fmax=8000).shape[0], dsp.NFFT // 4 + 1)

    def test_minmax(self):
        lo, hi = dsp.minmax(np.arange(10), 3)
        self.assertEqual(lo.tolist(), [0, 3, 6])
        self.assertEqual(hi.tolist(), [2, 5, 9])
        lo, hi = dsp.minmax(np.arange(2), 4)
        self.assertEqual(lo.tolist(), [0, 0, 1, 1])
        self.assertEqual(len(dsp.minmax(np.array([], dtype=np.int16), 5)[0]), 5)

    def test_read_wav(self):
        import io, wave
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(16000)
            w.writeframes(np.array([1, -2, 3], dtype='<i2').tobytes())
        samples, fs = read_wav(buf.getvalue())
        self.assertEqual((samples.tolist(), fs), ([1, -2, 3], 16000))


if __name__ == '__main__':
    unittest.main()
