/* nonstdlib ;-) */
#ifndef NONSTDLIB_H
#define NONSTDLIB_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef unsigned long size_t;

void *memcpy(void *dest, const void *src, size_t n)
{
    for (size_t i=0; i<n; ++i) {
        ((u8*)dest)[i] = ((u8*)src)[i];
    }
    return dest;
}

#endif
