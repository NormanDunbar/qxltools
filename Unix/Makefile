# Generated automatically from Makefile.in by configure.
srcdir = ..
top_srcdir = ..
VPATH =  ..
LIBS = -lreadline -lm 
CFLAGS = -g -O2 -I. -I$(srcdir) -Wall -Wno-unused-result -Wno-pointer-sign -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
CC=gcc
DEFS=-DHAVE_CONFIG_H
EXEEXT=

qxltool: qxltool.o
	$(CC) $(CFLAGS) -o qxltool qxltool.o $(LIBS)

qxltool.o: $(srcdir)/qxltool.c $(srcdir)/qxltool.h config.h $(srcdir)/version.h
	$(CC) $(CFLAGS) $(DEFS) -c $(srcdir)/qxltool.c

clean:
	-rm -f *~
	-rm -f '#*#'
	-rm -f qxltool.o

distclean: clean
	-rm -f config.h config.status config.cache
	-rm -f qxltool$(EXEEXT)
