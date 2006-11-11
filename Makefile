CC = gcc
wings_INCS      = -I. -I/usr/X11R6/include
wings_LIBS      = -L/usr/X11R6/lib -L/usr/local/lib -lWINGs -lXft -lX11 -lwraster

PROGRAM = mmp

#OBJECTS = mmp.o WMAddOns.o backend.o frontend.o
OBJECTS = mmp.o WMAddOns.o backend_mplayer.o frontend.o

.SUFFIXES:	.o .c

# -std=c99
.c.o :
	$(CC) -g -c $(wings_INCS) -o $@ $<

all:    $(PROGRAM)

$(PROGRAM):	$(OBJECTS)
	$(CC) -o $(PROGRAM) $(OBJECTS) $(wings_LIBS)

clean: 
	rm -f *.o $(PROGRAM)
