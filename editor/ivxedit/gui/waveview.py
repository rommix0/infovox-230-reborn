"""Waveform, spectrogram and phoneme strip of one or more Speech results on one time axis.
Wheel zooms around the pointer, dragging pans, double-click shows everything."""
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen
from PySide6.QtWidgets import QSizePolicy, QWidget

from .. import dsp

WAVE_H, SPEC_H, STRIP_H, TITLE_H, GAP = 70, 140, 22, 16, 8


class Row:
    """one Speech, prepared for drawing"""

    def __init__(self, title, speech):
        self.title, self.speech = title, speech
        db, self.hop = dsp.spectrogram(speech.samples, speech.fs)
        lv = dsp.levels(db, fs=speech.fs)
        lv = 255 - lv                       # dark = loud
        self.image = QImage(lv.data, lv.shape[1], lv.shape[0], lv.strides[0],
                            QImage.Format.Format_Grayscale8).copy()
        self.duration = len(speech.samples) / speech.fs


class SignalView(QWidget):
    def __init__(self):
        super().__init__()
        self.rows = []
        self.t0, self.t1 = 0.0, 1.0
        self._drag = None
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumHeight(TITLE_H + WAVE_H + SPEC_H + STRIP_H)

    def set_results(self, results):
        """results: [(title, Speech)], drawn top to bottom"""
        self.rows = [Row(t, s) for t, s in results]
        self.reset_view()
        self.setMinimumHeight(max(1, len(self.rows)) * (TITLE_H + WAVE_H + SPEC_H + STRIP_H + GAP))
        self.update()

    def reset_view(self):
        self.t0, self.t1 = 0.0, max([0.1] + [r.duration for r in self.rows])
        self.update()

    def total(self):
        return max([0.1] + [r.duration for r in self.rows])

    # --- time <-> x
    def x_of(self, t):
        return (t - self.t0) / (self.t1 - self.t0) * self.width()

    def t_of(self, x):
        return self.t0 + x / max(1, self.width()) * (self.t1 - self.t0)

    def zoom(self, factor, at_x):
        t = self.t_of(at_x)
        span = min(self.total(), max(0.01, (self.t1 - self.t0) * factor))
        self.t0 = t - (at_x / max(1, self.width())) * span
        self.t1 = self.t0 + span
        self._clamp()

    def pan(self, dt):
        self.t0 += dt
        self.t1 += dt
        self._clamp()

    def _clamp(self):
        span = self.t1 - self.t0
        self.t0 = min(max(0.0, self.t0), max(0.0, self.total() - span))
        self.t1 = self.t0 + span
        self.update()

    # --- events
    def wheelEvent(self, e):
        self.zoom(0.8 if e.angleDelta().y() > 0 else 1.25, e.position().x())

    def mousePressEvent(self, e):
        self._drag = e.position().x()

    def mouseMoveEvent(self, e):
        if self._drag is not None:
            x = e.position().x()
            self.pan(self.t_of(self._drag) - self.t_of(x))
            self._drag = x

    def mouseReleaseEvent(self, e):
        self._drag = None

    def mouseDoubleClickEvent(self, e):
        self.reset_view()

    # --- drawing
    def paintEvent(self, e):
        p = QPainter(self)
        pal = self.palette()
        p.fillRect(self.rect(), pal.base())
        if not self.rows:
            p.setPen(pal.text().color())
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, 'Type a text and press Speak')
            p.end()
            return
        y = 0
        for row in self.rows:
            p.setPen(pal.text().color())
            p.drawText(4, y + TITLE_H - 4, row.title)
            y += TITLE_H
            self._wave(p, row, y)
            y += WAVE_H
            self._spec(p, row, y)
            y += SPEC_H
            self._strip(p, row, y)
            y += STRIP_H + GAP
        p.end()

    def _sample_range(self, row):
        s = row.speech
        return tuple(max(0, min(len(s.samples), int(t * s.fs))) for t in (self.t0, self.t1))

    def wave_columns(self, row):
        """-> (start_x, n): the columns the row's samples cover in the current view"""
        s = row.speech
        i0, i1 = self._sample_range(row)
        x0 = max(0, round(self.x_of(i0 / s.fs)))
        x1 = min(self.width(), round(self.x_of(i1 / s.fs)))
        return x0, max(0, x1 - x0)

    def _wave(self, p, row, y):
        s = row.speech
        i0, i1 = self._sample_range(row)
        x0, n = self.wave_columns(row)
        if i1 <= i0 or n <= 0:
            return
        lo, hi = dsp.minmax(s.samples[i0:i1], n)
        mid, scale = y + WAVE_H / 2, WAVE_H / 2 / 32768
        p.setPen(QPen(QColor('#3060a0')))
        for k in range(n):
            x = x0 + k
            p.drawLine(x, int(mid - hi[k] * scale), x, int(mid - lo[k] * scale))

    def _spec(self, p, row, y):
        f0, f1 = self.t0 / row.hop, self.t1 / row.hop
        p.drawImage(QRectF(0, y, self.width(), SPEC_H), row.image,
                    QRectF(f0, 0, f1 - f0, row.image.height()))

    def _strip(self, p, row, y):
        pal = self.palette()
        p.setPen(pal.text().color())
        p.drawLine(0, y, self.width(), y)
        for ev in row.speech.phonemes:
            x = self.x_of(ev.time)
            if -50 <= x <= self.width():
                p.drawLine(int(x), y, int(x), y + STRIP_H)
                p.drawText(int(x) + 2, y + STRIP_H - 6, ev.value)
