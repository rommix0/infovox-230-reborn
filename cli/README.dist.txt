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
  lang/            the 12 language files (*rules.ivx)
  sdk/             infovox230.h, the library API; on Windows also the MinGW import
                   library libinfovox230.dll.a and infovox230.def (for MSVC:
                   lib /def:infovox230.def /machine:x86)

The binaries are 32-bit (x86).

ivxsay
------

  ivxsay -l sw "God morgon."            speak through the speaker
  ivxsay -l ge -o hallo.wav "Guten Tag."   write a WAV file instead
  ivxsay -l am -i story.txt             read the text from a file (UTF-8)
  echo "Hello." | ivxsay -l bl          or from stdin

Language codes (lang/<code>rules.ivx):

  sw Swedish    no Norwegian   da Danish    fi Finnish   ic Icelandic   ge German
  du Dutch      fr French      it Italian   sp Spanish   bl British English
  am American English

-l also accepts the path of an .ivx file. Run ivxsay with no arguments for all
options: voice (-v 0..9), rate, pitch, sample rate, speed (-F), text encoding,
dictionary files (-d) and an event trace (-E).

Linux
-----

ivxsay needs the 32-bit C library and ALSA. On Debian/Ubuntu:

  sudo dpkg --add-architecture i386
  sudo apt install libc6:i386 libasound2:i386

ivxsay finds libinfovox230.so in its own directory. Keep libinfovox230.so.0 (a
symlink) next to it.

Language data
-------------

The lang/*.ivx files are the original product's language data, copyright Telia
Promotor AB.
