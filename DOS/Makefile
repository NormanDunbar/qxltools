# ---------------------------------------------------------------
#| qxltool.c : Access QXL.WIN files from other operating systems |
#|                                                               |
#| (c) Jonathan Hudson, April 1998                               |
#| No Warranty                                                   |
# ---------------------------------------------------------------
VPATH = ../

DCC = gcc-go32

# These options are mainly for Unix 
DEFS = -DHAVE_CONFIG_H

CFLAGS = -O3 -Wall -s -I. -I$(VPATH)  

qxltool: qxltool.o version.h
	$(DCC) -s -o qxltool qxltool.o
	rm qxltool

qxltool.o : qxltool.c qxltool.h version.h
	$(DCC) $(CFLAGS) $(DEFS) -c -o $@ $<

qxltool.exe : qxltool

clean:
	rm -f *.o *.exe *~


