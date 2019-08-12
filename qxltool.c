/*--------------------------------------------------------------.
| qxltool.c : Access QXL.WIN files from other operating systems |
|                                                               |
| (c) Jonathan Hudson, 1998 - 1999                              |
| No Warranty                                                   |
|                                                               |
| Bits added, corrections made, variables renamed "better"      |
| (c) Norman Dunbar, 2019                                       |
| Warranty? What warranty? :o)                                  |
`--------------------------------------------------------------*/
#define _GNU_SOURCE


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <stddef.h>
#include <sys/types.h>
#include <math.h>

#if defined(__unix__)
# include <fnmatch.h>
# define FNMATCH fnmatch
# define FNEQUAL 0
#elif defined(__WINNT) || defined(__GO32)
# ifdef HAVE_GETOPT_H
#  include <getopt.h>
# else
#  include <getopt.c>
# endif
# ifndef HAVE_FNMATCH
#  include <fnmatch.c>
# endif
# define FNMATCH(a,b,c) fnmatch(a,b)
# define FNEQUAL 1
#elif defined(__QDOS__)
# include <qdos.h>
/* Sad but true, this is completed fucked up (again) */
# define FNMATCH(a,b,c) fnmatch(b,a)
# define FNEQUAL 1
#else
# error "Unknown system type"
#endif

#include "qxltool.h"

#define LINSIZ (256)

static int help (QXL *, short mflag, char **av);
static QLDIR *readqldir (QXL *, QLDIR * cur);
static u_short nextcluster (QXL *, u_short, short);
static void updatecluster (QXL *, u_short, u_short);

#ifndef HAVE_STPCPY
/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
char *stpcpy (char *s1, char *s2)
{
    strcpy (s1, s2);
    return s1 + strlen (s1);
}
#endif


#ifndef WORDS_BIGENDIAN
/*--------------------------------------------------------------------
 * If we are on a little endian system, swap the bytes order in a word
 * around to big endian to suit the QL's big endian format.
 *--------------------------------------------------------------------*/
u_short inline swapword (u_short val)
{
    return (u_short) (val << 8) + (val >> 8);
}

/*--------------------------------------------------------------------
 * If we are on a little endian system, swap the bytes order in a long
 * around to big endian to suit the QL's big endian format.
 *--------------------------------------------------------------------*/
u_long inline swaplong (u_long val)
{
    return (u_long) (((u_long) swapword (val & 0xFFFF) << 16) |
                     (u_long) swapword (val >> 16));
}

# define SW(a,c) (a)->c = swapword((a)->c)
# define SL(a,c) (a)->c = swaplong((a)->c)
#else
/*--------------------------------------------------------------------
 * If we are on a big endian system, do nothing when asked to swap
 * byte order on a word value.
 *--------------------------------------------------------------------*/
u_short swapword (u_short val)
{
    return val;
}

/*--------------------------------------------------------------------
 * If we are on a big endian system, do nothing when asked to swap
 * byte order on a long value.
 *--------------------------------------------------------------------*/
u_short swaplong (u_long val)
{
    return val;
}

# define SW(a,c)
# define SL(a,c)
#endif

#ifdef __QDOS__
char _prog_name[] = "qxltool";
char _copyright[] = "(c) Jonathan R Hudson 1998";
char _version[] = VERSION;
char *_endmsg = NULL;

#include <fcntl.h>
#include <qdos.h>
#include <qptr.h>
/*--------------------------------------------------------------------
 * QL specific console setup function header.
 *--------------------------------------------------------------------*/
void myconsetup (chanid_t ch, WINDOWDEF_t * w);

/*--------------------------------------------------------------------
 * QL specific console definition.
 *--------------------------------------------------------------------*/
struct WINDOWDEF _condetails =
{6, 1, 0, 4, 512, 256, 0, 0};

/*--------------------------------------------------------------------
 * QL specific console setup function gets hooked into startup code
 * right here.
 *--------------------------------------------------------------------*/
void (*_consetup) () = myconsetup;

/*--------------------------------------------------------------------
 * This is where the QL specific console setup function does its work.
 * It appears to read the environment variable QXL_CON, if it exists.
 *--------------------------------------------------------------------*/
void myconsetup (chanid_t ch, WINDOWDEF_t * w)
{
    char *p;
    int ns, q1, q2, q3, q4;

    iop_flim (ch, -1, (WM_wsiz_t *) & w->width);
    if ((p = getenv ("QXL_CON")))
    {
        ns = sscanf (p, "%dx%da%dx%d", &q1, &q2, &q3, &q4);
        switch (ns)
        {
        case 4:
            w->y_origin = q4;
        case 3:
            w->x_origin = q3;
        case 2:
            w->height = q2;
        case 1:
            w->width = q1;
            break;
        }
    }
    consetup_qpac (ch, w);
}

/*--------------------------------------------------------------------
 * Scan a QL path name to extract the last part which makes up a QL
 * directory name.
 *--------------------------------------------------------------------*/
static char *lastdir (char *ws)
{
    char *p;
    char *q = ws;
    struct stat s;

    for (p = ws; *p; p++)
    {
        if (*p == '_')
        {
            char c;

            p++;
            c = *p;
            *p = 0;
            if (stat (ws, &s) == 0 && S_ISDIR (s.st_mode))
            {
                q = p;
            }
            *p = c;
        }
    }
    return q;
}

#endif


/*--------------------------------------------------------------------
 * Trim trailing control characters from a string
 *--------------------------------------------------------------------*/
static u_char *strim (u_char *s)
{
    u_char *p;
    short n;

    n = strlen ((char *)s);
    p = s + n;
    while (*--p <= ' ' && n--)
    {
        *p = '\0';
    }
    return s;
}


/*--------------------------------------------------------------------
 * Given a QLWA header, convert words and longs to the host computers 
 * byte order, if necessary. Does nothing at all on big endian systems.
 *--------------------------------------------------------------------*/
static void h_normalise (HEADER * h)
{
    SW (h, nameSize);
    SW (h, formatRandom);
    SW (h, accessCount);
    SW (h, sectorsPerGroup);
    SW (h, numberOfGroups);
    SW (h, freeGroups);
    SW (h, rootDirectoryId);
    SW (h, sectorsPerMap);
    SW (h, firstFreeGroup);
    SL (h, rootDirectorySize);
    SW (h, map[0]);
}

/*--------------------------------------------------------------------
 * Writes a new QLWA header to the open QXL file. This will overwrite
 * any existing header.
 *--------------------------------------------------------------------*/
static void writeheader (QXL * qxl)
{
    HEADER h;
    int i;

    /* Reset file position to the very start */
    lseek (qxl->fd, 0, SEEK_SET);
    qxl->h.accessCount++;
    h = qxl->h;
    for(i = 0; i < sizeof(h.name); i++)
    {
        if(h.name[i] == 0) h.name[i] = ' ';
    }
    h_normalise (&h);
    write (qxl->fd, &h, sizeof (HEADER));
    strim ((char *) h.name);
}

/*--------------------------------------------------------------------
 * Reads the QLWA header from an opened QXL file.
 *--------------------------------------------------------------------*/
static void readheader (QXL * qxl)
{
    /* Reset file position to the very start */
    lseek (qxl->fd, 0, SEEK_SET);

    /* Read current header */
    read (qxl->fd, &qxl->h, sizeof (HEADER));

    /* Swap byte order in words and longs in header */
    h_normalise (&qxl->h);

    /* Is it a QLWA header? */
    if (memcmp (qxl->h.id, "QLWA", 4) == 0)
    {
        /* Now create a fake directory record for the root directory */
        qxl->root.length = qxl->h.rootDirectorySize;
        qxl->root.map = qxl->h.rootDirectoryId;
        qxl->root.type = 0xff;
        *qxl->root.name = 0;
        qxl->root.nlen = 0;
        strim ((char *) qxl->h.name);
    }
    else
    /* Nope, not QLWA */
    {
        fputs ("Not a QXL file\n", stderr);
        exit (0);
    }
}

/*--------------------------------------------------------------------
 * Swaps the byte order in a directory entry on little endian systems
 * but does nothing on big endian systems.
 *--------------------------------------------------------------------*/
static void d_normalise (QLDIR * d)
{
    SL (d, length);
    SW (d, type);
    SL (d, data);
    SW (d, nlen);
    SL (d, date);
    SW (d, map);
}

/*--------------------------------------------------------------------
 * Lists a directory contents.
 *--------------------------------------------------------------------*/
static int d_list (QXL * qxl, QLDIR * d, void *a, void *pb, u_short c)
{
    struct tm *tm;
    char s[128];
    size_t n;
    char *p;
    int flen, m;

    /* Strip off the unused copy of the directory entry */
    flen = d->length - sizeof (QLDIR);

    if(flen >=0)
    {
        p = (char *) d->name;
        m = d->nlen;
        if (d->length==0 && m == 0)
        {
            m=strlen(p);
        }
        if (qxl->curd.nlen)
        {
            p += qxl->curd.nlen + 1;
            m -= (qxl->curd.nlen + 1);
        }

        n = sprintf (s, "%-36.*s %s %8d ", m, p,
                     (d->length==0 ? "DELETED" : ""), flen);

        if (d->type != 0xff)
        {
            size_t k;

            tm = localtime (&d->date);
#if defined (HAVE_STRFTIME)
            k = strftime (s + n, sizeof (s) - n,
                          "%d-%m-%y %H:%M:%S ", tm);
#else
            k = sprintf (s + n, "%02d-%02d-%02d %02d:%02d:%02d ",
                         tm->tm_mday, tm->tm_mon + 1, tm->tm_year % 100,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
#endif
            n += k;
        }
        else
        {
            strcpy (s + n, "      (Directory) ");
            n += 18;
        }


        if (d->type == 1)
        {
            sprintf (s + n, "(%ld) ", (long)d->data);
        }
        fputs (s, qxl->fp);
        fputc ('\n', qxl->fp);
    }
    return 0;
}

/* quick and dirty version for now */

#if !defined (FNM_CASEFOLD)
# define FNM_CASEFOLD 0
#endif

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int qfindwild (QXL * qxl, QLDIR * d, void *fn, void *pft, u_short flag)
{
    QLDIR *pd = NULL;
    u_short ft = *(u_short *)pft;

    if (ft == 0 || (ft == d->type))
    {
        char fnm[40];

        memcpy (fnm, (char *) d->name, d->nlen);
        *(fnm + d->nlen) = 0;

        if (FNEQUAL == FNMATCH (fn, fnm, FNM_CASEFOLD))
        {
            pd = malloc (sizeof (QLDIR));
            *pd = *d;
        }
    }
    return (int) pd;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int qfindbest (QXL * qxl, QLDIR * d, void *fn, void *pft, u_short flag)
{
    size_t wlen = strlen (fn);
    u_short ft = *(u_short *)pft;

    if ((ft == d->type))
    {
        if ((d->nlen > qxl->lastd.nlen) && (d->nlen <= wlen) &&
                memcmp ((char *) d->name, (char *)fn, d->nlen) == 0
                && *((char *)fn+d->nlen) == '_')
        {
            memcpy (&qxl->lastd, d, sizeof (QLDIR));
        }
    }
    return 0;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int qmatch (QXL * qxl, QLDIR * d, void *fn, void *pft, u_short flag)
{
    QLDIR *pd = NULL;
    size_t wlen = strlen ((char *)fn);
    u_short ft = *(u_short *)pft;

    if (ft == 0 || (ft == d->type))
    {
        if (d->nlen == wlen && memcmp ((char *) d->name, (char *)fn,
                                       wlen) == 0)
        {
            pd = malloc (sizeof (QLDIR));
            *pd = *d;
        }
    }
    return (int) pd;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int processdir (QXL * qxl, QLDIR * buf, QLDIR * cur,
                       PCALLBACK func, char *fn, void *pft, short flag)
{
    QLDIR *d, *last, *z;
    int res = 0;
    last = buf + cur->length / sizeof (QLDIR);

    for (d = buf + 1; d < last; d++)
    {
        d_normalise (d);
        d->date += QL2UNIX;
        if ((res = func (qxl, d, fn, pft, flag)) == 0)
        {
            if (d->type == 0xff && (flag & DO_RECURSE))
            {
                z = readqldir (qxl, d);
                qxl->tmpd = *d;
                res = processdir (qxl, z, d, func, fn, pft, flag);
                qxl->tmpd = *cur;
                free (z);
            }
        }
        else
            qxl->lastd = *cur;
        if (res)
            break;
    }
    return res;
}

/*--------------------------------------------------------------------
 * Allocate a new cluster (or group). Returns the cluster number. If
 * 'o' is non-zero then ??????????????????????? otherwise ???????????.
 *
 * QUERY: We always reduce the number of free clusters even when we
 * don't allocate one.
 *--------------------------------------------------------------------*/
static u_short getcluster (QXL * qxl, u_short o)
{
    u_short c, n;

    /* Decrease the number of free clusters left */
    qxl->h.freeGroups--;

    /* Grab the first free cluster number */
    c = nextcluster (qxl, qxl->h.firstFreeGroup, 0);
    n = qxl->h.firstFreeGroup;
    if (o)
        updatecluster (qxl, o, n);

    updatecluster (qxl, n, 0);
    qxl->h.firstFreeGroup = c;
    return n;
}

/*--------------------------------------------------------------------
 * Return a cluster number which is the next cluster to be added to the
 * end of the cluster passed in as parameter 'c'.
 * 'Flag' is non-zero when we need to actually allocate a new cluster 
 * and zero when we just want the number.
 *--------------------------------------------------------------------*/
static u_short nextcluster (QXL * qxl, u_short c, short flag)
{
    u_long newoff;
    u_short d;

    /* Offset into the map, 2 bytes per cluster */
    newoff = c * 2 + offsetof (HEADER, map);

    /* Seek to the map word we want. */
    lseek (qxl->fd, newoff, SEEK_SET);

    /* Read the map word in big endian format 
     * Swap the byte order if necessary */
    read (qxl->fd, &d, sizeof (short));
    d = swapword (d);

    /* If this is the last cluster (for current file) and we are allocating 
     * call getcluster to allocate the cluster if there are free clusters 
     * remaining. */
    if (d == 0 && flag)
    {
        if (qxl->h.freeGroups)
        {
            /* Actually allocate the cluster & update the map */
            d = getcluster (qxl, c);
        }
        else
        {
            /* We are stuffed! */
            fputs ("Disk is full!\n", stderr);
            exit (0);
        }
    }

    /* Return the newly allocated or calculated cluster number */
    return d;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static QLDIR *readqldir (QXL * qxl, QLDIR * cur)
{
    u_char *buf, *p;
    int dlen, rlen;
    u_short fcluster;

    dlen = cur->length;
    p = buf = malloc (dlen);

    for (fcluster = cur->map; (fcluster && dlen);)
    {
        lseek (qxl->fd, fcluster * qxl->h.sectorsPerGroup, SEEK_SET);
        if (dlen < qxl->h.sectorsPerGroup)
            rlen = dlen;
        else
            rlen = qxl->h.sectorsPerGroup;
        read (qxl->fd, p, rlen);
        p += rlen;
        dlen -= rlen;
        fcluster = nextcluster (qxl, fcluster, 0);
    }
    return (QLDIR *) buf;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void writeqldir (QXL * qxl, QLDIR * cur, QLDIR * buf)
{
    u_char *p;
    int dlen, rlen;
    u_short clu;
    u_short ifree;

    dlen = cur->length;
    p = (u_char *) buf;
    ifree = qxl->h.freeGroups;

    for (clu = cur->map; dlen;)
    {
        lseek (qxl->fd, clu * qxl->h.sectorsPerGroup, SEEK_SET);
        if (dlen < qxl->h.sectorsPerGroup)
            rlen = dlen;
        else
            rlen = qxl->h.sectorsPerGroup;
        write (qxl->fd, p, rlen);
        p += rlen;
        dlen -= rlen;
        if (dlen)
            clu = nextcluster (qxl, clu, 1);
    }
    if (ifree != qxl->h.freeGroups)
        writeheader (qxl);
}

/*--------------------------------------------------------------------
 * Opens a QXL file for processing. The default is read-only unless
 * -w was on the command line, or the RW command has been used. The
 * QLWA header is read on a successful open.
 *--------------------------------------------------------------------*/
static int openqxl (QXL * qxl, char *fn)
{
    if ((qxl->fd = open (fn, qxl->mode)) != -1)
    {
        readheader (qxl);
        memcpy (&qxl->curd, &qxl->root, sizeof (QLDIR));
        strcpy (qxl->fn, fn);
    }
    return qxl->fd;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static QLDIR *find_file (QXL * qxl, char *fn, u_short ft, QLDIR * cur,
                         short flag)
{
    int res = 0;
    QLDIR *buf;
    char wanted[40];
    int n;
    PCALLBACK cb;

    if ((n = cur->nlen))
    {
        memcpy (wanted, (char *) cur->name, n);
        *(wanted + n) = '_';
        n++;
    }
    strcpy (wanted + n, fn);

    cb = (flag & DO_BEST) ? qfindbest : (flag & DO_WILD) ? qfindwild : qmatch;
    buf = readqldir (qxl, cur);
    qxl->tmpd = *buf;
    res = processdir (qxl, buf, cur, cb, wanted, &ft, flag);
    free (buf);
    return (QLDIR *) res;
}


/*--------------------------------------------------------------------
 * QUIT/EXIT commands.
 * We are done, leave the program.
 *--------------------------------------------------------------------*/
static int quit (QXL * qxl, short mflag, char **av)
{
    exit (0);
    return 0;
}

/*--------------------------------------------------------------------
 * CD command.
 * Change QXL Directory.
 *--------------------------------------------------------------------*/
static int qcd (QXL * qxl, short mflag, char **av)
{
    QLDIR *d;
    char *p = (av) ? *av : NULL;
    int rv = 0;


    if (p == NULL || *p ==0 ||  strcmp(p,"/") == 0 || strcmp(p,"_") == 0)
    {
        memcpy (&qxl->curd, &qxl->root, sizeof (QLDIR));
    }
    else if(strcmp(p,"..") == 0)
    {
        char foo[QLPATH_MAX+1];
        memcpy(foo, (char *)qxl->curd.name, qxl->curd.nlen);
        *(foo+qxl->curd.nlen) =0;
        d = find_file (qxl, foo, 0xff, &qxl->root, DO_RECURSE);
        memcpy (&qxl->curd, &qxl->lastd, sizeof (QLDIR));
    }
    else if (strcmp(p, ".") != 0)
    {
        if (*(p + strlen (p) - 1) == '_')
        {
            *(p + strlen (p) - 1) = 0;
        }

        d = find_file (qxl, p, 0xff, &qxl->curd, DO_RECURSE);
        if (d)
        {
            qxl->curd = *d;
            free (d);
        }
        else
        {
            if((mflag & QX_FAIL_SILENTLY) == 0)
                fprintf (stderr, "Not found %s\n", p);
            rv = -1;
        }
    }
    /* else no op, stay in current */
    return rv;
}

/*--------------------------------------------------------------------
 * LSLR command.
 * List the contents of the current QXL directory plus any 
 * sub-directories.
 *--------------------------------------------------------------------*/
static int qlslr (QXL * qxl, short mflag, char **av)
{
    QLDIR *buf;
    char *cmd = *av;
    u_short ft = 0;

    if (cmd && *cmd)
    {
        fputs ("ls[lr] takes no parameters (currently), cd first\n", stderr);
    }
    else
    {
        buf = readqldir (qxl, &qxl->curd);
        qxl->tmpd = *buf;
        processdir (qxl, buf, &qxl->curd, d_list, NULL, &ft, DO_RECURSE);
        free (buf);
    }
    return 0;
}

/*--------------------------------------------------------------------
 * LS command.
 * List the contents of the current QXL directory.
 *--------------------------------------------------------------------*/
static int qls (QXL * qxl, short mflag, char **av)
{
    QLDIR *buf;
    char *cmd = *av;
    u_short ft = 0;

    if (cmd && *cmd)
    {
        fputs ("ls[lr] takes no parameters (currently), cd first\n", stderr);
    }
    else
    {
        buf = readqldir (qxl, &qxl->curd);
        qxl->tmpd = *buf;
        processdir (qxl, buf, &qxl->curd, d_list, NULL, &ft, 0);
        free (buf);
    }
    return 0;
}

/*--------------------------------------------------------------------
 * DUMP command.
 * Displays a QXL directory structure plus any warts found.
 *--------------------------------------------------------------------*/
static int qdump (QXL * qxl, short mflag, char **av)
{
    QLDIR *buf;
    char *cmd = *av;
    u_short ft = 1;

    if (cmd && *cmd)
    {
        fputs ("dump takes no parameters (currently), cd first\n", stderr);
    }
    else
    {
        buf = readqldir (qxl, &qxl->curd);
        qxl->tmpd = *buf;
        processdir (qxl, buf, &qxl->curd, d_list, NULL, &ft, 1);
        free (buf);
    }
    return 0;
}


/*--------------------------------------------------------------------
 * PWD command
 * Prints the current directory in the QXL file.
 *--------------------------------------------------------------------*/
static int qpwd (QXL * qxl, short mflag, char **av)
{
    char name[QLPATH_MAX+1];

    if(qxl->curd.nlen)
    {
        memcpy(name, (char *)qxl->curd.name, qxl->curd.nlen);
        *(name+qxl->curd.nlen) = 0;
    }
    else
    {
        strcpy(name, "/");
    }
    fprintf (qxl->fp, "%s\n", name);
    return 0;
}


/*--------------------------------------------------------------------
 * Type or copy a QXL file to the host OS.
 *--------------------------------------------------------------------*/
static void cpfile (QXL * qxl, QLDIR * d)
{
    u_char *buf;
    int dlen, rlen;
    u_short cluster;
    short off = sizeof (QLDIR);

    buf = (u_char *) alloca (qxl->h.sectorsPerGroup);
    dlen = d->length;
    cluster = d->map;

    if (qxl->fp)
    {
        for (cluster = d->map; cluster; cluster = nextcluster (qxl, cluster, 0))
        {
            lseek (qxl->fd, cluster * qxl->h.sectorsPerGroup, SEEK_SET);
            if (dlen < qxl->h.sectorsPerGroup)
                rlen = dlen;
            else
                rlen = qxl->h.sectorsPerGroup;
            read (qxl->fd, buf, rlen);

            fwrite (buf + off, 1, rlen - off, qxl->fp);

            dlen -= rlen;
            off = 0;
        }
    }

    /* Process QXL executable files with an XTCC trailer record on DOS/Unix etc. */
    if (d->type == 1 && qxl->close == 1 && d->data)
    {
#ifndef __QDOS__
        XTCC xtcc;

        fseek (qxl->fp, -8, SEEK_CUR);
        fread (&xtcc, 1, 8, qxl->fp);
        if (memcmp (xtcc.x.xtcc, "XTcc", 4))
        {
            memcpy (xtcc.x.xtcc, "XTcc", 4);
            xtcc.dlen = swaplong (d->data);
            fwrite (&xtcc, 1, 8, qxl->fp);
        }
#else
        /* Write out the file to QDOS and set the header natively. */
        {
            struct qdirect qd;
            qd.d_datalen = d->data;
            qd.d_type = 1;
            qd.d_length = d->length;
            qd.d_access = 0;
            fs_heads (fgetchid (qxl->fp), -1, &qd, 14);
        }
#endif
    }
}


/*--------------------------------------------------------------------
 * CAT, CP, TCAT, TCP commands.
 * Copy or type a QXL file. Converts LF to CR/LF.
 *--------------------------------------------------------------------*/
static int qcp (QXL * qxl, short mflag, char **av)
{
    QLDIR *d;
    char *p = *av;

    if (p)
    {
        d = find_file (qxl, p, 0, &qxl->curd, DO_RECURSE);
        if (d)
        {
            cpfile (qxl, d);
            free (d);
        }
        else
            fputs ("Not found\n", stderr);
    }
    return p == NULL;
}

/*--------------------------------------------------------------------
 * Update a cluster in the map. 
 *
 * QUERY: Use of QLDIR? Really? WTF? SEEK_SET is from start of file
 * so surely this should be:
 *      f * 2 + offsetof(HEADER, map) ????
 *--------------------------------------------------------------------*/
static void updatecluster (QXL * qxl, u_short f, u_short c)
{
    lseek (qxl->fd, f * 2 + sizeof (QLDIR), SEEK_SET);
    c = swapword (c);
    write (qxl->fd, &c, sizeof (short));
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void updatefree (QXL * qxl, QLDIR * d)
{
    u_short c, cn, f;

    for (c = d->map; c;)
    {
        cn = nextcluster (qxl, c, 0);
        f = qxl->h.firstFreeGroup;
        qxl->h.firstFreeGroup = c;
        updatecluster (qxl, c, f);
        c = cn;
        qxl->h.freeGroups++;
    }
    writeheader (qxl);
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void updatedir (QXL * qxl, QLDIR * d)
{
    QLDIR *e, *f;

    e = readqldir (qxl, &qxl->lastd);
    for (f = e + 1; f < (e + qxl->lastd.length / sizeof (QLDIR)); f++)
    {
        if (d->map == swapword (f->map))
        {
            memset (f, 0, sizeof (QLDIR));
            break;
        }
    }
    writeqldir (qxl, &qxl->lastd, e);
    free (e);
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void rmfile (QXL * qxl, QLDIR * d)
{
    char fn[40];
    memcpy (fn, d->name, d->nlen);
    *(fn + d->nlen) = 0;
    updatefree (qxl, d);
    updatedir (qxl, d);
}

/*--------------------------------------------------------------------
 * RM command
 * Delete a file.
 *--------------------------------------------------------------------*/
static int qrm (QXL * qxl, short flag, char **av)
{
    QLDIR *d;
    char *p = *av;

    if (p)
    {
        if ((qxl->mode & O_RDWR) == O_RDWR)
        {
            d = find_file (qxl, p, 0, &qxl->curd, DO_RECURSE);
            if (d)
            {
                if (d->type != 0xff)
                {
                    rmfile (qxl, d);
                    free (d);
                }
                else
                    fputs ("Use rmdir to delete empty directories\n", stderr);
            }
            else
                fputs ("Not found\n", stderr);

        }
        else
            fputs ("readonly file system\n", stderr);
    }

    return p == NULL;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int dircount (QXL * qxl, QLDIR * d)
{
    QLDIR *e, *f;
    int n = 0;

    e = readqldir (qxl, d);
    for (f = e + 1; f < (e + d->length / sizeof (QLDIR)); f++)
    {
        if (f->nlen && swaplong (f->length) >= sizeof (QLDIR))
        {
            n++;
        }
    }
    free (e);
    return n;
}


/*--------------------------------------------------------------------
 * RMDIR Command.
 * Delete a directory - which must be empty.
 *--------------------------------------------------------------------*/
static int qrmdir (QXL * qxl, short mflag, char **av)
{
    QLDIR *d;
    char *p = *av;

    if (p)
    {
        d = find_file (qxl, p, 0xff, &qxl->curd, DO_RECURSE);
        if (d)
        {
            int dn;

            if ((dn = dircount (qxl, d)) == 0)
            {
                rmfile (qxl, d);
                free (d);
            }
            else
            {
                fprintf (stderr,
                         "Directory must be empty to delete (%d)\n", dn);
            }
        }
        else
            fputs ("Not found\n", stderr);
    }
    return p == NULL;
}

/*--------------------------------------------------------------------
 * Called from main() to get args to pass to the appropriate function
 * which is about to be called to operate on the QXL file. For example
 * 'mkdir fred' returns 'fred'. The return is an array of char* and
 * the memory allocated will be reclaimed in closeout() below.
 *--------------------------------------------------------------------*/
static char **openout (QXL * qxl, char *s, short flag)
{

    char **av, **pav;
    char *r0, *r1, *r2, *p;
    int l, n = 0, nl;

    l = strlen (s);
    pav = av = (char **) malloc (sizeof (char *) * 256);

    if ((r1 = r0 = strpbrk (s, ">|")) != NULL)
    {
        r1++;
        if (*r1 == '>')
            r1++;
        while (*r1 <= ' ')
            r1++;
        r2 = r1;
        while (*r2 > ' ')
            r2++;
        n = r2 - r1;
        p = alloca (n + 1);
        memcpy (p, r1, n);
        *(p + n) = 0;

        if (*r0 == '|')
        {
            qxl->fp = popen (p, "wb");
            qxl->close = 2;
        }
        else if (*r0 == '>')
        {
            char *mode;
            if (*(r0 + 1) == '>')
            {
                mode = (flag & QX_TEXT) ? "a" : "ab";
            }
            else
                mode = (flag & QX_TEXT) ? "w+" : "w+b";
            qxl->fp = fopen (p, mode);
            qxl->close = 1;
        }
        if ((nl = l - (r2 - s)) > 0)
        {
            memmove (r0, r2, nl);
        }
        *(r0 + nl) = 0;
    }

    if (qxl->fp == NULL)
    {
        qxl->fp = stdout;
        qxl->close = 0;
    }

    for (n = 0, r0 = s; (*av = strtok (r0, "\t ")); r0 = 0, n++, av++)
        ;
    pav = (char **) realloc (pav, sizeof (char *) * (n + 1));
    *(pav + n) = NULL;

    return pav;
}

/*--------------------------------------------------------------------
 * Reclaims the memory allocated by openout() above.
 *--------------------------------------------------------------------*/
static void closeout (QXL * qxl, char **pav)
{
    if (pav)
        free (pav);

    if (qxl->close == 1)
    {
        fclose (qxl->fp);
    }
    else if (qxl->close == 2)
    {
        pclose (qxl->fp);
    }
    qxl->fp = NULL;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static double unify (QXL * qxl, u_short x, char *f)
{
    double ts;

    ts = x * qxl->h.sectorsPerGroup;
    if (ts > 1024 * 1024)
    {
        ts /= 1024 * 1024;
        *f = 'M';
    }
    else if (ts > 1024)
    {
        ts /= 1024;
        *f = 'K';
    }
    else
        *f = ' ';
    return ts;
}


/*--------------------------------------------------------------------
 * INFO Command.
 * Display QXL file information.
 *--------------------------------------------------------------------*/
static int qinfo (QXL * qxl, short mflag, char **p)
{
    double ts, fs;      /* Bytes to Kb or Mb as appropriate. */
    char tsb, fsb;      /* K or M suffix for total and free size. */
    struct stat st;

    fstat (qxl->fd, &st);

    ts = unify (qxl, qxl->h.numberOfGroups, &tsb);  /* Total size in Mb or Kb */
    fs = unify (qxl, qxl->h.freeGroups, &fsb);      /* Free size in Mb or Kb */

    /* Display the file name and whether it is read-only or not. */
    fprintf (qxl->fp, "Info for QXL file %s %s\n",
             qxl->fn, (qxl->mode == (O_RDONLY | O_BINARY)) ? "(RO)" : "(RW)");

    /* Display the QXL disc label. */
    fprintf (qxl->fp, "Label %-.*s", qxl->h.nameSize, qxl->h.name);

    /* If there's a mismatch in the name size, display the full name. */
    if (strlen ((char *) qxl->h.name) != qxl->h.nameSize)
    {
        fprintf (qxl->fp, " [%s]", qxl->h.name);
    }

    /* Group/cluster size in bytes. */
    fprintf (qxl->fp, "\nGroup size %d bytes\n", qxl->h.sectorsPerGroup);

    /* Total number of groups on QXL file, plus file size in KB or MB. */
    fprintf (qxl->fp, "Total number of groups %d (%.2f %cb, on disk %d b)\n",
             qxl->h.numberOfGroups, ts, tsb, (int) st.st_size);

    /* Of which, how many are free? Plus free size in KB or MB. */
    fprintf (qxl->fp, "Free  groups %d (%.2f %cb)\n",
             qxl->h.freeGroups, fs, fsb);

    /* Size of the map, the format random and access count. */
    fprintf (qxl->fp, "Groups per map %d, (map len %d*512), formatRandom %d, accessCount %d\n",
             qxl->h.sectorsPerMap, qxl->h.sectorsPerMap, qxl->h.formatRandom, qxl->h.accessCount);

    return 0;
}


/*--------------------------------------------------------------------
 * HEADER Command.
 * Display QXL file header. (ND 2019)
 *--------------------------------------------------------------------*/
static int qheader (QXL * qxl, short mflag, char **p)
{
    fprintf(qxl->fp, "QLWA identifier................................. '%4.4s'\n", qxl->h.id);
    fprintf(qxl->fp, "Label size...................................... %d\n", qxl->h.nameSize);
    fprintf(qxl->fp, "Label........................................... '%*.*s'\n", qxl->h.nameSize, qxl->h.nameSize, qxl->h.name);
    fprintf(qxl->fp, "Spare - expecting 0............................. %d\n", qxl->h.spare);
    fprintf(qxl->fp, "Format random number............................ %d\n", qxl->h.formatRandom);
    fprintf(qxl->fp, "Access counter.................................. %d\n", qxl->h.accessCount);
    fprintf(qxl->fp, "Interleave factor - expecting 0................. %d\n", qxl->h.interleave);
    fprintf(qxl->fp, "Sectors per group............................... %d\n", qxl->h.sectorsPerGroup);
    fprintf(qxl->fp, "Sectors per track - expecting 0................. %d\n", qxl->h.sectorsPerTrack);
    fprintf(qxl->fp, "Tracks per cylinder - expecting 0............... %d\n", qxl->h.tracksPerCylinder);
    fprintf(qxl->fp, "Cylinders per drive - expecting 0............... %d\n", qxl->h.cylindersPerDrive);
    fprintf(qxl->fp, "Total number of groups.......................... %d\n", qxl->h.numberOfGroups);
    fprintf(qxl->fp, "Number of free groups........................... %d\n", qxl->h.freeGroups);
    fprintf(qxl->fp, "Sectors per map................................. %d\n", qxl->h.sectorsPerMap);
    fprintf(qxl->fp, "Number of maps - expecting 1.................... %d\n", qxl->h.numberOfMaps);
    fprintf(qxl->fp, "First free group................................ %d\n", qxl->h.firstFreeGroup);
    fprintf(qxl->fp, "Root directory number........................... %d\n", qxl->h.rootDirectoryId);
    fprintf(qxl->fp, "Root directory length........................... %ld\n", (long)qxl->h.rootDirectorySize);
    fprintf(qxl->fp, "First sector in this partition - expecting 0.... %ld\n", (long)qxl->h.firstSectorPart);
    fprintf(qxl->fp, "Parking cylinder - expecting 0.................. %d\n", qxl->h.parkingCylinder);

    return 0;
}


/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void makenewfile (QXL * qxl, char *fn1, QLDIR * ld, FILE * fp, time_t ftim)
{

    QLDIR *e, *f;
    short got = 0;
    u_long ddata;
    u_char *buf;
    int n, m, nt;
    u_short c;

    e = readqldir (qxl, ld);

    for (f = e + 1; f < e + ld->length / sizeof (QLDIR); f++)
    {
        if (swaplong (f->length) < sizeof (QLDIR) && swapword (f->type)
                != 0xff)
        {
            got = 1;
            break;
        }
    }

    if (got == 0)
    {
        e = realloc (e, ld->length + sizeof (QLDIR));
        f = e + ld->length / sizeof (QLDIR);
        ld->length += sizeof (QLDIR);
    }

    buf = (u_char *) alloca (qxl->h.sectorsPerGroup);

    memset (buf, 0, qxl->h.sectorsPerGroup);
    memset (f, 0, sizeof (QLDIR));

    nt = 0;
    ddata = 0;

    if (fp)
    {
        int ilen, off, nr;
#ifndef __QDOS__
        XTCC xtcc;
        fseek (fp, -8, SEEK_END);
        fread (&xtcc, 1, 8, fp);
        if (memcmp (xtcc.x.xtcc, "XTcc", 4) == 0)
        {
            ddata = swaplong (xtcc.dlen);
        }
        rewind (fp);
#else
        {
            struct qdirect qd;
            if (fs_headr (fgetchid (fp), -1, &qd, sizeof (qd)) >= 0)
            {
                ddata = qd.d_datalen;
            }
        }
#endif
        for (nr = -1, off = sizeof (QLDIR), c = 0; nr != 0;)
        {
            ilen = qxl->h.sectorsPerGroup - off;
            if ((nr = fread (buf + off, 1, ilen, fp)) > 0)
            {
                c = getcluster (qxl, c);
                if (off)
                {
                    f->map = c;
                }
                lseek (qxl->fd, c * qxl->h.sectorsPerGroup, SEEK_SET);
                write (qxl->fd, buf, nr + off);
                off = 0;
                nt += nr;
            }
        }
        if (ddata)
        {
            f->type = 1;
        }
    }
    else
    {
        f->date = 0;
        f->map = c = getcluster (qxl, 0);
        lseek (qxl->fd, c * qxl->h.sectorsPerGroup, SEEK_SET);
        write (qxl->fd, buf, qxl->h.sectorsPerGroup);
        f->type = 0xff;
    }

    f->date = ftim;
    f->length = nt + sizeof (QLDIR);
    f->data = ddata;

    m = strlen (fn1);
    n = 0;

    if (ld->map == qxl->curd.map)
    {
        if ((n = ld->nlen))
        {
            memcpy ((char *) f->name, (char *) ld->name, n);
            *(f->name + n) = '_';
            n++;
        }
    }

    if (m + n > QLPATH_MAX)
    {
        m = QLPATH_MAX - n;
    }

    memcpy ((char *) f->name + n, fn1, m);
    f->nlen = m + n;

    d_normalise (f);
    writeqldir (qxl, ld, e);
    free (e);

    if (ld->map == qxl->curd.map)
    {
        qxl->curd = *ld;
    }

    /* Must also update the parent */
    if (got == 0)
    {
        char fnm[40];
        QLDIR *dd;

        memcpy (fnm, (char *) ld->name, ld->nlen);
        *(fnm + ld->nlen) = 0;
        if ((dd = find_file (qxl, fnm, 0xff, &qxl->root, DO_RECURSE)))
        {
            e = readqldir (qxl, &qxl->lastd);
            for (f = e + 1; f < (e + qxl->lastd.length / sizeof (QLDIR)); f++)
            {
                if (dd->map == swapword (f->map))
                {
                    f->length = swaplong (ld->length);
                    break;
                }
            }
            writeqldir (qxl, &qxl->lastd, e);
            free (e);
            free (dd);
        }
        else
        {
            qxl->root.length += sizeof (QLDIR);
            qxl->h.rootDirectorySize = qxl->root.length;
        }
    }
    writeheader (qxl);
}

/*--------------------------------------------------------------------
 * MKDIR Command.
 * Create a new directory.
 *--------------------------------------------------------------------*/
static int qmkdir (QXL * qxl, short mflag, char **av)
{
    char *p = *av;
    QLDIR ld, *d = NULL;
    char *star;

    if (p)
    {
        char ocd[40], ndir[40],*q;
        *ocd = 0;
        if(qxl->curd.nlen)
        {
            memcpy(ocd, (char *)qxl->curd.name, qxl->curd.nlen);
            *(ocd + qxl->curd.nlen) = 0;
            q = stpcpy(ndir, ocd);
            *q++ = '_';
            strcpy(q, p);
            p = ndir;
            qcd(qxl, 0, 0);
        }

        if ((qxl->mode & O_RDWR) == O_RDWR)
        {
            char wild[42];
            p = stpcpy (wild, p);
            if (*(p - 1) == '_')
            {
                p--;
                *p = 0;
            }

            if ((d = find_file (qxl, wild, 0, &qxl->curd, DO_RECURSE)) == NULL)
            {
                star = p;
                *p++ = '_';
                *p++ = '*';
                *p = 0;
                d = find_file (qxl, wild, 0, &qxl->curd, DO_RECURSE | DO_WILD);
                *star = 0;
            }

            if (d)
            {
                printf ("Cannot mkdir for %s as %-.*s exists\n",
                        wild, d->nlen, d->name);
                free (d);
            }
            else
            {

                qxl->lastd = qxl->root;

                find_file (qxl, wild, 0xff, &qxl->curd, DO_RECURSE | DO_BEST);
                ld = qxl->lastd;
                if (ld.nlen == 0)
                {
                    ld = qxl->curd;
                }

                makenewfile (qxl, wild, &ld, NULL, 0);
            }
            if(*ocd)
            {
                q = ocd;
                qcd(qxl, 0, &q);
            }
        }
        else
        {
            fputs ("read only filesystem\n", stderr);
        }

    }
    return p == NULL;
}

/*--------------------------------------------------------------------
 * WR Command.
 * Write a host file into the current directory in the QXL file.
 *--------------------------------------------------------------------*/
static int qwrite (QXL * qxl, short mflag, char **av)
{
    FILE *fp = NULL;
    QLDIR ld, dl, *d;
    long dlen = 0;
    char *fn0, *fn1;

    int rv = -1;
    char ntmp[40];

    if ((fn0 = *av) != NULL)
    {
        if ((qxl->mode & O_RDWR) == O_RDWR)
        {
            char ocd[40], nfil[40],*q;
            fn1 = *(av + 1);
            if (fn1 == NULL)
            {
                char *p;
#ifndef __QDOS__
                p = strrchr (fn0, '/');
                if (p == NULL)
                {
                    p = strrchr (fn0, '\\');
                }
                if (p)
                    p++;
                else
                    p = fn0;
#else
                p = lastdir (fn0);
#endif
                fn1 = strcpy(ntmp, p);
                for (p = fn1; *p; p++)
                {
                    if (*p == '.')
                        *p = '_';
                }
            }
            *ocd = 0;
            if(qxl->curd.nlen)
            {
                memcpy(ocd, (char *)qxl->curd.name, qxl->curd.nlen);
                *(ocd + qxl->curd.nlen) = 0;
                q = stpcpy(nfil, ocd);
                *q++ = '_';
                strcpy(q, fn1);
                fn1 = nfil;
                qcd(qxl, 0, 0);
            }

            if ((d = find_file (qxl, fn1, 0, &qxl->curd, DO_RECURSE)) != NULL)
            {
                if (d->type != 0xff)
                {
                    dlen = d->length;
                    dl = qxl->lastd;
                }
                else
                {
                    dlen = -1;
                    fprintf (stderr, "Can't overwrite directory \"%s\"\n",
                             fn0);
                }
            }

            if (dlen >= 0)
            {
                char *mode;
                qxl->lastd = qxl->root;
                find_file (qxl, fn1, 0xff, &qxl->curd, DO_RECURSE | DO_BEST);
                ld = qxl->lastd;
                if (ld.nlen == 0)
                {
                    ld = qxl->curd;
                }
                if(mflag & QX_ARGV0_IS_FP)
                {
                    fp = (FILE *)(*av);
                }
                else
                {
                    mode = (mflag & QX_TEXT) ? "r" : "rb";
                    fp = fopen (fn0, mode);
                }

                if (fp != NULL)
                {
                    struct stat st;
                    int need;

                    fstat (fileno (fp), &st);
                    need = ((sizeof (QLDIR) + (long)st.st_size) - dlen) /
                           qxl->h.sectorsPerGroup;

                    if (need > (qxl->h.freeGroups - 1))
                    {
                        fprintf (stderr,
                                 "%s %d No room (%d free sectors, %ld b wanted %d)\n",
                                 fn0, qxl->h.sectorsPerGroup,
                                 qxl->h.freeGroups,
                                 (long) st.st_size + sizeof (QLDIR),
                                 need
                                );
                    }
                    else
                    {
                        time_t ftm;

                        if (d)
                        {
                            qxl->lastd = dl;
                            rmfile (qxl, d);
                        }
                        ftm = (mflag & QX_ARGV0_IS_FP)
                              ? (time_t)av[2] : st.st_ctime;
                        ftm  += UNIX2QL;
                        makenewfile (qxl, fn1, &ld, fp, ftm);
                    }
                    rv = 0;
                }
                else
                    perror (fn0);
            }
            if (d)
                free (d);
            if(*ocd)
            {
                q = ocd;
                qcd(qxl, 0, &q);
            }
        }
        else
        {
            fputs ("read only filesystem\n", stderr);
        }
    }
    if(fp) fclose (fp);
    return rv;
}


/*--------------------------------------------------------------------
 * VERSION Command.
 * Displays details of the qxltool version and target OS.
 *--------------------------------------------------------------------*/
static int version (QXL * qxl, short mflag, char **av)
{
    fputs ("qxltool " VERSTR " for " TARGET "\n", stdout);
    return 0;
}


/*--------------------------------------------------------------------
 * Get the default QXL file name. Attempts to read it from:
 *
 * The passed parameter;
 * The environment variable QXL_FILE ;
 * The default:
 *      Unix:   /dos/QXL.WIN
 *      NT:     c:\QXL.WIN
 *      QDOS:   win2_tmp_qxl.win
 *--------------------------------------------------------------------*/
static char *defargs (char *av)
{
    char *fn;
    if (av)
    {
        fn = av;
    }
    else if ((fn = getenv ("QXL_FILE")) == NULL)
    {
        fn = DEFQXL;
    }
    return fn;
}

/*--------------------------------------------------------------------
 * OPEN Command.
 * Opens a QXL file.
 *--------------------------------------------------------------------*/
static int qxlopen (QXL * qxl, short mflag, char **av)
{
    char *fn = defargs (*av);
    if (qxl->fd <= 0)
    {
        openqxl (qxl, fn);
        return 0;
    }
    else
        return 1;
}

/*--------------------------------------------------------------------
 * CLOSE Command.
 * Closes a QXL file.
 *--------------------------------------------------------------------*/
static int qclose (QXL * qxl, short mflag, char **av)
{
    if (qxl->fd > 0)
    {
        close (qxl->fd);
        qxl->fd = -1;
        *qxl->fn = 0;
        return 0;
    }
    return 1;
}

/*--------------------------------------------------------------------
 * Given an array of strings, this function combines them all into a
 * single string, with a space between each. The buffer (cmd) better
 * be big enough or we are into buffer overflow territory!
 *--------------------------------------------------------------------*/
static void concatargs (char **av, char *cmd)
{
    char *c = cmd;

    if (*av)
    {
        while (*av)
        {
            /* Copy current string and return pointer to terminating '\0'. */
            c = stpcpy (c, *av);
            *c++ = ' ';
            av++;
        }

        /* When done, c is one past the last space, replace it
         * with a '\0' prior to return. */
        --c;
    }
    *c = 0;
}

/* Format the whole of the requested drive to sectors of binary zeros.
 * This will include the map, the root directory etc. The map and 
 * header will be written later.
 */
static void doFormat(QXL *qxl, unsigned totalSectors, u_short sectorSize)
{
    unsigned i;

    /* STACK OVERFLOW ALERT! */
    char *oneBlockForFormatting = alloca(sectorSize);

    memset (oneBlockForFormatting, 0, sectorSize);

/*
    k = j = qxl->h.numberOfGroups / 20;
    m = 5;
*/

    lseek (qxl->fd, 0, SEEK_SET);

    for (i = 0; i < totalSectors; i++)
    {
        if (write (qxl->fd, oneBlockForFormatting, sectorSize) != sectorSize)
        {
            perror ("Format");
            exit (0);
        }

        /*
        if (i > k)
        {
            printf (" %3d%%\b\b\b\b\b", m);
            fflush (stdout);
            k += j;
            m += 5;
        }
        */
    }
}


/*--------------------------------------------------------------------
 * Initialises a QXL file back to "brand new". Called from qinit() and 
 * qformat() according to whether this is a new file being formatted
 * or an existing one being re-formatted to the same size.
 *--------------------------------------------------------------------*/
static void init (QXL *qxl, char *lab, unsigned bytes)
{
    u_short i, j;
    size_t n;

    u_short megaBytes;
    u_short totalSectors;
    u_short sectorsPerGroup;
    u_short numberOfGroups;
    u_short sectorsPerMap;
    u_short groupsPerMap;
    u_short rootDirectoryId;
    u_short firstFreeGroup;
    u_short freeGroups;

    /* Size of a physical sector on disc. */
    const int sectorSize = 512;

    /* Work header stuff out from size requested. */
    megaBytes = bytes / 1024 / 1024;

    /* Bytes / sectorSize = total sectors. */
    totalSectors = bytes / sectorSize;

    /* Sectors per group is megabytes / 32, or 4 minimum. */
    sectorsPerGroup = ceil((double)megaBytes / 32.0);
    sectorsPerGroup = sectorsPerGroup < 4 ? 4 : sectorsPerGroup;

    /* How many groups/ */
    numberOfGroups = totalSectors / sectorsPerGroup;

    /* How many sectors in the map? */
    sectorsPerMap = ceil(((numberOfGroups * 2) + 64) / 512.0);

    /* How many groups in the map? */
    groupsPerMap = ceil((double)sectorsPerMap / (double)sectorsPerGroup);

    /* We start counting from zero, so the root directory id is
     * just the groupsPerMap value. */
    rootDirectoryId = groupsPerMap;

    /* And the first free group is the one after that. */
    firstFreeGroup = rootDirectoryId + 1;

    /* And finally, what's left is free. */
    freeGroups = numberOfGroups - groupsPerMap - 1;

    /* If we can write the file, let's do it! */
    if ((qxl->mode & O_RDWR) == O_RDWR)
    {
        /* Format the entire disc to the requested size. */
        fputs ("Formatting ....\n", stdout);
        doFormat(qxl, totalSectors, sectorSize);

        /* Now build a QXL file header. */
        memset (&qxl->h, 0, sizeof (HEADER));
        memset (qxl->h.name, ' ', 20);

        /* Header Flag. */
        memcpy (qxl->h.id, "QLWA", 4);

        /* Disc name. */
        n = strlen (lab);
        if (n > 20)
            n = 20;

        memcpy (qxl->h.name, lab, n);
        qxl->h.nameSize = n;

        /* Format random number. */
        srand (time (NULL));
        qxl->h.formatRandom = rand ();

        /* Update counter. */
        qxl->h.accessCount = 0;

        /* Sectors per group. */
        qxl->h.sectorsPerGroup  = sectorsPerGroup;

        /* Number of groups. */
        qxl->h.numberOfGroups = numberOfGroups;

        /* Free groups. */
        qxl->h.freeGroups = freeGroups;

        /* Sectors per map. */
        qxl->h.sectorsPerMap = sectorsPerMap;

        /* How many maps? Always 1. */
        qxl->h.numberOfMaps = 1;

        /* First free group number. */
        qxl->h.firstFreeGroup = firstFreeGroup;

        /* Root directory id. */
        qxl->h.rootDirectoryId = rootDirectoryId;

        /* Root directory size? Always 64. */
        qxl->h.rootDirectorySize = 64;

        /* A HEADER struct includes map[0]. That will
         * always be initialised to 1. */
        qxl->h.map[0] = 1;

        /* Write the QLWA header, plus map[0]. */
        writeheader (qxl);

    fprintf(stderr, "QLWA identifier................................. '%4.4s'\n", "QLWA");
    fprintf(stderr, "Bytes........................................... '%d'\n", bytes);
    fprintf(stderr, "Megabytes....................................... '%d'\n", megaBytes);
    fprintf(stderr, "Label size...................................... %d\n", (int)n);
    fprintf(stderr, "Label........................................... '%*.*s'\n", (int)n, (int)n,  lab);
    fprintf(stderr, "Spare - expecting 0............................. %d\n", 0);
    fprintf(stderr, "Format random number............................ %d\n", 1234);
    fprintf(stderr, "Access counter.................................. %d\n", 1);
    fprintf(stderr, "Interleave factor - expecting 0................. %d\n", 0);
    fprintf(stderr, "Sectors per group............................... %d\n", sectorsPerGroup);
    fprintf(stderr, "Sectors per track - expecting 0................. %d\n", 0);
    fprintf(stderr, "Tracks per cylinder - expecting 0............... %d\n", 0);
    fprintf(stderr, "Cylinders per drive - expecting 0............... %d\n", 0);
    fprintf(stderr, "Total number of groups.......................... %d\n", numberOfGroups);
    fprintf(stderr, "Number of free groups........................... %d\n", freeGroups);
    fprintf(stderr, "Sectors per map................................. %d\n", sectorsPerMap);
    fprintf(stderr, "Number of maps - expecting 1.................... %d\n", 1);
    fprintf(stderr, "First free group................................ %d\n", firstFreeGroup);
    fprintf(stderr, "Root directory number........................... %d\n", rootDirectoryId);
    fprintf(stderr, "Root directory length........................... %d\n", (int)64);
    fprintf(stderr, "First sector in this partition - expecting 0.... %d\n", (int)0);
    fprintf(stderr, "Parking cylinder - expecting 0.................. %d\n", 0);


        /* Position after the header area => start of map[0]! */
        lseek (qxl->fd, sizeof (HEADER) - 2, SEEK_SET);

        /* Write the entire map where each entry points at the next one. 
         * map[0] = 1, map[1] = 2 ... map[numberOfGroups - 1] = 0;
         */
        for (i = 0; i < numberOfGroups  - 1; i++) {
            j = swapword(i + 1);
            write(qxl->fd, &j, 2);
        }        

        /* Write the very last map entry as zero. The end. */
        j = 0;
        write(qxl->fd, &j, 2);

        /* The final block of the map needs a zero. */
        lseek(qxl->fd, (groupsPerMap -1) * 2 + 0x40, SEEK_SET);
        write(qxl->fd, &j, 2);

        /* The root directory entry needs a zero. */
        write(qxl->fd, &j, 2);

        fputs (" Done\n", stdout);

#ifdef mc68000
        fputs(" you must fix the geometry before SMSQ can use this filesystem\n",stdout);
#endif

#if 0
        qxl->root.length = qxl->h.rootDirectorySize;
        qxl->root.map = qxl->h.rootDirectoryId;
        qxl->root.type = 0xff;
        *qxl->root.name = 0;
        qxl->root.nlen = 0;
        qxl->lastd = qxl->root;
        qxl->curd = qxl->root;
#else
        readheader(qxl);
#endif

    }
    else
    {
        fputs ("read only filesystem\n", stderr);
    }
}


/*--------------------------------------------------------------------
 * INIT Command.
 * Reinitialises an existing QXL file back to "brand new".
 *--------------------------------------------------------------------*/
static int qinit (QXL * qxl, short mflag, char **av)
{
    /* BUFFER OVERFLOW ALERT!
     * ND 1029. */
    char lab[80];
    u_short bs = qxl->h.sectorsPerGroup;
    u_short ts = qxl->h.numberOfGroups;
    concatargs (av, lab);

    /* I changed this to simply use the size in MB. ND 2019 */
    /* init (qxl, lab, bs, ts); */
    init (qxl, lab, bs * ts * 512);
    qcd(qxl, 0, 0);
    return 0;
}

#ifdef HAVE_LIBREADLINE
/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
char * READLINE(char *a, char *b)
{
    char *c = NULL;
    if(isatty(fileno(stdin)))
    {
        if((c=readline(a)) != NULL && b)
        {
            strcpy(b, c);
        }
    }
    else if((c=fgets(b,LINSIZ,stdin)) != NULL)
    {
        char *d;
        if((d = strchr(b,'\n')))
        {
            *d = 0;
        }
    }
    return c;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
void FREECMD(char *a, char *b)
{
    if(isatty(fileno(stdin)))
    {
        if(*a)
            add_history(a);
        free(b);
    }
    if(!isatty((fileno(stdout))))
    {
        fputs("EOM\b\b\b\n", stdout);
        fflush(stdout);
    }
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
char * DUPCMD(char *b)
{
    char *a;
    if(isatty(fileno(stdin)))
    {
        a = b;
    }
    else
    {
        a = strdup(b);
    }
    return a;
}
#else
/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
char * READLINE(char *a, char *b)
{
    char *c = NULL;
    if(isatty(fileno(stdin)))
    {
        fputs(a, stdout);
        fflush(stdout);
    }
    if((c=fgets(b, LINSIZ, stdin)) != NULL)
    {
        char *d;
        if((d = strchr(b,'\n')))
        {
            *d = 0;
        }
    }
    return c;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
void FREECMD(char *a, char *b)
{
    if(!isatty(fileno(stdin)) && !isatty(fileno(stdout)))
    {
        fputs("EOM\b\b\b\n", stdout);
        fflush(stdout);
    }
}
# define DUPCMD(b) strdup(b)
#endif

#ifdef mc68000
# define READCMD(a,b,c) b = READLINE(a,c)

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int qfixgeometry (QXL * qxl, short mflag, char **av)
{
    char *spc=NULL;
    char *hpc=NULL;
    char *p = (av) ? *av : NULL;
    unsigned short sects,heads;
    char *pcmd;
    char cmd[LINSIZ];

    if (p) printf("ignoring parameter..\n");

    printf("current geometry: %d Sects/track %d Heads/cyl\n",qxl->h.dummy2[0],qxl->h.dummy2[1]);

    if ((READCMD ("Secs/track : ", pcmd, cmd)) != NULL)
    {
        spc = DUPCMD(pcmd);
    }
    else
    {
        printf("geometry not changed\n");
        return 0;
    }
    if ((READCMD ("Heads/cyl : ", pcmd, cmd)) != NULL)
    {
        hpc = DUPCMD(pcmd);
    }
    else return 0;

    sects=strtoul(spc,NULL,0);
    heads=strtoul(hpc,NULL,0);
    qxl->h.dummy2[0]=sects;
    qxl->h.dummy2[1]=heads;
    printf("setting to %d/%d\n",sects,heads);
    writeheader(qxl);
    free(hpc);
    free(spc);

    return 0;
}
#endif

/*--------------------------------------------------------------------
 * FORMAT Command.
 * Formats a brand new QXL file. Called from main() with mflag = 0.
 * The parameters in av are QXL filename, Size in mb, label. The code
 * below assumes that any of those can be NULL, but you cannot call
 * qxltools with -W and not supply the whole set of parameters. :)
 *--------------------------------------------------------------------*/
static int qformat (QXL * qxl, short mflag, char **av)
{
    char cmd[LINSIZ];
    char *pcmd;

    /* Buffer overflow alert!
     * If I am stupid enough to supply a long set of parameters, 
     * this buffer overflows into whatever comes after it in the program. 
     * NOT GOOD! */
    char lab[32];

    char *fn = NULL, *size = NULL, *label = NULL;

    if (*av)
    {
        /* Get QXL Filename to create. */
        fn = *av++;

        if (*av)
        {
            /* Get size in MB. */
            size = *av++;

            if (*av)
            {
                /* The remaining parameters are the QXL file's label. */
                concatargs (av, lab);
                label = lab;
                fputs(label, stderr);
            }
        }
    }

    /* This code is dead, it never gets called. */
    if (label == NULL || *label == 0)
    {
        if ((pcmd = READLINE ("QXL File name : ", cmd)) != NULL)
        {
            fn = DUPCMD (pcmd);
            if ((pcmd = READLINE ("Size in Mb : ", cmd)) != NULL)
            {
                size = DUPCMD (pcmd);
                if ((pcmd = READLINE ("Label  : ", cmd)) != NULL)
                {
                    label = DUPCMD (pcmd);
                }
            }
        }
    }

    if (label)
    {
        int fmode = O_CREAT | O_BINARY | O_RDWR;
        QXL *q = malloc (sizeof (QXL));

        if(qxl->fmode == 0)
        {
            fmode |= O_EXCL;
        }

        if (q &&
                (q->fd = open (fn, fmode, 0666)) != -1)
        {
            /*u_short bs;*/
            unsigned ss;

            /* Get the size in MB - Error trapping? None? 
             * Possible check for a zero return? That's what 
             * we get when no valid conversion happened.
             * ND 2019. */
            ss = strtoul (size, NULL, 10);

            /* Looks like we are testing if there's room for the file. */
            /* ss << 20 = size in bytes. */
            lseek(q->fd, (ss << 20), SEEK_SET);
            lseek(q->fd, 0, SEEK_SET);
#ifdef HAVE_FTRUNCATE
            ftruncate(q->fd, (ss << 20));
#endif
            /* Size must be between 1 and 256 MB. 
             * Why the upper limit? 
             * ND 2019. */
            if (ss < 1)
                ss = 1;

            if (ss > 256)
                ss = 256;

            q->mode = fmode;

            /* I changed this to simply use the size in bytes. ND 2019 */
            init (q, label, ss << 20);
            close (q->fd);
        }
        else
            perror ("New QXL");
        if(q) free (q);
    }
    else
        fputc ('\n', stdout);
    if (label != lab)
    {
        if (fn)
            free (fn);
        if (size)
            free (size);
        if (label)
            free (label);
    }

    return 0;
}


/*--------------------------------------------------------------------
 * SHELL Command.
 * Opens a shell session on the OS.
 *--------------------------------------------------------------------*/
static int qsystem (QXL * qxl, short mflag, char **av)
{
    char cmd[1024], *c;

    concatargs (av, cmd);

    if (*cmd == 0)
    {
        if ((c = getenv (SHELL)))
        {
            strcpy (cmd, c);
        }
    }

    if (*cmd)
    {
        system (cmd);
    }
    return 0;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int clone_cb (QXL * sqxl, QLDIR * d, void *q, void *pb, u_short c)
{
    QXL *dqxl = (QXL *)q;
    /* UNUSED: int flen; */
    char *sd, *dd;
    int nsd;
    char df[128];
    char *p;
    int nfn;

    if(d->nlen)
    {
        sd = (char *)pb;
        nsd = strlen(sd);
        dd = sd + nsd + 1;

        strcpy(df, dd);
        if(*df && *df != '_')
        {
            strcat(df, "_");
        }

        /* UNUSED: flen = d->length - sizeof (QLDIR); */
        if(d->type == 0xff)
        {
            qcd(dqxl, 0, NULL);
            p = strcat(df, (char *)d->name); // cast for c68, alas
            if(qcd(dqxl, QX_FAIL_SILENTLY, &p))
                qmkdir(dqxl, 0, &p);
        }
        else /*if (flen > 0)*/
        {
            char *args[3];
            p = (char *)sqxl->tmpd.name;
            nfn = strlen(p);
            if(nfn && *(d->name+nfn) == '_') nfn++;
            p = strcat(df, p);

            sqxl->fp = tmpfile();
            cpfile(sqxl, d);
            rewind(sqxl->fp);
            qcd(dqxl, 0, 0);
            qcd(dqxl, 0, &p);
            args[0] = (char *)sqxl->fp;
            args[1] = (char *)d->name + nfn; // cast for c68, alas
            args[2] = (char *)d->date;
            qwrite(dqxl, (short)QX_ARGV0_IS_FP, args);
        }
    }
    return 0;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int makeclone (QXL * sqxl, QXL *dqxl, char *paths)
{
    QLDIR *buf;
    buf = readqldir (sqxl, &sqxl->curd);
    sqxl->tmpd = *buf;
    processdir (sqxl, buf, &sqxl->curd, clone_cb, (void *)dqxl, paths, 1);
    free (buf);
    return 0;
}


#define COPY_IN 'I'
#define COPY_OUT 'O'

#ifdef GO32
# define SEP ';'
#else
# define SEP ':'
#endif

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static char *initpath(QXL *qxl, char *dpath)
{

    if(qxl->curd.nlen)
    {
        dpath = stpcpy(dpath, (char *)qxl->curd.name);
        *dpath++ = '_';
        *dpath = 0;
    }

    return dpath;
}

/*--------------------------------------------------------------------
 * CLONE Command.
 * Clones the current QXL file.
 *--------------------------------------------------------------------*/
static int qclone(QXL * qxl, short mflag, char **av)
{
    QXL *sqxl, *dqxl, *lxq = NULL;
    int rv = 0;

    short in_out = 0;
    char *from;
    char *to;
    char *sep;
    char *newfile = 0;
    char *startat = 0;
    char *p = NULL,*q = NULL;

    from = *av;
    to = *(av+1);
    if(from && to)
    {
        char *paths;

        if((sep = strchr(from, SEP)) != NULL)
        {
            in_out = COPY_IN;
            p = from;
            q = to;
        }
        else if((sep = strchr(to, SEP)) != NULL)
        {
            in_out = COPY_OUT;
            p = to;
            q = from;
        }
        if(p && q)
        {
            char *dpath;

            *sep = 0;
            newfile = strdup(p);
            startat = strdup(sep+1);
            if(strcmp(startat, "_") == 0)
            {
                *startat = 0;
            }

            if(strcmp(q,"_") == 0)
            {
                *q = 0;
            }

            paths = malloc(strlen(q) + strlen(startat) + 4);
            lxq = malloc (sizeof (QXL));
            memset (lxq, 0, sizeof (QXL));

            if(in_out == COPY_OUT)
            {
                sqxl = qxl;
                dqxl = lxq;
                dqxl->mode = O_BINARY|O_RDWR;
                if(openqxl(dqxl, newfile) != -1)
                {
                    dpath = initpath(qxl, paths);
                    dpath = stpcpy(dpath, q);
                    dpath++;
                    strcpy(dpath, startat);
                }
                else rv = -1;
            }
            else
            {
                sqxl = lxq;
                dqxl = qxl;
                sqxl->mode = O_BINARY;
                if(openqxl(sqxl, newfile) != -1)
                {
                    dpath= stpcpy(paths, startat);
                    dpath++;
                    dpath = initpath(qxl, dpath);
                    strcpy(dpath, q);
                }
                else rv = -1;
            }

            if(rv == 0)
            {
                char *scd = paths;
                int n = strlen(paths);

                if(n > sqxl->curd.nlen)
                {
                    scd += sqxl->curd.nlen;
                    if(*scd == '_') scd++;
                }

                if(*scd == 0 || qcd(sqxl, 0, &scd) == 0)
                {
                    if(qcd(dqxl, QX_FAIL_SILENTLY, &dpath))
                    {
                        qmkdir(dqxl, 0, &dpath);
                        qcd(dqxl, 0, &dpath);
                    }
                    makeclone (sqxl, dqxl, paths);
                }

                if(newfile) free (newfile);
                if(startat) free (startat);
            }
            if(lxq) qclose(lxq, 0, NULL);
        }
    }
    else
    {
        rv = -1;
    }
    return rv;
}

/*--------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static int qchgmode(QXL *qxl)
{
    char *p,*q;
    p = strdup(qxl->fn);
    q = malloc(qxl->curd.nlen+1);
    memcpy(q, qxl->curd.name, qxl->curd.nlen);
    *(q+qxl->curd.nlen) = 0;
    qclose(qxl, 0, NULL);
    openqxl(qxl, p);
    qcd(qxl, QX_FAIL_SILENTLY, &q);
    free (p);
    free (q);
    return 0;
}

/*--------------------------------------------------------------------
 * RW Command.
 * Changes the current QXL file to read-write.
 *--------------------------------------------------------------------*/
static int qrwmode (QXL* qxl, short mflag, char **av)
{
    qxl->mode &= ~O_RDONLY;
    qxl->mode |= O_RDWR;
    return qchgmode(qxl);
}

/*--------------------------------------------------------------------
 * RO Command.
 * Changes the current QXL file to read-only.
 *--------------------------------------------------------------------*/
static int qromode (QXL* qxl, short mflag, char **av)
{
    qxl->mode &= ~O_RDWR;
    qxl->mode |= O_RDONLY;
    return qchgmode(qxl);
}

/*--------------------------------------------------------------------
 * The command table. The format is:
 * Command-name, function to call, brief help text, flags.
 *--------------------------------------------------------------------*/
static JTBL jtbl[] =
{
    {"cd", qcd, "Changes the QXL working directory.", QX_OPEN},
    {"lslr", qlslr, "List the contents of the current QXL directory, and sub-directories. See 'dirr'.", QX_TEXT | QX_OPEN},
    {"dirr", qlslr, "List the contents of the current QXL directory, and sub-directories. See 'lslr'.", QX_TEXT | QX_OPEN},
    {"ls", qls, "List the contents of the current QXL directory. See 'dir'.", QX_TEXT | QX_OPEN},
    {"dir", qls, "List the contents of the current QXL directory. See 'ls'.", QX_TEXT | QX_OPEN},
    {"dump", qdump, "Dumps the current QXL directory structure, warts and all!", QX_OPEN},
    {"pwd", qpwd,"Displays the current QXL working directory name.", QX_TEXT | QX_OPEN},
    {"cat", qcp,"Types/Copies a QXL file converting LF to CR/LF.. See 'cp'.", QX_OPEN},
    {"cp", qcp,"Types/Copies a QXL file converting LF to CR/LF. See 'cat'.", QX_OPEN},
#ifdef NEED_DOS_FUNCTIONS
    {"tcat", qcp,"See 'cat' but no conversion of LF takes place.", QX_TEXT | QX_OPEN},
    {"tcp", qcp,"See 'cp' but no conversion of LF takes place.", QX_TEXT | QX_OPEN},
    {"twr", qwrite,"see 'wr' but no conversion of CR/LF takes place", QX_TEXT | QX_OPEN},
    {"twrite", qwrite,"see 'write' but no conversion of CR/LF takes place", QX_TEXT | QX_OPEN},
#endif
    {"info", qinfo, "Displays brief information about the current QXL file.", QX_TEXT | QX_OPEN},
    {"header", qheader, "Displays the QLWA header for the current QXL file.", QX_TEXT | QX_OPEN},
    {"quit", quit, "Exits from 'qxltool'. See 'exit'."},
    {"exit", quit, "Exits from 'qxltool'. See 'quit'."},
    {"init", qinit, "Reformats the current QXL file to the same size as before.", QX_OPEN},
    {"wr", qwrite, "Writes a host file into the current QXL directory. Converts CR/LF to LF. See 'write'.", QX_OPEN},
    {"write", qwrite, "Writes a host file into the current QXL directory. Converts CR/LF to LF. See 'wr'.", QX_OPEN},
    {"rmdir", qrmdir, "Deletes a QXL directory, which must be empty.", QX_OPEN},
    {"rm", qrm, "Deletes a file from the QXL file.", QX_OPEN},
    {"mkdir", qmkdir, "Creates a new QXL directory within the current one.", QX_OPEN},
    {"version", version, "Display 'qxltool' version and host OS."},
    {"help", help, "Displays this potentially helpful text!"},
    {"open", qxlopen, "Opens an existing QXL file for processing."},
    {"close", qclose, "Closes the current QXL file.", QX_OPEN},
    {"format", qformat, "Creates and formats a new QXL file."},
    {"shell", qsystem, "Spawns a shell. (To the OS)."},
    {"clone", qclone, "Copies all/part of a QXL file to another one.", QX_OPEN},
    {"RO", qromode, "Makes QXL file read-only.", QX_OPEN},
    {"RW", qrwmode, "Makes QXL file read-write.", QX_OPEN},
#ifdef mc68000
    {"fix_geometry", qfixgeometry, "Fixes the disc geometry to what SMSQ expects."},
#endif
    {NULL, NULL}
};

/*--------------------------------------------------------------------
 * HELP Command.
 * Displays help. Actually, just displays the command table's list of
 * Commands.
 *--------------------------------------------------------------------*/
static int help (QXL * qxl, short mflag, char **av)
{
    JTBL *j;
    fputs ("Commands in qxltool " VERSION "\n\n", qxl->fp);

    for (j = jtbl; j->name; j++)
    {
        fprintf (qxl->fp, "\t%s\t%s\n", j->name, j->helpText);
    }
    fputs ("\nRead the fine manual for more information\n", qxl->fp);
    return 0;
}

/*--------------------------------------------------------------------
 * Usage - displayed when the command line is incorrect.
 *--------------------------------------------------------------------*/
static void ShowCmdHelp()
{
    char *txt[] =
    {
        "Usage: qxltool [-W] [-w] [-h] [QXL_FILE] [params]",
        "If QXL_FILE is not defined on the command line, then the environment",
        "variable is named QXL_FILE is used",
        "",
        "qxltool defaults to a readonly QXL.WIN file to protect your data,",
        "if you're feeling brave, you can run qxltool in read-write mode",
        "by giving the '-w' option.",
        "",
        "\tqxltool -w /cdrom/qxl.win",
        "",
        "qxltool can attempt to format and use a new QXL.WIN file by giving",
        "more than three parameters:"
        "",
        "\tqxltool -w file_name size_in_MB Label0 ... LabelN ",
        "",
        "for example:",
        "",
        "\tqxltool -w /tmp/qxl.win 8 QXL Disk Label",
        "",
        "Would create an 8Mb QXL file (/tmp/qxl.win) with a disk label",
        "\"QXL Disk Label\".",
        "",
        "In order to format an existing QXL file (or partition), then you must",
        "give a -W option. This is to protect people from accidentally",
        "formatting an existing file/partition.",
        "",
        "\tqxltool -W /tmp/oldqxl.win 8 QXL Disk Label", "","Good Luck!",
        NULL
    };
    char **p;
    for(p = txt; *p; p++)
    {
        fputs(*p, stderr);
        fputc('\n', stderr);
    }
}

/*====================================================================
 * Start Here. There must be at least one parameter, the file name,
 * unless the environment variable QXL_FILE is set, in which case it
 * will be used.
 *====================================================================*/
int main (int ac, char **av)
{

    char *fn;                               /* QXL file name */
    int c;                                  /* Commandline switches */
    int defmod = O_RDONLY | O_BINARY;       /* Default open mode */
    char *lcmd = NULL;                      /* -c option's command to execute */

    QXL *qxl = malloc (sizeof (QXL));
    memset (qxl, 0, sizeof (QXL));
#ifdef __unix__
    signal (SIGPIPE, SIG_IGN);
#endif

    /* Do we have any options?
     * -c = Run a qxltool command and exit.
     * -w = Open QXL file in read-write mode.
     * -W = Allows QXL file to be formatted.
     */
    while ((c = getopt (ac, av, "wWc:")) != EOF)
    {
        switch (c)
        {
        case 'W':
            qxl->fmode = 1;
        case 'w':
            defmod = O_BINARY | O_RDWR;
            break;
        case 'c':
            lcmd = strdup(optarg);
            break;
        }
    }

    /* Look for the QXL filename, or use a valid default. */
    fn = defargs (*(av + optind));

    qxl->mode |= defmod;                    /* Defaults to read only mode */

    /* If requested, create and format a new QXL file first.
     * 
     * something like: qxltool -W QXL_filename MB label for example:
     *
     * qxltool -W new_qlx.win 30 "30MB QXL File"
     */
    if (ac - optind > 2)    
    {
        qformat (qxl, 0, av + optind);
    }

    fprintf(stderr, "Size of DIR (64): %d\n", (int)sizeof(QLDIR));
    fprintf(stderr, "Size of TIME_T (4): %d\n", (int)sizeof(time_t));

    /* Open the QXL file and loop around processing commands. */
    if ((openqxl (qxl, fn)) != -1)
    {
        char cmd[LINSIZ];
        u_char *pcmd, *ptr;

        while (1)
        {
            JTBL *j;
            char **args;

            /* Did we have a -c command supplied? */
            if(lcmd)
            {
#ifndef HAVE_LIBREADLINE
                pcmd = strcpy(cmd, lcmd);
#else
                pcmd = lcmd;
#endif
            }
            else if((pcmd = READLINE ("> ", cmd)) == NULL)
            {
                break;
            }

            if ((ptr = strchr (pcmd, '#')))
                *ptr = 0;

            ptr = pcmd + strspn (pcmd, "\t ");

            /* Search the command table for the supplied command. */
            for (j = jtbl; j->name; j++)
            {
                int n = strlen (j->name);
                if (memcmp (ptr, j->name, n) == 0)
                {
                    /* We have a valid command. Deal with it. */
                    strim (ptr);
                    while (*(ptr + n) > ' ')
                        n++;
                    n += strspn (ptr + n, "\t ");
                    if (qxl->fd <= 0 && j->flag & QX_OPEN)
                    {
                        fputs ("That requires an open QXL_FILE\n", stderr);
                    }
                    else
                    {
                        /* Extract arguments for the command to be executed. */
                        args = openout (qxl, ptr + n, j->flag);

                        /* Call the appropriate function here */
                        if ((j->func) (qxl, j->flag, args))
                        {
                            fputs ("Invalid argument(s)\n", stderr);
                        }

                        /* Reclaim any allocated memory for the arguments above. */
                        closeout (qxl, args);
                    }
                    break;
                }
            }

            /* Invalid command. */
            if (*pcmd && j->name == NULL)
            {
                fprintf (stdout, "Invalid command %s\n", pcmd);
            }

            /* Add cmd to the history, reclaim memory for pcmd. */
            FREECMD (cmd, pcmd);

            /* Only process one -c command then exit. */
            if(lcmd) break;
        }

        /* Sign off when NOT called with -c. */
        if(!lcmd && isatty(fileno(stdout)))
            fputs ("quit\n", stdout);
    }
    else
    {
        /* openqxl() failed. Show some command line help. */
        ShowCmdHelp();
    }

    return 0;
}
