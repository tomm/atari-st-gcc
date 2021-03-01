#ifndef TOS_H
#define TOS_H

/* XBIOS */
static inline void Supexec (int (*func)())
{
	asm volatile (R"(
	move.l	%0,-(%%sp)
	move.w	#$26,-(%%sp)
	trap	#14
	addq.l	#6,%%sp
    )"
    ::"a"(func)
	);
}

/* GEMDOS */
static inline void Cconws (const char *str)
{
	asm volatile (R"(
	move.l	%0,-(%%sp)
	move.w	#9,-(%%sp)
	trap	#1
	addq.l	#6,%%sp
    )"
    ::"a"(str)
	);
}

static inline void Cconin ()
{
	asm volatile (R"(
	move.w	#1,-(%%sp)
	trap	#1
	addq.l	#2,%%sp
	)"::);
}

static inline void *Physbase ()
{
    void *p;
    asm volatile (R"(
    	move.w	#2,-(%%sp)
    	trap	#14
    	addq.l	#2,%%sp
    	move.l	%%d0,%0
    )"
    :"=a"(p));
    return p;
}

static inline void Term ()
{
	asm volatile (R"(
	clr.w	-(%%sp)
	trap	#1
    )":);
}

#endif /* TOS_H */
