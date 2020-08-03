#include "atari-st/softmath.h"
#include "atari-st/gemdos.h"

int main()
{
    // these are all gemdos calls
	Cconws("Hello world");  // print a message
	Cconin();               // wait for a keypress
	Term();                 // exit

	return 0;
}
