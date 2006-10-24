#include <WINGs/WINGs.h>

#define   APP_LONG    "minimalistic music player"
#define   APP_SHORT   "mmp"
#define   APP_VERSION   "v0.3"

typedef struct Frontend Frontend;
typedef struct Backend Backend;

Frontend* feCreate();
Bool feInit(Frontend*);
void feAddBackend(Frontend*, Backend*);
void feRemoveBackend(Frontend*, Backend*);
void feShowDir(Frontend*, char*);
void feSetArtist(Frontend*, char*);
void feSetSongName(Frontend*, char*);
void fePlayingStopped(Frontend*);
//   v-- needed?
void feSetSongLength(Frontend*, unsigned int);
void feSetCurrentPosition(Frontend*, float, unsigned int, unsigned int);

Backend* beCreate();
Bool beInit(Backend*);
void beAddFrontend(Backend*, Frontend*);
void beRemoveFrontend(Backend*, Frontend*);
void bePlay(Backend*, char*);
void beStop(Backend*);
WMArray* beGetSupportedExtensions(Backend*);
