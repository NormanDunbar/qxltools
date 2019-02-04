################################################################
#  Makefile
# 
# 
#  Copyright (C) 1999 Jonathan R. Hudson
#  Developed by Jonathan R. Hudson <jrhudson@bigfoot.com>
# 
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
# 
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
# 
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
# 
################################################################



ifeq ($(OSTYPE),cygwin32)
	OS=nt
	DIR=NT
else	
	OS=unix
	DIR=Unix	
endif

SRC = qxltool.txt README.QXL qxltool.h qxltool.c Makefile version.h COPYING \
	INSTALL Makefile.in config.h.in configure.in configure

all: domake

domake:	$(OS)

unix:	Unix/Makefile
	cd Unix ; $(MAKE)

nt:	NT/Makefile
	cd NT ; $(MAKE)

dos:
	cd DOS ; $(MAKE)

qdos:
	cd QDOS ; $(MAKE)

clean:
	-rm -f *~
	-rm -f '#*#'
	cd $(DIR) ; $(MAKE) clean 

distclean:
	-rm -f *~
	-rm -f '#*#'
	-rm -f config.cache config.status
	cd $(DIR) ; $(MAKE) distclean 

dist:   $(SRC) 
	-rm -rf Unix/* 
	qlzip -9zr /tmp/qxltool Unix docs QDOS/qxltool QDOS/Makefile \
	NT/fnmatch.c NT/getopt.c NT/getopt.h NT/qxltool.exe \
	DOS/Makefile DOS/qxltool.exe \
	xfer $(SRC) <version.h
	@ echo ""
#	cp qxltool.zip /home/jrh/Comms/WebPage/misc/qxltool.zip

Unix/Makefile:
	cd Unix ; ../configure

NT/Makefile:
	cd NT ; ../configure
