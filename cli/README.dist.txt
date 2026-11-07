infovox230
==========

A standalone build of the Infovox 230 text-to-speech engine (Telia Promotor AB,
1996-1998): the RULSYS rule interpreter and formant synthesizer, re-implemented in C
from the original engine and byte-identical to its audio. No SAPI and no original
DLL are needed.

Contents
--------

  ivxsay           command-line text to speech (ivxsay.exe on Windows)
  infovox230.dll   the engine library (libinfovox230.so on Linux)
  lang/v1.12/      the 12 earlier language files (1988-1998)
  lang/v2.2/       the 12 language files of the Infovox 230 v2.2 release
                   (1999-2002): newer rules, and large lexicons in most languages
  sdk/             infovox230.h, the library API; on Windows also the MinGW import
                   library libinfovox230.dll.a and infovox230.def (for MSVC:
                   lib /def:infovox230.def /machine:x86)

The binaries are 32-bit (x86).

ivxsay
------

  ivxsay -l lang/v2.2/swrules.ivx "God morgon."         speak through the speaker
  ivxsay -l lang/v2.2/gerules.ivx -o hallo.wav "Hallo."  write a WAV file instead
  ivxsay -l lang/v1.12/amrules.ivx -i story.txt       read the text from a file
  echo "Hello." | ivxsay -l lang/v2.2/blrules.ivx     or from stdin (UTF-8)

-l takes the path of a language file. A relative path is tried from the current
directory, then from ivxsay's own folder, so lang/... works from anywhere.
Each folder has these 12 files:

  swrules Swedish     norules Norwegian   darules Danish   firules Finnish
  icrules Icelandic   gerules German      durules Dutch    frrules French
  itrules Italian     sprules Spanish     blrules British English
  amrules American English

The engine is the v1.1 one in both cases; the v2.2 files run on it unchanged.
Run ivxsay with no arguments for all options: voice (-v 0..9), rate, pitch,
sample rate, speed (-F), text encoding, dictionary files (-d) and an event
trace (-E).

Linux
-----

ivxsay needs the 32-bit C library and ALSA. On Debian/Ubuntu:

  sudo dpkg --add-architecture i386
  sudo apt install libc6:i386 libasound2:i386

ivxsay finds libinfovox230.so in its own directory. Keep libinfovox230.so.0 (a
symlink) next to it.

Language data
-------------

The lang/*/*.ivx files are the original product's language data, copyright Telia
Promotor AB.
