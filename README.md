# infovox230

A reverse-engineered, standalone C port of the **Infovox 230** text-to-speech engine
(Telia Promotor AB, 1996–1998): the KTH RULSYS rule interpreter and OVE III-style
formant synthesizer, extracted from the SAPI 4 engine `bin/Ivx230nt.dll`.

The port is bit-exact against the original DLL, from text to audio samples, in all
12 languages. It needs neither SAPI nor the DLL at runtime.

## Build

Requires 32-bit gcc (MinGW on Windows; `gcc-multilib` and `libasound2-dev:i386` on Linux).

```bash
make          # build/infovox230.dll + build/ivxsay.exe  (build/linux/... on Linux)
make test     # differential tests against the DLL (Windows), api_test, ivxsay WAV check
make dist     # release archive in dist/
```

## Usage

```bash
ivxsay -l sw "God morgon."              # speak through the speaker
ivxsay -l ge -o hallo.wav "Guten Tag."  # write a WAV file
echo "Hello." | ivxsay -l bl            # read from stdin
```

Languages: `sw` `no` `da` `fi` `ic` `ge` `du` `fr` `it` `sp` `bl` (British English)
`am` (American English). Run `ivxsay` with no arguments for all options.

The library API (`include/infovox230.h`) is a pull-based stream: create an engine,
`ivx_feed` UTF-8 text, `ivx_read` samples and events (marks, phonemes, done), with
user dictionaries via `ivx_dict_add`.

## Layout

| Path | Contents |
|---|---|
| `src/` | The RULSYS port, one file per original source file |
| `lib/`, `include/` | The `infovox230` library and its public header |
| `cli/` | `ivxsay` |
| `tests/` | Differential tests against the real DLL, plus the library's own tests |
| `tools/` | Disassembly, `.ivx` decoding and Ghidra helpers |
| `bin/` | The original DLL and the `.ivx` language files |
| `docs/` | Research papers and design specs |

See [`REFERENCE.md`](REFERENCE.md) for the technical findings (synth, `.ivx` format,
rule bytecode) and [`AGENTS.md`](AGENTS.md) for the working conventions.

## License note

The files in `bin/` (DLL and `.ivx` language data) are the original product's,
copyright Telia Promotor AB.
