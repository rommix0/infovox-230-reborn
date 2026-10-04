"""Shift every phoneme id and require identical audio from ivxsay (spec: Testing, 3)."""
import os
import subprocess
import tempfile
import unittest

from ivxedit.model import ParamItem, Phoneme, load
from ivxedit.rules import ph_positions
from ivxedit.writer import to_bytes
from tests.util import LANGS, ivx_bytes, ivxsay, lang_texts

SAY = ivxsay()


def shifted(data):
    """the file with a 12-byte record no rule or text can reach inserted after the defaults
    record, so every later phoneme id moves by 12"""
    f = load(data)
    f.phonemes.insert(1, Phoneme(b'\x01\x01', 0, 0, []))
    return to_bytes(f)


def ref_names(f):
    """every phoneme-id operand in rule order, as (phoneme name or None, delta); names are
    unique in every shipped inventory"""
    return [a if isinstance(a, int) else (None if a.ph is None else a.ph.name, a.delta)
            for m in f.modules.values() for r in m.rules for i in r.instrs
            for a in (i.args[k] for k in ph_positions(i))]


class ReferencesTest(unittest.TestCase):
    def test_shifted_file_has_the_same_references(self):
        # also covers rules the test texts never reach, such as the 8 terminator references
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                self.assertEqual(ref_names(load(shifted(data))), ref_names(load(data)))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class RelocationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.texts = lang_texts()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def speak(self, data, text):
        ivx, txt, wav = (os.path.join(self.tmp.name, n) for n in ('x.ivx', 'x.txt', 'x.wav'))
        with open(ivx, 'wb') as f:
            f.write(data)
        with open(txt, 'wb') as f:
            f.write(text)
        subprocess.run([str(SAY), '-l', ivx, '-e', 'cp1252', '-i', txt, '-o', wav],
                       check=True, capture_output=True)
        with open(wav, 'rb') as f:
            return f.read()

    def test_shifted_ids_speak_the_same(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                data, text = ivx_bytes(lang), self.texts['%srules.ivx' % lang]
                moved = shifted(data)
                self.assertNotEqual(moved, data)
                self.assertEqual(self.speak(moved, text), self.speak(data, text))

    def test_control_edit_changes_the_audio(self):
        # proves the comparison can fail: raise every F1 point of every phoneme by 20
        for lang in LANGS:
            with self.subTest(lang=lang):
                data, text = ivx_bytes(lang), self.texts['%srules.ivx' % lang]
                f = load(data)
                for ph in f.phonemes[1:]:
                    for it in ph.items:
                        if isinstance(it, ParamItem) and it.pid == 5:
                            it.points = [(t, min(255, v + 20)) for t, v in it.points]
                self.assertNotEqual(self.speak(to_bytes(f), text), self.speak(data, text))


if __name__ == '__main__':
    unittest.main()
