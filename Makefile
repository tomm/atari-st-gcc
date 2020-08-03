# On debian you need to install gcc-8-m68k-linux-gnu for cross-compilation
PREFIX=/usr/local
CC = gcc
LIBS = 
CFLAGS = -g -O2 -Wall

# build GNU m68k asm to atari ST .prg executable assembler
default: gas68k.o dict.o
	$(CC) $(CFLAGS) gas68k.o dict.o -o atari-st-gasm $(LIBS)

clean:
	-rm *.o
	-rm atari-st-gasm

install: default
	mkdir -p $(PREFIX)/include/atari-st
	cp -rf atari_includes/*.h $(PREFIX)/include/atari-st
	cp atari-st-gasm atari-st-gcc $(PREFIX)/bin
