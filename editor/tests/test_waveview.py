"""The signal view: painting, zoom, pan (offscreen)."""
import unittest

import numpy as np

from tests.qt import SlotErrors, app, needs_qt


def fake_speech():
    from ivxedit.speak import Event, Speech
    fs = 16000
    samples = (np.sin(2 * np.pi * 440 * np.arange(fs) / fs) * 20000).astype(np.int16)
    return Speech(wav=b'', samples=samples, fs=fs,
                  events=[Event(0.0, 'phoneme', ''), Event(0.5, 'phoneme', 'A:')])


@needs_qt
class SignalViewTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.waveview import SignalView
        app()
        self.v = SignalView()
        self.v.resize(800, 600)

    def test_paint(self):
        from ivxedit.gui.waveview import SignalView
        sp = fake_speech()
        with SlotErrors() as errors:
            SignalView().grab()                 # empty view
            self.v.set_results([('A', sp), ('B', sp)])
            self.v.grab()
        self.assertEqual(errors, [])
        self.assertEqual(len(self.v.rows), 2)
        self.assertEqual((self.v.t0, self.v.t1), (0.0, 1.0))

    def test_zoom_pan_clamp(self):
        v = self.v
        v.set_results([('A', fake_speech())])
        v.zoom(0.5, 400)
        self.assertAlmostEqual(v.t0, 0.25)
        self.assertAlmostEqual(v.t1, 0.75)
        v.pan(10)
        self.assertEqual(v.t1, 1.0)
        self.assertAlmostEqual(v.t1 - v.t0, 0.5)
        v.pan(-10)
        self.assertEqual(v.t0, 0.0)
        v.zoom(100, 0)
        self.assertEqual((v.t0, v.t1), (0.0, 1.0))
        v.zoom(0.5, 400)
        v.reset_view()
        self.assertEqual((v.t0, v.t1), (0.0, 1.0))
        self.assertAlmostEqual(v.x_of(v.t_of(123)), 123)


if __name__ == '__main__':
    unittest.main()
