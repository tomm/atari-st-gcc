#ifndef __GEMDOS_H
#define __GEMDOS_H

/* XBIOS */
void Supexec (int (*func)())
{
	asm (
"	movea.l	4(%a7),%a0\n"
"	move.l	%a0,-(%sp)\n"
"	move.w	#$26,-(%sp)\n"
"	trap	#14\n"
"	addq.l	#6,%sp\n"
	);
}

/* GEMDOS */
void Cconws (const char *poop)
{
	asm (
"	movea.l	4(%a7),%a0\n"
"	move.l	%a0,-(%sp)\n"
"	move.w	#9,-(%sp)\n"
"	trap	#1\n"
"	addq.l	#6,%sp\n"
	);
}

void Cconin ()
{
	asm (
"	move.w	#1,-(%sp)\n"
"	trap	#1\n"
"	addq.l	#2,%sp\n"
	);
}

void *Physbase ();
asm (
"Physbase:\n"
"	move.w	#2,-(%sp)\n"
"	trap	#14\n"
"	addq.l	#2,%sp\n"
"	move.l	%d0,%a0\n"
"	rts\n"
);

void Term ()
{
	asm (
"	clr.w	-(%sp)\n"
"	trap	#1\n"
	);
}



#endif /* __GEMDOS_H */
