#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct
{
    uint8_t id[4];                 /* "QLWA"  */
    uint16_t nameSize;           /* Size of 'disc' name */
    uint8_t  name[20];           /* Disc name, space padded */
    uint16_t spare ;       /* Unused */
    uint16_t rand;               /* System random number */
    uint16_t access;             /* Update counter */
    uint16_t interleave;         /* Interleave factor qxl = 0 */
    uint16_t sectorsPerGroup;    /* Sectors per group */
    uint16_t sectorsPerTrack;    /* Sectors per track qxl = 0 */
    uint16_t tracksPerCylinder;  /* Tracks per cylinder qxl =  0 */
    uint16_t cylindersPerDrive;  /* Cylinders per drive qxl = 0 */
    uint16_t numberOfGroups;     /* Number of groups */
    uint16_t freeGroups;         /* Number of free groups */
    uint16_t sectorsPerMap;      /* Sectors per map */
    uint16_t numberOfMaps;       /* Number of maps qxl = 1 */
    uint16_t firstFreeGroup;     /* First free group */
    uint16_t rootDirectoryId;    /* Root director number */
    uint32_t  rootDirectorySize ;   /* Root directory length */
    uint32_t  firstSectorPart ;     /* First sector in this partition qxl = 0 */
    uint16_t parkingCylinder;    /* Park cylinder qxl = 0 */
    uint16_t map[1];             /* The map starts here ... */
} HEADER;

typedef struct
{
    uint32_t length ;
    uint16_t type;
    uint32_t data ;
    uint32_t  dummy1 ;
    uint16_t nlen;
    uint8_t name[32];
    time_t date ;
    uint16_t dummy2;
    uint16_t map;
    uint32_t dummy3 ;
} QLDIR;



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
// gcc -o StructSstdInt[.exe] -I .. StructStdInt.c -m32 -fpack-struct=1
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
// QLDIR = 64 (Should be 64)
// Time_t = 8. (Needs to be 4)
//
// 32bit (-m32):
//
// HEADER = 66 (Should be 64)
// QLDIR = 64 (Should be 64)
// Time_t = 4. (Needs to be 4)
//
// 32bit (-m32 -fpack-struct=1)
//
// HEADER = 64
// QLDIR = 60 (should be 64)
// Time_t = 4. (Needs to be 4)
//
// So, NONE are correct in 64 bit mode. Hmmm.
//=========================================================================




int main(int argc, char *argv[]) {

    // The '-2' is for the first word of the map, included in the HEADER struct.
    printf("Sizeof(struct HEADER) = %u, (Should be 64)\n", sizeof(HEADER) -2);
    printf("Sizeof(struct QLDIR)  = %u, (Should be 64)\n", sizeof(QLDIR));
    printf("Sizeof(time_t)        = %u, (Needs to be 4\n", sizeof(time_t)); 
    
    
    return 0;
}

