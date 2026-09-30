# C port of Ivx230nt.dll and the infovox230 library.
# Windows: needs the 32-bit winlibs gcc (C:\winlibs\mingw32\bin) on PATH:
#   export PATH=/c/winlibs/mingw32/bin:$PATH ; make test
# Linux (gcc-multilib): make test builds build/linux/ and runs the library tests.
# SSE2 double math matches the DLL's x87 tables (MSVC runs x87 at 53-bit precision).

CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -m32 -msse2 -mfpmath=sse -Isrc -Itools/ghidra
LDLIBS  = -lm

SRC     = src/synth.c src/synth_data.c src/mem.c \
          src/contwork.c src/cwflow.c src/cwtest.c src/cwelem.c src/cwmath.c src/cwvars.c \
          src/cwmisc.c src/tpelem.c src/opsmooth.c src/opstart.c src/opbatch.c src/op_data.c \
          src/ttsctrl.c src/tpchar.c src/tpstr.c src/tp_data.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))
HDR     = src/ivx.h src/synth.h src/cw.h src/op.h src/tp.h src/tts.h tools/ghidra/ivx_types.h
TESTS   = build/synth_cmp.exe build/cw_cmp.exe build/op_cmp.exe build/tp_cmp.exe build/tts_cmp.exe

# the infovox230 shared library (include/infovox230.h) and its clients
ifeq ($(OS),Windows_NT)
B       = build
EXE     = .exe
LIB     = $(B)/infovox230.dll
LIBLD   = -shared -static-libgcc -Wl,--out-implib,$(B)/libinfovox230.dll.a \
          -Wl,--output-def,$(B)/infovox230.def
LIBPOST =
PIC     =
RPATH   =
AUDIO   = cli/audio_win.c
AUDIOLIBS = -lwinmm
PLAT    = win32
else
B       = build/linux
EXE     =
LIB     = $(B)/libinfovox230.so
LIBLD   = -shared -Wl,-soname,libinfovox230.so.0
# ld records the -soname as the runtime lookup name (DT_NEEDED), but only $(LIB)
# itself is built, so add the .so.0 name as a symlink or the loader can't find it
# even with rpath set.
LIBPOST = ln -sf libinfovox230.so $(B)/libinfovox230.so.0
PIC     = -fPIC
RPATH   = -Wl,-rpath,'$$ORIGIN'
AUDIO   = cli/audio_alsa.c
AUDIOLIBS = -lasound
PLAT    = linux-i386
endif
LIBOBJ    = $(patsubst src/%.c,$(B)/lib/%.o,$(SRC)) $(B)/lib/infovox230.o $(B)/lib/ivxdict.o
LIBCFLAGS = $(CFLAGS) $(PIC) -fvisibility=hidden -DIVX_BUILD -Iinclude
CLIFLAGS  = -std=gnu11 -O2 -Wall -Wextra -m32 -Iinclude

ifeq ($(OS),Windows_NT)
all: $(TESTS) $(LIB) $(B)/api_test$(EXE) $(B)/ivxsay$(EXE)
else
all: $(LIB) $(B)/api_test$(EXE) $(B)/ivxsay$(EXE)
endif

$(B)/lib/%.o: src/%.c $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(B)/lib/infovox230.o: lib/infovox230.c lib/ivxdict.h include/infovox230.h $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(B)/lib/ivxdict.o: lib/ivxdict.c lib/ivxdict.h include/infovox230.h $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(LIB): $(LIBOBJ)
	$(CC) $(CFLAGS) $(LIBLD) $^ -o $@ $(LDLIBS)
	$(LIBPOST)

$(B)/api_test$(EXE): tests/api_test.c include/infovox230.h $(LIB)
	$(CC) $(CLIFLAGS) tests/api_test.c -o $@ -L$(B) -linfovox230 $(RPATH)

# text to speech through the library
$(B)/ivxsay$(EXE): cli/ivxsay.c cli/audio.h $(AUDIO) include/infovox230.h $(LIB)
	$(CC) $(CLIFLAGS) cli/ivxsay.c $(AUDIO) -o $@ -L$(B) -linfovox230 $(RPATH) $(AUDIOLIBS)

src/synth_data.c: tools/gen_synth_data.py bin/Ivx230nt.dll
	python tools/gen_synth_data.py

src/op_data.c: tools/gen_op_data.py bin/Ivx230nt.dll
	python tools/gen_op_data.py

src/tp_data.c: tools/gen_tp_data.py bin/Ivx230nt.dll
	python tools/gen_tp_data.py

build/%.o: src/%.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.exe: tests/%.c $(OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# the interpreter test counts opcode executions (CW_COVERAGE build of the port)
build/cov/%.o: src/%.c $(HDR)
	@mkdir -p build/cov
	$(CC) $(CFLAGS) -DCW_COVERAGE -DCW_STEP_LIMIT=20000000 -c $< -o $@

build/cw_cmp.exe: tests/cw_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/op_cmp.exe: tests/op_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/tp_cmp.exe: tests/tp_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/tts_cmp.exe: tests/tts_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

ifeq ($(OS),Windows_NT)
test: all
	build/synth_cmp.exe bin/Ivx230nt.dll
	build/cw_cmp.exe bin
	build/op_cmp.exe bin
	build/tp_cmp.exe bin
	build/tts_cmp.exe bin wav
	build/api_test.exe bin
	python tools/check_ivxsay.py
else
test: all
	$(B)/api_test bin
	python3 tools/check_ivxsay.py $(B)/ivxsay
endif

# release folder and archive for this platform: dist/infovox230-<version>-<plat>/
VERSION := $(shell v=$$(sed -n 's/^\#define IVX_VERSION \(0x[0-9a-fA-F]*\).*/\1/p' include/infovox230.h); \
             echo $$((v >> 16)).$$((v & 0xffff)))
DIST    = dist/infovox230-$(VERSION)-$(PLAT)

dist: $(LIB) $(B)/ivxsay$(EXE) cli/README.dist.txt include/infovox230.h
	rm -rf $(DIST) $(DIST).zip $(DIST).tar.gz
	mkdir -p $(DIST)/lang $(DIST)/sdk
	cp $(B)/ivxsay$(EXE) $(LIB) $(DIST)/
	strip $(DIST)/ivxsay$(EXE)
	strip --strip-unneeded $(DIST)/$(notdir $(LIB))
	cp bin/*.ivx $(DIST)/lang/
	cp include/infovox230.h $(DIST)/sdk/
	cp cli/README.dist.txt $(DIST)/README.txt
ifeq ($(OS),Windows_NT)
	cp $(B)/libinfovox230.dll.a $(B)/infovox230.def $(DIST)/sdk/
	cd dist && python -m zipfile -c $(notdir $(DIST)).zip $(notdir $(DIST))
else
	ln -s libinfovox230.so $(DIST)/libinfovox230.so.0
	# pack a copy with normal modes: a checkout under /mnt/c (WSL) makes every file 777
	t=$$(mktemp -d) && cp -a $(DIST) $$t/ && \
	find $$t -type d -exec chmod 755 {} + && find $$t -type f -exec chmod 644 {} + && \
	chmod 755 $$t/$(notdir $(DIST))/ivxsay $$t/$(notdir $(DIST))/libinfovox230.so && \
	tar -C $$t -czf $(DIST).tar.gz --owner=0 --group=0 --numeric-owner $(notdir $(DIST)) && \
	rm -rf $$t
endif

clean:
	rm -f build/*.o build/cov/*.o $(TESTS) $(B)/ivxsay$(EXE) $(B)/lib/*.o $(LIB) \
	      $(B)/api_test$(EXE) build/libinfovox230.dll.a build/infovox230.def \
	      build/linux/libinfovox230.so.0
	rm -rf dist

.PHONY: all test clean dist
