/*--------------------------------------------------------------.
| qxltool.h : Access QXL.WIN files from other operating systems |
|                                                               |
| (c) Jonathan Hudson, April 1998                               |
| No Warranty                                                   |
`--------------------------------------------------------------*/

#ifndef _QXLTOOL_H
#define _QXLTOOL_H

#include <sys/time.h>
#include <unistd.h>

#ifndef QDOS
    #include <stdint.h>
#endif

#include "version.h"

#define UNIX2QL    283996800
#define QL2UNIX   -(UNIX2QL)

#define VERSTR   VERSION ", " __DATE__

#if defined __GNUC__
#define PACKED  __attribute__ ((packed))
#else
#define PACKED
#endif

#ifdef __unix__
# define DEFQXL "/dos/QXL.WIN"
# define SHELL "SHELL"
# define TARGET "Linux"
#elif defined (__WINNT)
# define DEFQXL "C:\\QXL.WIN"
# define SHELL "COMSPEC" 
# define TARGET "WIN32"
#else
# define DEFQXL "win2_tmp_qxl.win"
# define TARGET "QDOS"
# define SHELL  "SHELL"
#endif

#ifdef NEED_U_CHAR
 typedef unsigned char u_char;
#endif

#ifdef NEED_U_SHORT
 typedef unsigned short u_short;
#endif
#ifdef NEED_U_LONG
 typedef unsigned long u_long;
#endif

#ifdef HAVE_READLINE_READLINE_H
# include <readline/readline.h>
#endif

#ifdef HAVE_READLINE_HISTORY_H
# include <readline/history.h>
#else
// Oh dear cygwin B20 defines readline.h but not history.h
extern void add_history(char *);
#endif

#define QLPATH_MAX 36

#ifndef QDOS
    /* QDOS doesn't do pragmas. Other systems need byte alignment. */
    #pragma pack(push, 1)
#endif

typedef struct
{
    char id[4];                 /* "QLWA"  */
    uint16_t nameSize;           /* Size of 'disc' name */
    u_char  name[20];           /* Disc name, space padded */
    uint16_t spare;              /* Unused */
    uint16_t formatRandom;       /* Format random number */
    uint16_t accessCount;        /* Update counter */
    uint16_t interleave;         /* Interleave factor qxl = 0 */
    uint16_t sectorsPerGroup;    /* Sectors per group */
    uint16_t sectorsPerTrack;    /* Sectors per track qxl = 0 */
    uint16_t tracksPerCylinder;  /* Tracks per cylinder qxl =  0 */
    uint16_t cylindersPerDrive;  /* Cylinders per drive qxl = 0 */
    uint16_t numberOfGroups;     /* Total number of groups */
    uint16_t freeGroups;         /* Number of free groups */
    uint16_t sectorsPerMap;      /* Sectors per map */
    uint16_t numberOfMaps;       /* Number of maps qxl = 1 */
    uint16_t firstFreeGroup;     /* First free group */
    uint16_t rootDirectoryId;    /* Root director number */
    uint32_t  rootDirectorySize;  /* Root directory length */
    uint32_t  firstSectorPart;    /* First sector in this partition qxl = 0 */
    uint16_t parkingCylinder;    /* Park cylinder qxl = 0 */
    uint16_t map[1];             /* The map starts here ... */
} HEADER;

typedef struct
{
    uint32_t length;
    uint16_t type;
    uint32_t data;
    uint32_t  dummy1;
    uint16_t nlen;
    uint8_t name[QLPATH_MAX];
    time_t date;
    uint16_t dummy2;
    uint16_t map;
    uint32_t dummy3;
} QLDIR;

#ifndef QDOS
    #pragma pack(pop)
#endif

typedef struct 
{
    union 
    {
        char xtcc[4];
        long x;
    } x;
    uint32_t dlen;
} XTCC;

typedef struct
{
    int fd;
    HEADER h;
    QLDIR curd;
    QLDIR root;
    QLDIR tmpd;    
    QLDIR lastd;
    FILE *fp;
    short close;
    char fn[PATH_MAX];
    int mode;
    short fmode;
} QXL;

#ifndef O_BINARY
# define O_BINARY 0
#endif

#ifndef offsetof
# define offsetof(sname,fname) ((long)&((sname *)0)->fname)
#endif

typedef int (*PCALLBACK)(QXL *, QLDIR *, void *, void *, u_short);
typedef int (*ACTFUNC)(QXL *, short , char **);

typedef struct
{
    char *name;
    ACTFUNC func;
    char *helpText;
    short flag;
} JTBL;

#define DO_RECURSE (1 << 0)
#define DO_BEST    (1 << 1)
#define DO_WILD    (1 << 2)

#define QX_TEXT    (1 << 0)
#define QX_OPEN    (1 << 1)
#define QX_FAIL_SILENTLY (1 << 14)
#define QX_ARGV0_IS_FP (1 << 15)
#endif
