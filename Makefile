CC = gcc
VERSION=0.3.99
WINGS_LIBS:=$(shell pkg-config --libs WINGs)
WINGS_CLFAGS:=$(shell pkg-config --cflags WINGs)
#DEBUG=-DDEBUG


ifdef $(DEBUG)
  OPT=-g
else
  OPT=-Os
endif
#wings_INCS      = -I. -I/usr/X11R6/include -I/usr/local/include -I/usr/pkg/include
#wings_LIBS      = -L/usr/X11R6/lib -L/usr/local/lib -L/usr/pkg/lib -lWINGs -lXft -lX11 -lwraster
# you may need:
# -lintl -liconv
CFLAGS = $(OPT) $(DEBUG) -DHAVE_BACKEND_MPLAYER $(WINGS_CFLAGS) -DVERSION=\"$(VERSION)\"

PROGRAM = mmp

#OBJECTS = mmp.o WMAddOns.o backend.o frontend.o
OBJECTS = mmp.o WMAddOns.o backend_mplayer.o frontend.o

.SUFFIXES:	.o .c

# -std=c99
.c.o :
	$(CC) -c -o $@ $(CFLAGS) $<

all:    $(PROGRAM)

$(PROGRAM):	$(OBJECTS)
	$(CC) -o $(PROGRAM) $^ $(WINGS_LIBS)
	#strip $(PROGRAM)

clean: 
	rm -f *.o $(PROGRAM)
