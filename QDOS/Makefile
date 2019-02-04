# ---------------------------------------------------------------
#| qxltool.c : Access QXL.WIN files from other operating systems |
#|                                                               |
#| (c) Jonathan Hudson, April 1998                               |
#| No Warranty                                                   |
# ---------------------------------------------------------------
VPATH = ../

DEFS = -DHAVE_CONFIG_H

QCC = qdos-gcc
QFLAGS = -O2 -I. -I$(VPATH) $(DEFS)

qxltool: qxltool.o
	$(QCC) $(QFLAGS) -o qxltool qxltool.o -lm
	ls -l qxltool
	qcp qxltool /ql/exe/qxltool

qxltool.o : ../qxltool.c ../qxltool.h ../version.h
	$(QCC) $(QFLAGS) -c -o $@ $<

clean:
	rm -f *.o qxltool

