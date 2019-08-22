/*--------------------------------------------------------------.
| qxltool.h : Access QXL.WIN files from other operating systems |
|                                                               |
| (c) Jonathan Hudson, April 1998                               |
| No Warranty                                                   |
|                                                               |
| Bits added, corrections made, variables renamed "better"      |
| (c) Norman Dunbar, 2019                                       |
| Warranty? What warranty? :o)                                  |
`--------------------------------------------------------------*/

#ifndef _QXLTOOL_H
#define _QXLTOOL_H

#include <sys/time.h>
#include <unistd.h>

#include "version.h"

#define UNIX2QL    283996800                    /**< Converts Unix epoch to QL epoch in dates. */
#define QL2UNIX   -(UNIX2QL)                    /**< Converts QL epoch to Unix epoch in dates. */

#define VERSTR   VERSION ", " __DATE__          /**< QXLTool version information */

#if defined __GNUC__
# define PACKED  __attribute__ ((packed))
#else
# define PACKED
#endif

#if defined __unix__ || defined __linux__
# define DEFQXL "/dos/QXL.WIN"
# define SHELL "SHELL"
# define TARGET "Linux"
#elif defined __WINNT || defined __NT__ || \
      defined __WIN32__ || defined __WIN64__ || \
      defined __MINGW32__ || defined __MINGW64__ || defined __GO32
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

#ifndef QDOS
# include <stdint.h>
#endif

#define QLPATH_MAX 36

#ifndef QDOS
  /* QDOS doesn't do pragmas. Other systems need byte alignment. */
# pragma pack(push, 1)
#endif

/** QXL File Header.
 * This structure describes and defines the header part of a QXL Hard drive file. It
 * should always be byte aligned and is 64 + 2 bytes in size. Any other value for the
 * header size is incorrect. The header is actually only 64 bytes long, but the code is
 * appending on the first 16 bit word in the map, for some, as yet, unknown reason. :)
 */
typedef struct
{
    char id[4];                     /**< "QLWA"  */
    uint16_t nameSize;              /**< Size of 'disc' name */
    u_char  name[20];               /**< Disc name, space padded */
    uint16_t spare;                 /**< Unused */
    uint16_t formatRandom;          /**< Format random number */
    uint16_t accessCount;           /**< Update counter */
    uint16_t interleave;            /**< Interleave factor qxl = 0 */
    uint16_t sectorsPerGroup;       /**< Sectors per group */
    uint16_t sectorsPerTrack;       /**< Sectors per track qxl = 0 */
    uint16_t tracksPerCylinder;     /**< Tracks per cylinder qxl =  0 */
    uint16_t cylindersPerDrive;     /**< Cylinders per drive qxl = 0 */
    uint16_t numberOfGroups;        /**< Total number of groups */
    uint16_t freeGroups;            /**< Number of free groups */
    uint16_t sectorsPerMap;         /**< Sectors per map */
    uint16_t numberOfMaps;          /**< Number of maps qxl = 1 */
    uint16_t firstFreeGroup;        /**< First free group */
    uint16_t rootDirectoryId;       /**< Root director number */
    uint32_t  rootDirectorySize;    /**< Root directory length */
    uint32_t  firstSectorPart;      /**< First sector in this partition qxl = 0 */
    uint16_t parkingCylinder;       /**< Park cylinder qxl = 0 */
    uint16_t map[1];                /**< The map starts here ... */
} HEADER;

/** QXL Directory Entry.
 * This structure defines the 64 byte (byte aligned) layout of a QXL directory entry. This is
 * a copy of the file header which all files have at the start, although it is never used by the
 * QDOS/SMSQ system. An empty file, in the directory, is always 64 bytes in size. A zero length
 * file is usually a deleted file, especially if the name length is also zero.
 */
typedef struct
{
    uint32_t length;                /**< File length. Zero is possibly deleted. 64 is an empty file. */
    uint16_t type;                  /**< File type: 0=Text, SuperBASIC source, etc, 1=EXECutable, 2=Object (linker input) file, 255=Directory. */
    uint32_t data;                  /**< EXECutable files' require a dataspace for the stack etc. */
    uint32_t xtra;                  /**< Unused. */
    uint16_t nlen;                  /**< Size of filename. Zero is a deleted file if length is also zero. */
    uint8_t name[QLPATH_MAX];       /**< Filename. The full path is here. 36 chracters maximum. */
    uint32_t updatedate;            /**< File update date. In the QL epoch (Seconds since 01/01/1970) */
    uint16_t version;               /**< File version number. */
    uint16_t map;                   /**< File id number. Index into the map area. */
    uint32_t backupdate;            /**< File backup date and time. Used by WinBack etc. */
} QLDIR;

/** XTCC Data block.
 * This is an 8 byte block, added to the end of a file created on a non-QDOS/SMSQ system, which records the
 * dataspace requirements for executable files. This, with the correct software on QDOS/SMSQ, allows for the
 * transfer of files between operating systems, without loss of the header data.
 */
typedef struct 
{
    union 
    {
        char xtcc[4];               /**< Marker flag. 'XTcc'. */
        long x;                     /**< TODO: Purpose to be confirmed. */
    } x;
    uint32_t dlen;                  /**< Dataspace required when running on QDOS/SMSQ. */
} XTCC;

#ifndef QDOS
# pragma pack(pop)
#endif

/** QXL Structure.
 * This structure is used internally by QXLTool, to pass around details of the QXL file being processed.
 */
typedef struct
{
    int fd;                         /**< File descriptor to access the underlying QXL file. */
    HEADER h;                       /**< QXL file's header. */
    QLDIR curd;                     /**< Directory entry for the current directory. */
    QLDIR root;                     /**< Directory entry for the root directory. */
    QLDIR tmpd;                     /**< TODO: To be confirmed. */
    QLDIR lastd;                    /**< TODO: To be confirmed. */
    FILE *fp;                       /**< FILE pointer to write/copy files to, etc. */
    short close;                    /**< TODO: To be confirmed. */
    char fn[PATH_MAX];              /**< Underlying QXL filename on the OS. */
    int mode;                       /**< TODO: To be confirmed. */
    short fmode;                    /**< Format flag. If 1, then the file can be formatted. */
} QXL;

#ifndef O_BINARY
# define O_BINARY 0                 /**< TODO: Purpose to be confirmed!  */
#endif

#ifndef offsetof
# define offsetof(sname,fname) ((long)&((sname *)0)->fname)
#endif

/** Internal callback function.
 * This defines a callback function used internally to facilitate processdir() and
 * file_file().
 * @param QXL pointer
 * @param QLDIR pointer
 * @param void pointer
 * @param void pointer
 * @param u_short
 * @return int
 */
typedef int (*PCALLBACK)(QXL *, QLDIR *, void *, void *, u_short);

/** Internal action function.
 * This defines a callback function used internally to execute user commands.
 * @param QXL pointer
 * @param short
 * @param **char - pointer to an array of strings - the parameters entered by the user.
 * @return int
 */
typedef int (*ACTFUNC)(QXL *, short , char **);

#define SECTORSIZE 512              /**< Defines the number of bytes in a single sector. Always 512, currently.  */

/** Job Table Entry.
 * This structure defines the layout of a single entry in the QXLtool job table. This is used to determine
 * if the commands typed by the user, at the prompt, are valid, and if so, to call the appropriate function
 * to facilitate the command. Parameters are passed in the usual 'argv' manner, much loved in C programs.
 */
typedef struct
{
    char *name;                     /**< Command name - as typed by the user at the prompt.  */
    ACTFUNC func;                   /**< The actual function called. */
    char *helpText;                 /**< Brief help text describing the purpose of this command.  */
    short flag;                     /**< TODO: Purpose to be confirmed!  */
} JTBL;

#define DO_RECURSE (1 << 0)         /**< Indicates that we want to recurse into the current directory. */
#define DO_BEST    (1 << 1)         /**< TODO: Purpose to be confirmed!  */
#define DO_WILD    (1 << 2)         /**< TODO: Purpose to be confirmed!  */

#define QX_TEXT    (1 << 0)         /**< TODO: Purpose to be confirmed!  */
#define QX_OPEN    (1 << 1)         /**< TODO: Purpose to be confirmed!  */
#define QX_FAIL_SILENTLY (1 << 14)  /**< TODO: Purpose to be confirmed!  */
#define QX_ARGV0_IS_FP (1 << 15)    /**< TODO: Purpose to be confirmed!  */

/* Interesting. I cannot compile with the option -O0 to turn off
 * optimisations to enable me to debug the code, unless I have these
 * two lines present. */
u_short swapword(u_short val);
u_long swaplong(u_long val);


#endif /* _QXLTOOL_H  */
