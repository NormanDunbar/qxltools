/*--------------------------------------------------------------.
| qxltool.c : Access QXL.WIN files from other operating systems |
|                                                               |
| (c) Jonathan Hudson, 1998 - 1999                     		|
| No Warranty                                                   |
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
char *stpcpy (char *s1, char *s2)
{
    strcpy (s1, s2);
    return s1 + strlen (s1);
}
#endif


#ifndef WORDS_BIGENDIAN
u_short inline swapword (u_short val)
{
    return (u_short) (val << 8) + (val >> 8);
}

u_long inline swaplong (u_long val)
{
    return (u_long) (((u_long) swapword (val & 0xFFFF) << 16) |
                     (u_long) swapword (val >> 16));
}

# define SW(a,c) (a)->c = swapword((a)->c)
# define SL(a,c) (a)->c = swaplong((a)->c)
#else
u_short swapword (u_short val)
{
    return val;
}

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
void myconsetup (chanid_t ch, WINDOWDEF_t * w);

struct WINDOWDEF _condetails =
{6, 1, 0, 4, 512, 256, 0, 0};
void (*_consetup) () = myconsetup;

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

static u_char *strim (u_char *s)
{
    u_char *p;
    short n;

    n = strlen (s);
    p = s + n;
    while (*--p <= ' ' && n--)
    {
        *p = '\0';
    }
    return s;
}

static void h_normalise (HEADER * h)
{
    SW (h, namlen);
    SW (h, rand);
    SW (h, access);
    SW (h, sectc);
    SW (h, total);
    SW (h, free);
    SW (h, direct);
    SW (h, formatted);
    SW (h, first);
    SL (h, dlen);
    SW (h, map[0]);
}

static void writeheader (QXL * qxl)
{
    HEADER h;
    int i;

    lseek (qxl->fd, 0, SEEK_SET);
    qxl->h.access++;
    h = qxl->h;
    h.sectc >>= 9;
    for(i = 0; i < sizeof(h.name); i++)
    {
        if(h.name[i] == 0) h.name[i] = ' ';
    }
    h_normalise (&h);
    write (qxl->fd, &h, sizeof (HEADER));
    strim ((char *) h.name);
}

static void readheader (QXL * qxl)
{
    lseek (qxl->fd, 0, SEEK_SET);
    read (qxl->fd, &qxl->h, sizeof (HEADER));
    h_normalise (&qxl->h);
    if (memcmp (qxl->h.id, "QLWA", 4) == 0)
    {
        /* Now create a fake directory record for the root directory */
        qxl->root.length = qxl->h.dlen;
        qxl->root.map = qxl->h.direct;
        qxl->root.type = 0xff;
        *qxl->root.name = 0;
        qxl->root.nlen = 0;
        qxl->h.sectc <<= 9;
        strim ((char *) qxl->h.name);
    }
    else
    {
        fputs ("Not a QXL file\n", stderr);
        exit (0);
    }
}

static void d_normalise (QLDIR * d)
{
    SL (d, length);
    SW (d, type);
    SL (d, data);
    SW (d, nlen);
    SL (d, date);
    SW (d, map);
}

static int d_list (QXL * qxl, QLDIR * d, void *a, void *pb, u_short c)
{
    struct tm *tm;
    char s[128];
    size_t n;
    char *p;
    int flen, m;

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
            sprintf (s + n, "(%ld) ", d->data);
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

static u_short getcluster (QXL * qxl, u_short o)
{
    u_short c, n;
    qxl->h.free--;
    c = nextcluster (qxl, qxl->h.first, 0);
    n = qxl->h.first;
    if (o)
        updatecluster (qxl, o, n);
    updatecluster (qxl, n, 0);
    qxl->h.first = c;
    return n;
}

static u_short nextcluster (QXL * qxl, u_short c, short flag)
{
    u_long newoff;
    u_short d;

    newoff = c * 2 + offsetof (HEADER, map);
    lseek (qxl->fd, newoff, SEEK_SET);
    read (qxl->fd, &d, sizeof (short));
    d = swapword (d);
    if (d == 0 && flag)
    {
        if (qxl->h.free)
        {
            d = getcluster (qxl, c);
        }
        else
        {
            fputs ("Disk is full!\n", stderr);
            exit (0);
        }
    }
    return d;
}

static QLDIR *readqldir (QXL * qxl, QLDIR * cur)
{
    u_char *buf, *p;
    int dlen, rlen;
    u_short fcluster;

    dlen = cur->length;
    p = buf = malloc (dlen);

    for (fcluster = cur->map; (fcluster && dlen);)
    {
        lseek (qxl->fd, fcluster * qxl->h.sectc, SEEK_SET);
        if (dlen < qxl->h.sectc)
            rlen = dlen;
        else
            rlen = qxl->h.sectc;
        read (qxl->fd, p, rlen);
        p += rlen;
        dlen -= rlen;
        fcluster = nextcluster (qxl, fcluster, 0);
    }
    return (QLDIR *) buf;
}

static void writeqldir (QXL * qxl, QLDIR * cur, QLDIR * buf)
{
    u_char *p;
    int dlen, rlen;
    u_short clu;
    u_short ifree;

    dlen = cur->length;
    p = (u_char *) buf;
    ifree = qxl->h.free;

    for (clu = cur->map; dlen;)
    {
        lseek (qxl->fd, clu * qxl->h.sectc, SEEK_SET);
        if (dlen < qxl->h.sectc)
            rlen = dlen;
        else
            rlen = qxl->h.sectc;
        write (qxl->fd, p, rlen);
        p += rlen;
        dlen -= rlen;
        if (dlen)
            clu = nextcluster (qxl, clu, 1);
    }
    if (ifree != qxl->h.free)
        writeheader (qxl);
}

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


static int quit (QXL * qxl, short mflag, char **av)
{
    exit (0);
    return 0;
}

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

static int qdump (QXL * qxl, short mflag, char **av)
{
    QLDIR *buf;
    char *cmd = *av;
    u_short ft = 1;

    if (cmd && *cmd)
    {
        fputs ("ls[lr] takes no parameters (currently), cd first\n", stderr);
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


static void cpfile (QXL * qxl, QLDIR * d)
{
    u_char *buf;
    int dlen, rlen;
    u_short cluster;
    short off = sizeof (QLDIR);

    buf = (u_char *) alloca (qxl->h.sectc);
    dlen = d->length;
    cluster = d->map;

    if (qxl->fp)
    {
        for (cluster = d->map; cluster; cluster = nextcluster (qxl, cluster, 0))
        {
            lseek (qxl->fd, cluster * qxl->h.sectc, SEEK_SET);
            if (dlen < qxl->h.sectc)
                rlen = dlen;
            else
                rlen = qxl->h.sectc;
            read (qxl->fd, buf, rlen);

            fwrite (buf + off, 1, rlen - off, qxl->fp);

            dlen -= rlen;
            off = 0;
        }
    }

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

static void updatecluster (QXL * qxl, u_short f, u_short c)
{
    lseek (qxl->fd, f * 2 + sizeof (QLDIR), SEEK_SET);
    c = swapword (c);
    write (qxl->fd, &c, sizeof (short));
}

static void updatefree (QXL * qxl, QLDIR * d)
{
    u_short c, cn, f;

    for (c = d->map; c;)
    {
        cn = nextcluster (qxl, c, 0);
        f = qxl->h.first;
        qxl->h.first = c;
        updatecluster (qxl, c, f);
        c = cn;
        qxl->h.free++;
    }
    writeheader (qxl);
}

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

static void rmfile (QXL * qxl, QLDIR * d)
{
    char fn[40];
    memcpy (fn, d->name, d->nlen);
    *(fn + d->nlen) = 0;
    updatefree (qxl, d);
    updatedir (qxl, d);
}

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

static double unify (QXL * qxl, u_short x, char *f)
{
    double ts;

    ts = x * qxl->h.sectc;
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


static int qinfo (QXL * qxl, short mflag, char **p)
{
    double ts, fs;
    char tsb, fsb;
    struct stat st;

    fstat (qxl->fd, &st);

    ts = unify (qxl, qxl->h.total, &tsb);
    fs = unify (qxl, qxl->h.free, &fsb);

    fprintf (qxl->fp, "Info for QXL file %s %s\n",
             qxl->fn, (qxl->mode == (O_RDONLY | O_BINARY)) ? "(RO)" : "");

    fprintf (qxl->fp, "Label %-.*s", qxl->h.namlen, qxl->h.name);
    if (strlen ((char *) qxl->h.name) != qxl->h.namlen)
    {
        fprintf (qxl->fp, " [%s]", qxl->h.name);
    }

    fprintf (qxl->fp, "\nSector size %d bytes\n", qxl->h.sectc);
    fprintf (qxl->fp, "Total sectors %d (%.2f %cb, on disk %d b)\n",
             qxl->h.total, ts, tsb, (int) st.st_size);
    fprintf (qxl->fp, "Free  sectors %d (%.2f %cb)\n",
             qxl->h.free, fs, fsb);
    fprintf (qxl->fp, "Formatted %d, (map len %d*512), rand %d, access %d\n",
             qxl->h.formatted, qxl->h.formatted, qxl->h.rand, qxl->h.access);
    return 0;
}

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

    buf = (u_char *) alloca (qxl->h.sectc);

    memset (buf, 0, qxl->h.sectc);
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
            ilen = qxl->h.sectc - off;
            if ((nr = fread (buf + off, 1, ilen, fp)) > 0)
            {
                c = getcluster (qxl, c);
                if (off)
                {
                    f->map = c;
                }
                lseek (qxl->fd, c * qxl->h.sectc, SEEK_SET);
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
        lseek (qxl->fd, c * qxl->h.sectc, SEEK_SET);
        write (qxl->fd, buf, qxl->h.sectc);
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
            qxl->h.dlen = qxl->root.length;
        }
    }
    writeheader (qxl);
}

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
                           qxl->h.sectc;

                    if (need > (qxl->h.free - 1))
                    {
                        fprintf (stderr,
                                 "%s %d No room (%d free sectors, %ld b wanted %d)\n",
                                 fn0, qxl->h.sectc,
                                 qxl->h.free,
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


static int version (QXL * qxl, short mflag, char **av)
{
    fputs ("qxltool " VERSTR " for " TARGET "\n", stdout);
    return 0;
}


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

static void concatargs (char **av, char *cmd)
{
    char *c = cmd;

    if (*av)
    {
        while (*av)
        {
            c = stpcpy (c, *av);
            *c++ = ' ';
            av++;
        }
        --c;
    }
    *c = 0;
}

static void init (QXL * qxl, char *lab, u_short bs, u_short ts)
{
    char *l;
    short ns;
    u_short i, j, k, m;
    size_t n;

    if ((qxl->mode & O_RDWR) == O_RDWR)
    {
        l = alloca (bs);

        memset (&qxl->h, 0, sizeof (HEADER));
        memset (qxl->h.name, ' ', 20);

        qxl->h.total = ts;
        qxl->h.sectc = bs;
        qxl->h.numberOfMaps = 1;
        n = strlen (lab);
        if (n > 20)
            n = 20;

        memcpy (qxl->h.name, lab, n);
        qxl->h.namlen = n;
        qxl->h.access = 0;
        memcpy (qxl->h.id, "QLWA", 4);
        ns = (qxl->h.total * 2 + bs - 1) / bs + 1;
        lseek (qxl->fd, sizeof (QLDIR), SEEK_SET);
        for(i=1; i<=ns-1; i++)
        {
            j=swapword(i);
            write (qxl->fd, &j, sizeof (short));
        }
        qxl->h.map[0]=1;  /* writeheader (over)writes this ! */
        j=0;
        write(qxl->fd,&j,2);
        i++; /* EOF MAP */

        qxl->h.formatted = (qxl->h.total*2+511+0x40)/512;
        /* this appears to be the length of map in sectors..*/

        qxl->h.direct = i-1;
        write(qxl->fd,&j,2);
        i++; /* EOF DIR */

        qxl->h.first = i-1;

        for (i = qxl->h.first; i < qxl->h.total - 1;)
        {
            i++;
            j = swapword (i);
            write (qxl->fd, &j, sizeof (short));
        }

        j = 0;
        write (qxl->fd, &j, sizeof (short));

        qxl->h.free = qxl->h.total - ns - 1;
        qxl->h.dlen = sizeof (QLDIR);
        srand (time (NULL));
        qxl->h.rand = rand ();

        writeheader (qxl);

        lseek (qxl->fd, bs * qxl->h.direct, SEEK_SET);
        l = alloca (bs);
        memset (l, 0, bs);
        fputs ("Formatting ....", stdout);

        k = j = qxl->h.total / 20;
        m = 5;

        for (i = qxl->h.direct; i < qxl->h.total; i++)
        {
            if (write (qxl->fd, l, bs) != bs)
            {
                perror ("Format");
                exit (0);
            }
            if (i > k)
            {
                printf (" %3d%%\b\b\b\b\b", m);
                fflush (stdout);
                k += j;
                m += 5;
            }
        }
        fputs (" Done\n", stdout);
#ifdef mc68000
        fputs(" you must fix the geometry before SMSQ can use this filesystem\n",stdout);
#endif
#if 0
        qxl->root.length = qxl->h.dlen;
        qxl->root.map = qxl->h.direct;
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

static int qinit (QXL * qxl, short mflag, char **av)
{
    char lab[80];
    u_short bs = qxl->h.sectc;
    u_short ts = qxl->h.total;
    concatargs (av, lab);
    init (qxl, lab, bs, ts);
    qcd(qxl, 0, 0);
    return 0;
}

#ifdef HAVE_LIBREADLINE
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

static int qformat (QXL * qxl, short mflag, char **av)
{
    char cmd[LINSIZ];
    char *pcmd;
    char lab[32];

    char *fn = NULL, *size = NULL, *label = NULL;

    if (*av)
    {
        fn = *av++;
        if (*av)
        {
            size = *av++;
            if (*av)
            {
                concatargs (av, lab);
                label = lab;
            }
        }
    }

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
            static short mbs[] =
            {32, 64, 128, 256, 0};
            u_short ss, bs, ts;
            short i;

            ss = strtoul (size, NULL, 10);

            lseek(q->fd, (ss << 20), SEEK_SET);
            lseek(q->fd, 0, SEEK_SET);
#ifdef HAVE_FTRUNCATE
            ftruncate(q->fd, (ss << 20));
#endif
            if (ss < 1)
                ss = 1;
            if (ss > 256)
                ss = 256;
            for (i = 0; mbs[i]; i++)
            {
                if (ss <= mbs[i])
                    break;
            }

            bs = (1 << i);
            ts = (ss << 20) / (bs<<9);
            bs <<= 9;
            q->mode = fmode;
            init (q, label, bs, ts);
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

static int clone_cb (QXL * sqxl, QLDIR * d, void *q, void *pb, u_short c)
{
    QXL *dqxl = (QXL *)q;
    int flen;
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

        flen = d->length - sizeof (QLDIR);
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

static int qrwmode (QXL* qxl, short mflag, char **av)
{
    qxl->mode &= ~O_RDONLY;
    qxl->mode |= O_RDWR;
    return qchgmode(qxl);
}

static int qromode (QXL* qxl, short mflag, char **av)
{
    qxl->mode &= ~O_RDWR;
    qxl->mode |= O_RDONLY;
    return qchgmode(qxl);
}

static JTBL jtbl[] =
{
    {"cd", qcd, QX_OPEN},
    {"lslr", qlslr, QX_TEXT | QX_OPEN},
    {"ls", qls, QX_TEXT | QX_OPEN},
    {"dump", qdump, QX_OPEN},
    {"pwd", qpwd, QX_TEXT | QX_OPEN},
    {"cat", qcp, QX_OPEN},
    {"cp", qcp, QX_OPEN},
#ifdef NEED_DOS_FUNCTIONS
    {"tcat", qcp, QX_TEXT | QX_OPEN},
    {"tcp", qcp, QX_TEXT | QX_OPEN},
    {"twr", qwrite, QX_TEXT | QX_OPEN},
#endif
    {"info", qinfo, QX_TEXT | QX_OPEN},
    {"quit", quit},
    {"exit", quit},
    {"init", qinit, QX_OPEN},
    {"wr", qwrite, QX_OPEN},
    {"rmdir", qrmdir, QX_OPEN},
    {"rm", qrm, QX_OPEN},
    {"mkdir", qmkdir, QX_OPEN},
    {"version", version},
    {"help", help},
    {"open", qxlopen},
    {"close", qclose, QX_OPEN},
    {"format", qformat},
    {"shell", qsystem},
    {"clone", qclone, QX_OPEN},
    {"RO", qromode, QX_OPEN},
    {"RW", qrwmode, QX_OPEN},
#ifdef mc68000
    {"fix_geometry", qfixgeometry},
#endif
    {NULL, NULL}
};

static int help (QXL * qxl, short mflag, char **av)
{
    JTBL *j;
    fputs ("Commands in qxltool " VERSION "\n\n", qxl->fp);

    for (j = jtbl; j->name; j++)
    {
        fprintf (qxl->fp, "\t%s\n", j->name);
    }
    fputs ("\nRead the fine manual for more information\n", qxl->fp);
    return 0;
}

static void ShowCmdHelp()
{
    char *txt[] =
    {
        "Usage: qxltool [-W] [-w] [-h] [QXL_FILE] [params]",
        "If QXL_FILE is not defined on the command line, then the environment",
        "variable is named QXL_FILE is used",
        "",
        "qxltool defaults to a readonly QXL.WIN file to protect your data,",
        "if you're feeling brave, you can run qxltool is read-write mode",
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


int main (int ac, char **av)
{

    char *fn;
    int c;
    int defmod = O_RDONLY | O_BINARY;
    char *lcmd = NULL;

    QXL *qxl = malloc (sizeof (QXL));
    memset (qxl, 0, sizeof (QXL));
#ifdef __unix__
    signal (SIGPIPE, SIG_IGN);
#endif
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

    fn = defargs (*(av + optind));

    qxl->mode |= defmod;

    if (ac - optind > 2)
    {
        qformat (qxl, 0, av + optind);
    }

    if ((openqxl (qxl, fn)) != -1)
    {
        char cmd[LINSIZ];
        u_char *pcmd, *ptr;

        while (1)
        {
            JTBL *j;
            char **args;

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

            for (j = jtbl; j->name; j++)
            {
                int n = strlen (j->name);
                if (memcmp (ptr, j->name, n) == 0)
                {
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
                        args = openout (qxl, ptr + n, j->flag);
                        if ((j->func) (qxl, j->flag, args))
                        {
                            fputs ("Invalid argument(s)\n", stderr);
                        }
                        closeout (qxl, args);
                    }
                    break;
                }
            }
            if (*pcmd && j->name == NULL)
            {
                fprintf (stdout, "Invalid command %s\n", pcmd);
            }
            FREECMD (cmd, pcmd);
            if(lcmd) break;
        }
        if(!lcmd && isatty(fileno(stdout)))
            fputs ("quit\n", stdout);
    }
    else
    {
        ShowCmdHelp();
    }

    return 0;
}
