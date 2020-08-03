Installation
============

`$ make`

`$ sudo make install`

Usage
=====

To compile your C to an Atari ST .prg (arch=m68000) binary, do:

`atari-st-gcc my_main.c`

Note that the assembler can't link multiple object files, so you need to code
in the slightly odd way where you just #include .c files from your my_main.c
entrypoint file.

There are some helpful utilities:

```
#include "atari-st/softmath.h"
#include "atari-st/gemdos.h"
```

Softmath allows 32-bit divide/multiply to work on m68k. Gemdos.h wraps some
basic gemdos calls.

See [hello_world.c](hello_world.c) for a working example of an Atari ST C
program.
```
