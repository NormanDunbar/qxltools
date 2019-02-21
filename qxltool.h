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

typedef struct
{
    char id[4];                 /* "QLWA"  */
    u_short nameSize;           /* Size of 'disc' name */
    u_char  name[20];           /* Disc name, space padded */
    u_short spare PACKED;       /* Unused */
    u_short rand;               /* System random number */
    u_short access;             /* Update counter */
    u_short interleave;         /* Interleave factor qxl = 0 */
    u_short sectorsPerGroup;    /* Sectors per group */
    u_short sectorsPerTrack;    /* Sectors per track qxl = 0 */
    u_short tracksPerCylinder;  /* Tracks per cylinder qxl =  0 */
    u_short cylindersPerDrive;  /* Cylinders per drive qxl = 0 */
    u_short numberOfGroups;     /* Number of groups */
    u_short freeGroups;         /* Number of free groups */
    u_short sectorsPerMap;      /* Sectors per map */
    u_short numberOfMaps;       /* Number of maps qxl = 1 */
    u_short firstFreeGroup;     /* First free group */
    u_short rootDirectoryId;    /* Root director number */
    u_long  rootDirectorySize PACKED;   /* Root directory length */
    u_long  firstSectorPart PACKED;     /* First sector in this partition qxl = 0 */
    u_short parkingCylinder;    /* Park cylinder qxl = 0 */
    u_short map[1];             /* The map starts here ... */
} HEADER;

typedef struct
{
    u_long length;
    u_short type;
    u_long data PACKED;
    u_long  dummy1 PACKED;
    u_short nlen;
    u_char name[QLPATH_MAX];
    time_t date PACKED;
    u_short dummy2;
    u_short map;
    u_long dummy3;
} QLDIR;

typedef struct 
{
    union 
    {
        char xtcc[4];
        long x;
    } x;
    u_long dlen;
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
