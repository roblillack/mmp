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
#include <sys/time.h>
#include <time.h>
extern int depth;
extern int depthc;
#define PRINTTIME()	{ struct timeval tv; gettimeofday(&tv, NULL);\
                          fprintf(stderr, "%u.%7u ", tv.tv_sec, tv.tv_usec); }
#define PRINTDEPTH()	PRINTTIME();\
                        for (depthc = depth; depthc > 0; depthc--)\
                          fprintf(stderr, "    ");
#define D1(x)    	PRINTDEPTH(); fprintf(stderr, x"\n"); fflush(stderr)
#define D2(x,y)		PRINTDEPTH(); fprintf(stderr, x"\n", y); fflush(stderr)
#define D3(x,y,z)	PRINTDEPTH(); fprintf(stderr, x"\n", y, z); fflush(stderr)
#define FB(x)           PRINTDEPTH(); fprintf(stderr, x" {\n"); fflush(stderr); fflush(stderr); depth++
#define FE(x)           depth--; PRINTDEPTH(); fprintf(stderr, "} ("x")\n"); fflush(stderr); fflush(stderr)
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
