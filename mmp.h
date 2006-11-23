#include <WINGs/WINGs.h>

#define   APP_SHORT     "mmp"
#define   APP_VERSION   "v0.4"
#define   APP_LONG      APP_SHORT" "APP_VERSION

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
void feSetFileLength(Frontend*, long);
void feSetCurrentPosition(Frontend*, long);
void feHandleSigChild(Frontend*);

Backend* beCreate();
Bool beInit(Backend*);
void beAddFrontend(Backend*, Frontend*);
void beRemoveFrontend(Backend*, Frontend*);
void bePlay(Backend*, char*);
void beStop(Backend*);
Bool beIsPlaying(Backend*);
void beHandleSigChild(Backend*);
WMArray* beGetSupportedExtensions(Backend*);
