CC = gcc
VERSION=0.5
WINGS_LIBS:=$(shell pkg-config --libs WINGs)
WINGS_CFLAGS:=$(shell pkg-config --cflags WINGs)
# if pkg-config not installed/doesn't work, set the above manually to something like
# (-lintl, -liconv may not be needed)
#WINGS_CFLAGS:=-I. -I/usr/X11R6/include -I/usr/local/include -I/usr/pkg/include
#WINGS_LIBS:=-L/usr/X11R6/lib -L/usr/local/lib -L/usr/pkg/lib -lWINGs -lXft -lX11 -lwraster -lintl -liconv

# possible values: mpg123 mplayer
BACKENDS=mpg123 mplayer

# uncomment for debug version
#DEBUG=-DDEBUG


ifdef DEBUG
  OPT=-g
else
  OPT=-Os
endif
CFLAGS = $(OPT) $(DEBUG) $(addprefix -DHAVE_BACKEND_, $(BACKENDS)) $(WINGS_CFLAGS) -DVERSION=\"$(VERSION)\"

PROGRAM = mmp

OBJECTS = mmp.o WMAddOns.o frontend.o $(addprefix backend_, $(addsuffix .o, $(BACKENDS)))

.SUFFIXES:	.o .c

# -std=c99
.c.o :
	$(CC) -c -o $@ $(CFLAGS) $<

all:    $(PROGRAM)

$(PROGRAM):	$(OBJECTS)
	$(CC) -o $(PROGRAM) $^ $(WINGS_LIBS)

clean: 
	rm -f *.o $(PROGRAM)
