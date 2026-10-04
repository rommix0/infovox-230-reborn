"""Shared test helpers: repository paths, the 12 language files, ivxsay, the test texts."""
import importlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGS = ['am', 'bl', 'da', 'du', 'fi', 'fr', 'ge', 'ic', 'it', 'no', 'sp', 'sw']


def ivx_path(lang):
    return ROOT / 'bin' / ('%srules.ivx' % lang)


def ivx_bytes(lang):
    return ivx_path(lang).read_bytes()


def tools_import(*names):
    """import modules from tools/ (ivx, ivxdasm, check_ivxsay)"""
    if str(ROOT / 'tools') not in sys.path:
        sys.path.insert(0, str(ROOT / 'tools'))
    return [importlib.import_module(n) for n in names]


def ivxsay():
    """ivxsay to run: $IVXSAY, else the Windows or the Linux build; None if not built"""
    env = os.environ.get('IVXSAY')
    if env:
        return Path(env).resolve()
    for p in (ROOT / 'build' / 'ivxsay.exe', ROOT / 'build' / 'linux' / 'ivxsay'):
        if p.exists():
            return p
    return None


def lang_texts():
    """{'amrules.ivx': cp1252 bytes, ...}: the texts of tests/harness.c langs[], the same
    ones tools/check_ivxsay.py speaks"""
    (check,) = tools_import('check_ivxsay')
    src = (ROOT / 'tests' / 'harness.c').read_text(encoding='latin-1')
    table = src[src.index('const Lang langs[] = {'):src.index('const int nlangs')]
    texts = {}
    for name, lits in re.findall(r'\{"(\w+rules\.ivx)",((?:\s*"(?:[^"\\]|\\.)*")+)\}', table):
        texts[name] = check.c_string(re.findall(r'"((?:[^"\\]|\\.)*)"', lits))
    return texts
