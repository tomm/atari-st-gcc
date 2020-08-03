#include "atari-st/softmath.h"
#include "atari-st/gemdos.h"
#include "atari-st/nonstdlib.h"

int main()
{
    // these are all gemdos calls
	Cconws("Hello world");  // print a message
	Cconin();               // wait for a keypress
	Term();                 // exit

	return 0;
}
