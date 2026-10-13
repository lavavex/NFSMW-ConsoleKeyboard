# Cross-build with MinGW-w64 (i686): make
#
# --no-insert-timestamp and --disable-auto-image-base make the build repeatable: the same
# compiler gives the same bytes in any folder. Another compiler version gives other bytes
# that do the same thing; the release's SHA256SUMS names the compiler it was built with.
CC = i686-w64-mingw32-gcc
CFLAGS = -std=gnu11 -O2 -Wall -Wextra
LDFLAGS = -shared -static-libgcc -s -Wl,--no-insert-timestamp,--disable-auto-image-base

NFSMWConsoleKeyboard.asi: NFSMWConsoleKeyboard.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# The release zip, with fixed file times and order so it repeats too.
dist: NFSMWConsoleKeyboard.asi
	rm -rf dist NFSMWConsoleKeyboard.zip && mkdir -p dist/scripts
	cp NFSMWConsoleKeyboard.asi dist/scripts/
	cp README.md dist/README.txt
	touch -t 200511150000 dist/scripts dist/scripts/NFSMWConsoleKeyboard.asi dist/README.txt
	cd dist && zip -X -r ../NFSMWConsoleKeyboard.zip README.txt scripts/NFSMWConsoleKeyboard.asi

clean:
	rm -rf NFSMWConsoleKeyboard.asi NFSMWConsoleKeyboard.zip dist

.PHONY: dist clean
