"""Check that build/ivxsay.exe speaks exactly like the DLL.

tests/tts_cmp.c ('tts_cmp bin wav') compares the port's pipeline with the DLL's sample
for sample and writes the port's audio to build/wav/<lang>_0.wav (engine configuration:
mode 8, voice 0, 22050 Hz). This script speaks the same texts (the langs[] table of
tests/harness.c) with ivxsay and requires byte-identical WAV files. It also requires
'ivxsay -d' to say the same as the text with the dictionary's replacements made, and to
reject malformed dictionary lines with 'file:line:'.
Run from the repository root after tts_cmp:  python tools/check_ivxsay.py

Usage: python tools/check_ivxsay.py [path/to/ivxsay]
"""
import os
import re
import subprocess
import sys
import tempfile

ESC = {'n': b'\n', 't': b'\t', 'r': b'\r', '"': b'"', '\\': b'\\'}


def c_string(literals):
    """bytes of adjacent C string literals (plain chars, \\xHH and simple escapes)"""
    out = b''
    for s in literals:
        i = 0
        while i < len(s):
            if s[i] != '\\':
                out += s[i].encode('latin-1')
                i += 1
            elif s[i + 1] == 'x':
                h = re.match(r'[0-9a-fA-F]+', s[i + 2:]).group(0)
                out += bytes([int(h, 16)])
                i += 2 + len(h)
            else:
                out += ESC[s[i + 1]]
                i += 2
    return out


DICT = ('# test dictionary\n'
        '\n'
        'USA = U S A\n'
        '!Kalle = Karl        # exact case: "kalle" stays\n'
        '?blorf = plårf        # fallback: only words the lexicon does not know\n'
        'här = där\n'
        'hej = /H[J/          # phonetic\n')
DICT_TEXT = 'Kalle och kalle bor i USA. Det var en blorf här.'
DICT_SAID = 'Karl och kalle bor i U S A. Det var en plårf där.'
BAD_DICTS = [('USA U S A\n', 1), ('ok = fine\n= nothing\n', 2), ('x = /H[J\n', 1),
             ('x = /H[J/ y\n', 1), ('two words = x\n', 1), ('?a = two words\n', 1),
             ('ok = fine\n?a = /H[J/\n', 2)]


def check_dict(say, tmp):
    """ivxsay -d: the same WAV as the text with the replacements made; bad lines rejected"""
    def run(args, text):
        with open(os.path.join(tmp, 'text.txt'), 'w', encoding='utf-8') as f:
            f.write(text)
        return subprocess.run([say, '-l', os.path.join('bin', 'swrules.ivx'), '-i', os.path.join(tmp, 'text.txt')]
                              + args, capture_output=True)

    path = os.path.join(tmp, 'dict.txt')
    a, b = os.path.join(tmp, 'a.wav'), os.path.join(tmp, 'b.wav')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(DICT)
    ok = run(['-d', path, '-o', a], DICT_TEXT).returncode == 0 and run(['-o', b], DICT_SAID).returncode == 0
    if ok:
        with open(a, 'rb') as fa, open(b, 'rb') as fb:
            ok = fa.read() == fb.read()
    print('%-12s %s' % ('-d', 'identical' if ok else 'DIFFERENT'))
    bad = not ok

    # a UTF-8 BOM (EF BB BF) in front of the dictionary, as Windows tools commonly write,
    # must be accepted and say the same thing (finding 1)
    c = os.path.join(tmp, 'c.wav')
    with open(path, 'wb') as f:
        f.write(b'\xef\xbb\xbf' + DICT.encode('utf-8'))
    okbom = run(['-d', path, '-o', c], DICT_TEXT).returncode == 0
    if okbom:
        with open(c, 'rb') as fc, open(b, 'rb') as fb:
            okbom = fc.read() == fb.read()
    print('%-12s %s' % ('-d BOM', 'identical' if okbom else 'DIFFERENT'))
    bad += not okbom

    for text, line in BAD_DICTS:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
        r = run(['-d', path, '-o', a], DICT_TEXT)
        if r.returncode != 1 or ('%s:%d:' % (path, line)).encode() not in r.stderr:
            print('-d %r: exit %d, %r' % (text, r.returncode, r.stderr))
            bad += 1
    return bad


def main():
    src = open('tests/harness.c', encoding='latin-1').read()
    table = src[src.index('const Lang langs[] = {'):src.index('const int nlangs')]
    langs = re.findall(r'\{"(\w+rules\.ivx)",((?:\s*"(?:[^"\\]|\\.)*")+)\}', table)
    say = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'ivxsay.exe'))
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, lits in langs:
            text = os.path.join(tmp, 'text.txt')
            wav = os.path.join(tmp, 'say.wav')
            with open(text, 'wb') as f:
                f.write(c_string(re.findall(r'"((?:[^"\\]|\\.)*)"', lits)))
            subprocess.run([say, '-l', os.path.join('bin', name), '-e', 'cp1252', '-i', text, '-o', wav],
                           check=True, capture_output=True)
            with open(wav, 'rb') as a, open(os.path.join('build', 'wav', name[:-4] + '_0.wav'), 'rb') as b:
                same = a.read() == b.read()
            bad += not same
            print('%-12s %s' % (name, 'identical' if same else 'DIFFERENT'))
        nlang = len(langs) - bad
        bad += check_dict(say, tmp)
    print('\nivxsay: %d of %d languages identical to the DLL-verified audio\n%d failures'
          % (nlang, len(langs), bad))
    return 1 if bad or len(langs) != 12 else 0


if __name__ == '__main__':
    sys.exit(main())
