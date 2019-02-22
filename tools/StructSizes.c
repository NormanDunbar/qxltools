#include <stdio.h>
#include <stdlib.h>

// We need these defines here, before including qxltool.h.
#define NEED_U_CHAR
#define NEED_U_SHORT
#define NEED_U_LONG

#include <qxltool.h>


//=========================================================================
// A small utility to read the qxltool.h file and print out the sizes of 
// various structs within. I'm not sure that things are as they seem.
//
// Norman Dunbar
// 17 February 2019.
//=========================================================================

//=========================================================================
// COMPILING:
//=========================================================================
//
// gcc -o StructSizes[.exe] -I .. StructSizes.c -m32 -fpack-struct=1
//
// If you compile this in 64 bit, it's wrong. Always compile in 32 bit with
// -m32.
//
// Also, with gcc, add -fpack-struct=1 or again, it's wrong.
//=========================================================================
// On Windows with Gcc I get:
//
// 64bit (-m64):
//
// HEADER = 66 (Should be 64)
// QLDIR = 72 (Should be 64)
// Time_t = 8. (Needs to be 4)
//
// 64bit (-m64 -fpack-struct=1)
//
// HEADER = 64 (Should be 64)
// QLDIR = 68 (Should be 64)
// Time_t = 8. (Needs to be 4)
//
// 32bit (-m32):
//
// HEADER = 66 (Should be 64)
// QLDIR = 68 (Should be 64)
// Time_t = 4. (Needs to be 4)
//
// 32bit (-m32 -fpack-struct=1)
//
// HEADER = 64
// QLDIR = 64
// Time_t = 4. (Needs to be 4)
//
// So, only the last one is correct. Sigh.
//=========================================================================




int main(int argc, char *argv[]) {

    // The '-2' is for the first word of the map, included in the HEADER struct.
    printf("Sizeof(struct HEADER) = %u, (Should be 64)\n", sizeof(HEADER) -2);
    printf("Sizeof(struct QLDIR)  = %u, (Should be 64)\n", sizeof(QLDIR));
    printf("Sizeof(time_t)        = %u, (Needs to be 4)\n", sizeof(time_t)); 
    return 0;
}

