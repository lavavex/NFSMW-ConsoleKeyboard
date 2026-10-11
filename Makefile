# Cross-build with MinGW-w64 (i686): make
CC = i686-w64-mingw32-gcc
CFLAGS = -std=gnu11 -O2 -Wall -Wextra
LDFLAGS = -shared -static-libgcc -s

NFSMWConsoleKeyboard.asi: NFSMWConsoleKeyboard.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

dist: NFSMWConsoleKeyboard.asi
	rm -rf dist && mkdir -p dist/scripts
	cp NFSMWConsoleKeyboard.asi dist/scripts/
	cp README.md dist/README.txt
	cd dist && zip -r ../NFSMWConsoleKeyboard.zip scripts README.txt

clean:
	rm -rf NFSMWConsoleKeyboard.asi NFSMWConsoleKeyboard.zip dist

.PHONY: dist clean
