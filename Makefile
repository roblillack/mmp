CC = gcc
VERSION=0.5
PREFIX=/usr/local
# possible values: mpg123 mplayer
#BACKENDS=mpg123 mplayer
#BACKENDS=mplayer
BACKENDS=mpg123

WINGS_LIBS:=$(shell pkg-config --libs WINGs x11) -lm
WINGS_CFLAGS:=$(shell pkg-config --cflags WINGs x11)
# if pkg-config not installed/doesn't work, set the above manually to something like
# (-lintl, -liconv may not be needed)
#WINGS_CFLAGS:=-I. -I/usr/X11R6/include -I/usr/local/include -I/usr/pkg/include
#WINGS_LIBS:=-L/usr/X11R6/lib -L/usr/local/lib -L/usr/pkg/lib -lWINGs -lXft -lX11 -lwraster -lintl -liconv

# uncomment for debug version
DEBUG=-DDEBUG

# there should be no need to change something below this point.
ICONSIZES:=16 22 32 48 64 128


ifdef DEBUG
  OPT=-g
else
  OPT=-Os
endif
CFLAGS = $(OPT) $(DEBUG) $(addprefix -DHAVE_BACKEND_, $(BACKENDS)) $(WINGS_CFLAGS) -DVERSION=\"$(VERSION)\"

PROGRAM = mmp

OBJECTS = mmp.o WMAddOns.o frontend.o $(addprefix backend_, $(addsuffix .o, $(BACKENDS))) md5.o

.SUFFIXES:	.o .c

# -std=c99
.c.o :
	$(CC) -c -o $@ $(CFLAGS) $<

all:    $(PROGRAM)

register-menuentry: burningsoda-mmp.desktop
	xdg-desktop-menu install $<

unregister-menuentry: burningsoda-mmp.desktop
	xdg-desktop-menu uninstall $<

register-icons: $(addsuffix .png, $(addprefix icon-, $(ICONSIZES)))
	for i in $(ICONSIZES); do\
	  echo $$i;\
	  bash xdg-icon-resource install --size $$i $(PREFIX)/share/mmp/icon-$$i.png burningsoda-mmp;\
	 done

unregister-icons:
	for i in $(ICONSIZES); do\
	  bash xdg-icon-resource uninstall --size $$i burningsoda-mmp;\
	 done

install-stuff: $(PROGRAM)
	install -d $(PREFIX)/bin
	install $(PROGRAM) $(PREFIX)/bin
	install -d $(PREFIX)/share/mmp
	install $(addsuffix .png, $(addprefix icon-, $(ICONSIZES))) $(PREFIX)/share/mmp
	install burningsoda-mmp.desktop $(PREFIX)/share/mmp

uninstall-stuff:
	rm $(PREFIX)/bin/$(PROGRAM)
	rm -rf $(PREFIX)/share/mmp

install: install-stuff register-menuentry register-icons
uninstall: unregister-menuentry unregister-icons uninstall-stuff


$(PROGRAM):	$(OBJECTS)
	$(CC) -o $(PROGRAM) $^ $(WINGS_LIBS)

clean: 
	rm -f *.o $(PROGRAM)
