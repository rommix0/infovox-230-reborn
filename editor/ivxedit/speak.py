"""Speak: run ivxsay on a model or on .ivx bytes, read back the audio and the events
(spec: GUI, Speak). ivxsay runs as its own process: the library is 32-bit, Python 64-bit."""
import io
import subprocess
import tempfile
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .checks import check
from .writer import to_bytes

ROOT = Path(__file__).resolve().parents[2]
BUILDS = (ROOT / 'build' / 'ivxsay.exe', ROOT / 'build' / 'linux' / 'ivxsay')
KINDS = ('mark', 'phoneme', 'error', 'done', '?')


class SpeakError(Exception):
    """ivxsay is missing or failed, or the model cannot be written; str() says which"""


@dataclass
class Options:
    """ivxsay options; None leaves the engine's default"""
    voice: int = None   # -v 0..9
    rate: int = None    # -r 0..100
    pitch: int = None   # -p 0..100
    fd: int = None      # -f 0..100
    fps: int = None     # -F 2..1000 frames per second (speed)
    fs: int = 22050     # -s sample rate

    def args(self):
        out = ['-s', str(self.fs)]
        for flag, v in (('-v', self.voice), ('-r', self.rate), ('-p', self.pitch),
                        ('-f', self.fd), ('-F', self.fps)):
            if v is not None:
                out += [flag, str(v)]
        return out


@dataclass
class Event:
    time: float       # seconds, as ivxsay prints it (ms resolution)
    kind: str         # 'mark', 'phoneme', 'error', 'done'
    value: str        # the phoneme's name ('' for none), else the number


@dataclass
class Speech:
    wav: bytes        # the WAV file ivxsay wrote
    samples: np.ndarray   # int16, mono
    fs: int
    events: list      # [Event]
    messages: list = field(default_factory=list)   # stderr lines that are not events

    @property
    def phonemes(self):
        return [e for e in self.events if e.kind == 'phoneme']


def find_ivxsay(override=None):
    """the ivxsay to run: `override` if it names an existing file, else build/ivxsay.exe or
    build/linux/ivxsay of this repository; None if there is none"""
    for p in ([Path(override)] if override else []) + list(BUILDS):
        if p.is_file():
            return p
    return None


def parse_events(err):
    """ivxsay -E's stderr (bytes) -> ([Event], [other lines]); an event line is
    '%10.3f %-7s value' (cli/ivxsay.c print_event)"""
    events, other = [], []
    for raw in err.split(b'\n'):
        line = raw.rstrip(b'\r').decode('latin-1')
        kind = line[11:18].rstrip()
        try:
            t = float(line[:10])
        except ValueError:
            t = None
        if t is not None and len(line) >= 19 and line[10] == ' ' and line[18] == ' ' and kind in KINDS:
            value = line[19:]
            if kind == 'phoneme':
                value = value.replace('\0', '').rstrip(' ')
            events.append(Event(t, kind, value))
        elif line:
            other.append(line)
    return events, other


def read_wav(data):
    """WAV bytes (16-bit mono) -> (int16 samples, sample rate)"""
    with wave.open(io.BytesIO(data)) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise SpeakError('ivxsay wrote a WAV that is not 16-bit mono')
        return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').copy(), w.getframerate()


def speak_bytes(data, text, opts=None, exe=None):
    """speak `text` with the .ivx file `data` -> Speech"""
    opts = opts or Options()
    exe = exe or find_ivxsay()
    if exe is None:
        raise SpeakError('ivxsay is not built: run make in the repository root')
    if not text.strip():
        raise SpeakError('there is no text to speak')
    with tempfile.TemporaryDirectory(prefix='ivxedit-') as d:
        ivx, txt, wav = (Path(d) / n for n in ('speak.ivx', 'text.txt', 'speak.wav'))
        ivx.write_bytes(data)
        txt.write_bytes(text.encode('utf-8'))
        r = subprocess.run([str(exe), '-l', str(ivx), '-i', str(txt), '-o', str(wav), '-E']
                           + opts.args(), capture_output=True, timeout=300,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        events, other = parse_events(r.stderr)
        if r.returncode != 0 or not wav.is_file():
            raise SpeakError('ivxsay failed (exit %d): %s' % (r.returncode, '; '.join(other) or 'no message'))
        out = wav.read_bytes()
    samples, fs = read_wav(out)
    return Speech(out, samples, fs, events, other)


def speak_model(f, text, opts=None, exe=None):
    """speak `text` with the model f; refuses (SpeakError) when check(f) finds problems"""
    problems = check(f)
    if problems:
        raise SpeakError('the file has %d problem(s): %s' % (
            len(problems), '; '.join(str(p) for p in problems[:3])))
    return speak_bytes(to_bytes(f), text, opts, exe)
