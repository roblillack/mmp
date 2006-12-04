#ifndef __mmp_h__
#define __mmp_h__
#include <WINGs/WINGs.h>
#include "WMAddOns.h"

#define   APP_SHORT     "mmp"
#define   APP_LONG      APP_SHORT" v"VERSION

//#define DEBUG

#ifdef DEBUG
#include <stdlib.h>
#include <stdio.h>
#define D1(x)    	fprintf(stderr, "* "__FILE__" %i: "x, __LINE__)
#define D2(x,y)		fprintf(stderr, "* "__FILE__" %i: "x, __LINE__, y)
#define D3(x,y,z)	fprintf(stderr, "* "__FILE__" %i: "x, __LINE__, y, z)

static int depth;
static int depthc;
#define FB(x)           for (depthc = ++depth; depthc > 0; depthc--)\
                          fprintf(stderr, (depth%2)?".":".");\
                        fprintf(stderr, "* "x" {\n"); fflush(stderr);
#define FE(x)           for (depthc = depth--; depthc > 0; depthc--)\
                          fprintf(stderr, (depth%2)?".":".");\
                        fprintf(stderr, "* } "x"\n"); fflush(stderr);
#else
#define FB
#define FE
#define D1(x)
#define D2(x,y)
#define D3(x,y,z)
#endif

typedef struct Frontend Frontend;
typedef struct Backend_T Backend;

struct Backend_T {
  WMArray *(*getSupportedExtensions)();
  void (*init)();
  Bool (*isPlaying)();
  void (*play)(const char*);
  void (*stopNow)();
  void (*switchFullscreen)       ();
  void (*pause)                  ();
  void (*seekForward)            ();
  void (*seekBackward)           ();
};

Frontend* feCreate();
Bool feInit(Frontend*);
void feAddBackend(Frontend*, Backend*);
void feRemoveBackend(Frontend*, Backend*);
void feResetDisplay(Frontend*);
void feShowDir(Frontend*, char*);
void feSetArtist(Frontend*, char*);
void feSetTitle(Frontend*, char*);
WMCallback fePlayingStopped;
void feSetFileLength(Frontend*, long);
void feSetCurrentPosition(Frontend*, long);
//void feGotSigChild(Frontend*);
#endif
