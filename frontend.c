#include "mmp.h"

#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "pixmaps/appicon.xpm"
#include "pixmaps/appicon-playing.xpm"
#include "pixmaps/appicon-small.xpm"
#include "pixmaps/appicon-smallplaying.xpm"

#include "pixmaps/play.xpm"
#include "pixmaps/prev.xpm"
#include "pixmaps/next.xpm"
#include "pixmaps/stop.xpm"
/* #include "pixmaps/quit.xpm" */
#include "pixmaps/up.xpm"
#include "pixmaps/down.xpm"
#include "pixmaps/dnd.xpm"

#define WinHeightIfSmall  70
#define DARKEN 100
#define LIGHTEN 100


#define ucfree(x) if(x) { wfree(x); x = NULL; }

// callbacks
void cbChangeSize(WMWidget*, void*);
void cbClick(WMWidget*, void*);
void cbClientMessage(XEvent*, void*);
void cbDoubleClick(WMWidget*, void*);
void cbFocusChanged(XEvent*, void*);
void cbLeftWindow(XEvent*, void*);
void cbKeyPress(XEvent*, void*);
void cbNextSong(WMWidget*, void*);
void cbPlaySong(WMWidget*, void*);
void cbPrevSong(WMWidget*, void*);
void cbSizeChanged(void*, WMNotification*);
void cbStopPlaying(WMWidget*, void*);
void cbPointerMotion(XEvent*, void*);
void cbSizeChanged(void*, WMNotification*);
void cbWindowClosed(WMWidget*, void*);
void cbQuit(WMWidget*, void*);

typedef enum ItemFlags {
  IsFile = 1,
  IsDirectory = 2,
  IsLink = 4,
  IsBrokenLink = 8,
  IsUnsupported = 16
} ItemFlags;

typedef struct Frontend {
  WMArray *backends;
  // ---
  Display *dpy;
  WMWindow *win;
  WMScreen *scr;
  //WMLabel *songtitlelabel,
  WMLabel *songtitle,
          *songartist,
          *songtime,
          *dirlabel;
  WMLabel *progressLabel;
  WMList *datalist;

  WMButton *sizebutton,
           *playsongbutton,
           *prevsongbutton,
           *nextsongbutton,
           *stopsongbutton,
           *dirupbutton;
  //         *quitbutton;

  WMLabel *coverImage;
  WMColor *colorWindowBack, *colorListBack, *colorSelectionBack,
          *colorSelectionFore, *colorPlayed;
  char *currentdir;
  int bigsize, largestnumber, VisibleRecord, ListHeight,
    WinHeightIfBig, fieldsChanged;
  WMMaskedEvents *mask;

  WMListItem *playingSongItem;
  char *playingSongDir, *playingSongFile;
  float currentRatio;
  
  WMUserDefaults* settings;

  Bool playing;
  Bool rewind;
  Bool showUnsupportedFiles;
  Bool haveCover;
  /*WMColor *globalBackgroundColor;*/
  long secondsPassed;
  long totalLength;
  Backend *activeBackend;
} myFrontend;

// other stuff
void setDirLabel(myFrontend*, const char*);
Backend* GetBackendSupportingFile(myFrontend*, const char*);
int CompareListItems(const void*, const void*);
void DrawListItem(WMList*, int, Drawable, char*, int, WMRect*);
WMArray* DropDataTypes(WMView*);
WMDragOperationType WantedDropOperation(WMView*);
Bool AcceptDropOperation(WMView*, WMDragOperationType);
void BeganDrag(WMView*, WMPoint*);
void EndedDrag(WMView*, WMPoint*, Bool);
WMData* FetchDragData(WMView*, char*);

void feFindCover(myFrontend*);

// -----------------------------------------------------------------------------

Frontend* feCreate() {
  myFrontend *f = wmalloc(sizeof(myFrontend));
  f->backends = WMCreateArray(0);
  f->currentdir = NULL;
  f->currentRatio = 0.0f;
  f->playingSongDir = NULL;
  f->playingSongFile = NULL;
  f->playingSongItem = NULL;
  f->playing = False;
  f->rewind = False;
  f->haveCover = False;
  f->settings = NULL;
  f->WinHeightIfBig = 0;
  f->showUnsupportedFiles = False;
  f->secondsPassed = -1;
  f->totalLength = -1;
  f->colorSelectionFore = NULL;
  f->colorSelectionBack = NULL;
  f->colorListBack = NULL;
  f->colorWindowBack = NULL;
  f->colorPlayed = NULL;
  return f;
}

void feAddBackend(myFrontend *f, Backend *b) {
  D3("feAddBackend: f=0x%x, b=0x%x\n", f, b);
  if (WMFindInArray(f->backends, NULL, b) == WANotFound) {
    WMAddToArray(f->backends, b);
  }
}

void feRemoveBackend(myFrontend *f, Backend *b) {
  D3("feRemoveBackend: f=0x%x, b=0x%x\n", f, b);
  int index;
  if ((index = WMFindInArray(f->backends, NULL, b)) != WANotFound) {
    WMDeleteFromArray(f->backends, index);
  }
}

void feSetArtist(myFrontend *f, char *text) {
  FB("feSetArtist");
  WMSetLabelText(f->songartist, text);
  FE("feSetArtist");
}

void feSetTitle(myFrontend *f, char *text) {
  FB("feSetSongName");
  WMSetLabelText(f->songtitle, text);
  FE("feSetSongName");
}

//void feSetSongLength(myFrontend*, int sec);

int findFirstPlayableFile(myFrontend *f) {
  WMArrayIterator i;
  WMListItem *item;

  WM_ITERATE_ARRAY(WMGetListItems(f->datalist), item, i) {
    if (item->uflags & IsFile &&
        GetBackendSupportingFile(f, item->text) != NULL) {
      return i;
    }
  }

  return WANotFound;
}

int findPrevPlayableFile(myFrontend *f) {
  WMArrayIterator i;
  WMListItem *item;
  int act;

  act = WMGetListSelectedItemRow(f->datalist);
  WM_ETARETI_ARRAY(WMGetListItems(f->datalist), item, i) {
    if (i < act && item->uflags & IsFile &&
        GetBackendSupportingFile(f, item->text) != NULL) {
      return i;
    }
  }

  return WANotFound;
}

int findNextPlayableFile(myFrontend *f) {
  WMArrayIterator i;
  WMListItem *item;
  int act;

  act = WMGetListSelectedItemRow(f->datalist);
  WM_ITERATE_ARRAY(WMGetListItems(f->datalist), item, i) {
    if (i > act && item->uflags & IsFile &&
        GetBackendSupportingFile(f, item->text) != NULL) {
      return i;
    }
  }

  return WANotFound;
}

void fePlayingStopped(void *data) {
  myFrontend *f = (myFrontend*) data;
  FB("fePlayingStopped");
  Bool goOn = f->playing;
  int next = findNextPlayableFile(f);
  feResetDisplay(f);

  if (!goOn) return;

  WMUnselectAllListItems(f->datalist);
  if (next == WANotFound && f->rewind) {
    next = findFirstPlayableFile(f);
  }

  if (next != WANotFound) {
    WMSelectListItem(f->datalist, next);
    cbPlaySong(NULL, f);
  }
  FE("fePlayingStopped");
}

void updateTimeDisplay(myFrontend *f) {
  //D2("updateTimeDisplay: f=0x%x\n", f);
  char buf[20];

  if (f->secondsPassed >= 0 && f->totalLength >= 0) {
    snprintf(buf, sizeof(buf), "%.2u:%.2u / %.2u:%.2u", f->secondsPassed/60, f->secondsPassed%60,
                                                        f->totalLength/60, f->totalLength%60);
    f->currentRatio = (float)f->secondsPassed/f->totalLength;
  } else {
    snprintf(buf, sizeof(buf), "?");
    f->currentRatio = 0.0;
  }

  if (!WMGetLabelText(f->songtime) || strncmp(WMGetLabelText(f->songtime), buf, sizeof(buf))) {
    WMSetLabelText(f->songtime, buf);
    if (f->playingSongItem)
      // but i don't want to redisplay the whole widget :(
      WMRedisplayWidget(f->datalist);
  }
}

void feSetFileLength(myFrontend *f, long length) {
  //D3("feSetFileLength: f=0x%x, length=%i\n", f, length);
  f->totalLength = length;
  updateTimeDisplay(f);
}
void feSetCurrentPosition(myFrontend *f, long passed) {
  //D3("feSetCurrentPosition: f=0x%x, passed=%i\n", f, passed);
  f->secondsPassed = passed;
  updateTimeDisplay(f);
}

void loadConfig(myFrontend *f) {
  FB("loadConfig");
  if (f->settings == NULL) {
    char conf[MAXPATHLEN];
    snprintf(conf, sizeof(conf), "%s/.mmprc", getenv("HOME"));
    f->settings = WMGetDefaultsFromPath(conf);
  }
  FE("loadConfig");
}

void saveConfig(myFrontend *f) {
  FB("saveConfig");
  WMSetUDIntegerForKey(f->settings,
                       WMWidgetWidth(f->win), "windowWidth");
  WMSetUDIntegerForKey(f->settings,
                       WMWidgetHeight(f->win), "windowHeight");
  WMPoint p = WMGetViewScreenPosition(WMWidgetView(f->win));
  WMSetUDIntegerForKey(f->settings, p.x, "windowPosX");
  WMSetUDIntegerForKey(f->settings, p.y, "windowPosY");
  WMSetUDStringForKey(f->settings, f->currentdir, "currentPath");
  WMSetUDBoolForKey(f->settings, f->rewind, "repeatMode");
  WMSetUDBoolForKey(f->settings, f->showUnsupportedFiles, "showUnsupportedFiles");
  WMSaveUserDefaults(f->settings);
  FE("saveConfig");
}

int getColorStringRed(const char *str) {
  if (!str || strlen(str) != 7) return -1;
  char *nptr; 
  long color = strtol(str+1, &nptr, 16);
  if (*nptr != '\0') return -1;
  return color >> 16;
}

int getColorStringGreen(const char *str) {
  if (!str || strlen(str) != 7) return -1;
  char *nptr; 
  long color = strtol(str+1, &nptr, 16);
  if (*nptr != '\0') return -1;
  return (color >> 8) & 0xff;
}

int getColorStringBlue(const char *str) {
  if (!str || strlen(str) != 7) return -1;
  char *nptr; 
  long color = strtol(str+1, &nptr, 16);
  if (*nptr != '\0') return -1;
  return color & 0xff;
}

Bool feInit(myFrontend *f) {
  _Xdebug=1;
  f->dpy = XOpenDisplay(NULL);
  //scr = WMCreateSimpleApplicationScreen(dpy);
  //scr = WMOpenScreen(NULL);
  // the only way to get an appicon seems to be:
  f->scr = WMCreateScreen(f->dpy, DefaultScreen(f->dpy));
  loadConfig(f);

    
  f->rewind = WMGetUDBoolForKey(f->settings, "repeatMode");
  f->showUnsupportedFiles = WMGetUDBoolForKey(f->settings, "showUnsupportedFiles");

  f->colorSelectionFore = WMGetUDColorForKey(f->settings, "colorSelectionForground", f->scr);
  if (!f->colorSelectionFore) f->colorSelectionFore = WMBlackColor(f->scr);
  f->colorSelectionBack = WMGetUDColorForKey(f->settings, "colorSelectionBackground", f->scr);
  if (!f->colorSelectionBack) f->colorSelectionBack = WMWhiteColor(f->scr);
  f->colorListBack = WMGetUDColorForKey(f->settings, "colorListBackground", f->scr);
  if (!f->colorListBack) f->colorListBack = WMGrayColor(f->scr);
  f->colorPlayed = WMGetUDColorForKey(f->settings, "colorPlayed", f->scr);
  if (!f->colorPlayed) f->colorPlayed = WMCreateRGBColor(f->scr, 240<<8, 220<<8, 220<<8, False);
  f->colorWindowBack = WMGetUDColorForKey(f->settings, "colorWindowBackground", f->scr);
  if (!f->colorWindowBack) f->colorWindowBack = WMGrayColor(f->scr);

  char *str;
  if ((str = WMGetUDStringForKey(f->settings, "colorBaseBackground")) && getColorStringRed(str) > -1) {
    int r = getColorStringRed(str);
    int g = getColorStringGreen(str);
    int b = getColorStringBlue(str);
    WMSetBlackColor(f->scr, WMCreateRGBColor(f->scr, (r-2*DARKEN > 0 ? r-2*DARKEN : 0) << 8,
                                                     (g-2*DARKEN > 0 ? g-2*DARKEN : 0) << 8,
                                                     (b-2*DARKEN > 0 ? b-2*DARKEN : 0) << 8, False));
    WMSetWhiteColor(f->scr, WMCreateRGBColor(f->scr, (r+LIGHTEN < 255 ? r+LIGHTEN : 255) << 8,
                                                     (g+LIGHTEN < 255 ? g+LIGHTEN : 255) << 8,
                                                     (b+LIGHTEN < 255 ? b+LIGHTEN : 255) << 8, False));
    WMSetDarkGrayColor(f->scr, WMCreateRGBColor(f->scr, (r-DARKEN > 0 ? r-DARKEN : 0) << 8,
                                                        (g-DARKEN > 0 ? g-DARKEN : 0) << 8,
                                                        (b-DARKEN > 0 ? b-DARKEN : 0) << 8, False));
    WMSetGrayColor(f->scr, WMCreateRGBColor(f->scr, r<<8, g<<8, b<<8, False));
  }
  WMSetApplicationHasAppIcon(f->scr, True);
  WMSetApplicationIconPixmap(f->scr, WMCreatePixmapFromXPMData(f->scr, appicon_xpm));

  f->win = WMCreateWindow(f->scr, APP_SHORT);
  WMSetWindowTitle(f->win, APP_LONG);
  WMSetWindowCloseAction(f->win, cbQuit, f);
  //WMResizeWidget(f->win, 280, 400);
  WMSetWindowMinSize(f->win, 250, WinHeightIfSmall);
  //WMSetWindowMaxSize(f->win, 280, 2000);
  WMSetWindowMiniwindowPixmap(f->win, WMCreatePixmapFromXPMData(f->scr, appicon_xpm));
  WMSetWindowMiniwindowTitle(f->win, APP_SHORT);
  int w = WMGetUDIntegerForKey(f->settings, "windowWidth");
  if (w == 0) w = 280;
  int h = WMGetUDIntegerForKey(f->settings, "windowHeight");
  if (h == 0) h = 400;
  WMResizeWidget(f->win, w, h);
  
  WMSetWindowInitialPosition(f->win,
    WMGetUDIntegerForKey(f->settings, "windowPosX"),
    WMGetUDIntegerForKey(f->settings, "windowPosY"));

  /*f->songtitlelabel = WMCreateLabel(f->win);
  WMSetLabelText(f->songtitlelabel, "currently playing:");
  WMSetLabelTextColor(f->songtitlelabel, WMDarkGrayColor(f->scr));*/

  f->coverImage = WMCreateLabel(f->win);
  WMSetLabelImagePosition(f->coverImage, WACenter);
  //WMSetWidgetBackgroundColor(f->coverImage, WMCreateRGBColor(f->scr, 255<<8, 0, 0, False));

  f->songtitle = WMCreateLabel(f->win);
  //WMSetLabelText(f->songtitle, "no file loaded.");
  WMSetLabelTextAlignment(f->songtitle, WARight);
  WMSetLabelTextColor(f->songtitle, WMCreateRGBColor(f->scr, 128<<8, 0, 0, False));
  WMSetLabelFont(f->songtitle, WMSystemFontOfSize(f->scr, 18));

  f->songartist = WMCreateLabel(f->win);
  //WMSetLabelText(f->songartist, APP_LONG);
  WMSetLabelTextAlignment(f->songartist, WARight);
  /*WMSetWidgetBackgroundColor(songartist, WMCreateRGBColor(scr, 222<<8, 0, 0, False));*/
  WMSetLabelTextColor(f->songartist, WMCreateRGBColor(f->scr, 64<<8, 0, 0, False));

  f->songtime = WMCreateLabel(f->win);
  WMSetLabelText(f->songtime, NULL);
  WMSetLabelTextAlignment(f->songtime, WARight);
  WMSetLabelTextColor(f->songtime, WMDarkGrayColor(f->scr));

  /*f->statuslabel = WMCreateLabel(f->win);
  WMSetLabelTextColor(f->statuslabel, WMDarkGrayColor(f->scr));
  WMSetLabelFont(f->statuslabel, WMSystemFontOfSize(f->scr, 10));*/

  f->prevsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->prevsongbutton, WMCreatePixmapFromXPMData(f->scr, prev_xpm));
  WMSetButtonImagePosition(f->prevsongbutton, WIPImageOnly);
  WMSetButtonAction(f->prevsongbutton, cbPrevSong, f);
  //WMSetBalloonTextForView("play the previous song.", WMWidgetView(f->prevsongbutton));
  WMSetButtonBordered(f->prevsongbutton, False);

  f->stopsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->stopsongbutton, WMCreatePixmapFromXPMData(f->scr, stop_xpm));
  WMSetButtonImagePosition(f->stopsongbutton, WIPImageOnly);
  WMSetButtonAction(f->stopsongbutton, cbStopPlaying, f);
  //WMSetBalloonTextForView("stops playback.", WMWidgetView(f->stopsongbutton));
  WMSetButtonBordered(f->stopsongbutton, False);

  f->playsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->playsongbutton, WMCreatePixmapFromXPMData(f->scr, play_xpm));
  WMSetButtonImagePosition(f->playsongbutton, WIPImageOnly);
  WMSetButtonAction(f->playsongbutton, cbPlaySong, f);
  //WMSetBalloonTextForView("play the selected song.", WMWidgetView(f->playsongbutton));
  WMSetButtonBordered(f->playsongbutton, False);

  f->nextsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->nextsongbutton, WMCreatePixmapFromXPMData(f->scr, next_xpm));
  WMSetButtonImagePosition(f->nextsongbutton, WIPImageOnly);
  //WMSetBalloonTextForView("play the next song.", WMWidgetView(f->nextsongbutton));
  WMSetButtonAction(f->nextsongbutton, cbNextSong, f);
  WMSetButtonBordered(f->nextsongbutton, False);

  /*f->quitbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonText(f->quitbutton, "quit");
  WMSetButtonBordered(f->quitbutton, False);
  WMSetButtonAction(f->quitbutton, cbQuit, f);
  //WMSetBalloonTextForView("quit the program.", WMWidgetView(f->quitbutton));
  WMSetButtonFont(f->quitbutton, WMSystemFontOfSize(f->scr, 10));*/

  f->sizebutton = WMCreateCustomButton(f->win, WBBPushInMask);
  WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, down_xpm));
  WMSetButtonImagePosition(f->sizebutton, WIPImageOnly);
  WMSetButtonBordered(f->sizebutton, False);
  WMSetButtonAction(f->sizebutton, cbChangeSize, f);
  //WMSetBalloonTextForView("show/hide the song list.", WMWidgetView(f->sizebutton));
  
  f->dirlabel = WMCreateLabel(f->win);
  WMSetLabelText(f->dirlabel, NULL);
  WMSetLabelTextAlignment(f->dirlabel, WALeft);
  WMSetLabelTextColor(f->dirlabel, WMDarkGrayColor(f->scr));

  f->datalist = WMCreateList(f->win);
  WMHangData(f->datalist, (void*)f);
  WMSetListAllowMultipleSelection(f->datalist, 0);
  WMSetListAllowEmptySelection(f->datalist, 0);
  WMSetListDoubleAction(f->datalist, cbDoubleClick, f);
  WMSetListUserDrawProc(f->datalist, DrawListItem);
  
  WMSetWidgetBackgroundColor(f->datalist, f->colorListBack);
  WMSetWidgetBackgroundColor(f->dirlabel, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->nextsongbutton, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->playsongbutton, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->prevsongbutton, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->sizebutton, f->colorWindowBack);
  //WMSetWidgetBackgroundColor(f->songtitlelabel, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->songtitle, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->songartist, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->songtime, f->colorWindowBack);
  //WMSetWidgetBackgroundColor(f->statuslabel, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->stopsongbutton, f->colorWindowBack);
  //WMSetWidgetBackgroundColor(f->quitbutton, f->colorWindowBack);
  WMSetWidgetBackgroundColor(f->win, f->colorWindowBack);

  //WMEnableUDPeriodicSynchronization(f->settings, True);
  
  /* layout */
  feResetDisplay(f);
  cbSizeChanged(f, NULL);

  /* mask mouse events while dragging */
  f->mask = WMMaskEvents(WMWidgetView(f->datalist));

  WMDragSourceProcs procs;
  procs.dropDataTypes = DropDataTypes;
  procs.wantedDropOperation = WantedDropOperation;
  procs.askedOperations = NULL;
  procs.acceptDropOperation = AcceptDropOperation;
  procs.beganDrag = BeganDrag;
  procs.endedDrag = EndedDrag;
  procs.fetchDragData = FetchDragData;
  WMSetViewDraggable(WMWidgetView(f->datalist), &procs,
                     WMCreatePixmapFromXPMData(f->scr, dnd_xpm));

  WMSetViewNotifySizeChanges(WMWidgetView(f->win), True);
  WMAddNotificationObserver(cbSizeChanged, f,
                            WMViewSizeDidChangeNotification, WMWidgetView(f->win));

  WMRealizeWidget(f->win);
  WMMapSubwidgets(f->win);
  WMMapWidget(f->win);

}

void feRun(myFrontend *f) {
  WMScreenMainLoop(f->scr);
  WMFreeMaskedEvents(f->mask);
}

Bool ExtensionIs(const char *filename, const char *ext) {
  return (strncasecmp(filename + strlen(filename) - strlen(ext),
                      ext, strlen(ext)) == 0);
}

Backend* GetBackendSupportingFile(myFrontend *f, const char *filename) {
  Backend *b;
  char *ext, *a;
  WMArrayIterator i, j;

  if (!f->backends) return NULL;

  // TODO: what to do with files w/o extension?
  if ((ext = rindex(filename, '.')) && strlen(++ext) > 0) {
    WM_ITERATE_ARRAY(f->backends, b, i) {
      WM_ITERATE_ARRAY((*b->getSupportedExtensions)(), a, j) {
        if (strcasecmp(a, ext) == 0) return b;
      }
    }
  }

  return NULL;
}

void feShowDir(myFrontend *f, char *dirname) {
  DIR *dirptr;
  struct dirent *entry;
  char realdirname[MAXPATHLEN];
  char buf[MAXPATHLEN];
  WMListItem *item = NULL;

  WMClearList(f->datalist);
  f->playingSongItem = NULL;

  if (dirname == NULL) {
    dirname = WMGetUDStringForKey(f->settings, "currentPath");
    if (dirname == NULL) dirname = ".";    
  }

  if (!realpath(dirname, realdirname)) {
    fprintf(stderr, "unable to expand %s\n", dirname);
    return;
  }

  //WMSetLabelText(f->dirlabel, rindex(realdirname, '/')+1);

  dirptr = opendir(realdirname);
  if (dirptr) {
    while ((entry = readdir(dirptr))) {
      // skip hidden entries and .. and .
      if (strlen(entry->d_name) < 1 ||
          entry->d_name[0] == '.') continue;

      struct stat s;
      int flags = 0;
      snprintf(buf, sizeof(buf), "%s/%s", realdirname, entry->d_name);
      if (lstat(buf, &s) != 0) continue;
      if (S_ISDIR(s.st_mode)) { //entry->d_type == DT_DIR) {
        flags = IsDirectory;
      } else if (S_ISLNK(s.st_mode)) { //entry->d_type == DT_LNK) {
          flags = IsLink;
          if (stat(buf, &s) == 0) {
            flags |= ((S_ISDIR(s.st_mode)) ? IsDirectory : IsFile);
          } else {
            flags |= IsBrokenLink;
          }
      } else if (S_ISREG(s.st_mode)) { /*if (entry->d_type == DT_REG) {*/
        flags = IsFile;
      } else {
        flags = IsBrokenLink;
      }
      
      if (flags & IsFile && !GetBackendSupportingFile(f, entry->d_name)) {
      	if (f->showUnsupportedFiles) {
          flags |= IsUnsupported;
      	} else {
          continue;
      	}
      }
      item = WMAddListItem(f->datalist, entry->d_name);
      item->uflags = flags;
      if (f->playingSongFile && f->playingSongDir &&
          !strcmp(f->playingSongFile, entry->d_name) &&
          !strcmp(f->playingSongDir, realdirname)) {
        f->playingSongItem = item;
      }
    }
    closedir(dirptr);
  }

  ucfree(f->currentdir);
  f->currentdir = wstrdup(realdirname);
  setDirLabel(f, f->currentdir);

  if (strcmp(realdirname, "/") != 0) {
    item = WMAddListItem(f->datalist, "..");
    item->uflags = IsDirectory;
  }
  
  WMSortListItemsWithComparer(f->datalist, CompareListItems);
}

void cbDoubleClick(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  char buf[MAXPATHLEN];

  /* selected entry is a dir? */
  if (WMGetListSelectedItem(f->datalist)->uflags & IsDirectory) {
    snprintf(buf, sizeof(buf), "%s/%s", f->currentdir,
             WMGetListSelectedItem(f->datalist)->text);
    feShowDir(f, buf);
    return;
  } else {
    cbPlaySong(NULL, f);
  }
}

void cbPlaySong(WMWidget *self, void *data) {
  D2("cbPlaySong: f=0x%x\n", data);
  myFrontend *f = (myFrontend*) data;
  Backend *b;
  char buf[MAXPATHLEN];

  /* selected entry not a file? */
  if (WMGetListSelectedItem(f->datalist) == NULL ||
      WMGetListSelectedItem(f->datalist)->uflags & IsDirectory ||
      WMGetListSelectedItem(f->datalist)->uflags & IsBrokenLink) {
    return;
  }

  if ((b = GetBackendSupportingFile(f, WMGetListSelectedItem(f->datalist)->text)) == NULL) {
    printf("no suitable backend found for file: %s\n", WMGetListSelectedItem(f->datalist)->text);
    return;
  }

  if (f->activeBackend) {
    (*f->activeBackend->stopNow)();
  }

  /* in case we have no id3-tag, set song name to filename minus .mp3 */
  strncpy(buf, WMGetListSelectedItem(f->datalist)->text, sizeof(buf)-1);
  buf[sizeof(buf)-1] = '\0';
  char *dot = rindex(buf, '.');
  if (dot) *dot = '\0';
  WMSetLabelText(f->songtitle, buf);
  WMSetLabelText(f->songartist, NULL);

  snprintf(buf, sizeof(buf), "%s/%s", f->currentdir,
           WMGetListSelectedItem(f->datalist)->text);
  (*b->play)(buf);

  ucfree(f->playingSongDir);
  f->playingSongDir = wstrdup(f->currentdir);
  ucfree(f->playingSongFile);
  f->playingSongFile = wstrdup(WMGetListSelectedItem(f->datalist)->text);
  f->playingSongItem = WMGetListSelectedItem(f->datalist);
  f->playing = True;
  f->activeBackend = b;
  f->currentRatio = 0.0f;

  feFindCover(f);
}

void feFindCover(myFrontend *f) {
  char buf[MAXPATHLEN];
  char *testnames[] = {
    "cover.jpg",
    "COVER.JPG",
    "cover.png",
    "COVER.PNG",
    NULL
  };

  f->haveCover = False;
  cbSizeChanged(f, NULL);

  strncpy(buf, f->playingSongDir, MAXPATHLEN);
  buf[sizeof(buf)-1] = '\0';
  if (strlen(buf) >= sizeof(buf) - 11) return;

  char *startpos = buf + strlen(buf);
  *startpos = '/';
  startpos++;

  RImage *orig = NULL;
  int i;
  for (i = 0; testnames[i] != NULL; i++) {
    strncpy(startpos, testnames[i], 10);
    if ((orig = RLoadImage(WMScreenRContext(f->scr), buf, 0)) != NULL)
      break;
  }
  if (orig == NULL) return;
  D2("pic found: %s\n", buf);
  RImage *scaled = RSmoothScaleImage(orig, 50, 50);
  // TODO: treshold?
  WMPixmap *pix = WMCreatePixmapFromRImage(f->scr, scaled, 127);
  WMSetLabelImage(f->coverImage, pix);
  f->haveCover = True;
  WMReleasePixmap(pix);
  RReleaseImage(scaled);
  RReleaseImage(orig);
  cbSizeChanged(f, NULL);

  return;
}

void feResetDisplay(myFrontend *f) {
  WMSetLabelText(f->songartist, NULL);
  WMSetLabelText(f->songtitle, "no file loaded.");
  WMSetLabelText(f->songtime, NULL);

  ucfree(f->playingSongDir);
  ucfree(f->playingSongFile);
  f->playingSongItem = NULL;
  f->playing = False;
  f->secondsPassed = -1;
  f->totalLength = -1;
  f->currentRatio = 0.0;
  f->activeBackend = NULL;
  if (f->haveCover) {
    f->haveCover = False;
    cbSizeChanged(f, NULL);
  }
  WMRedisplayWidget(f->datalist);
}

void cbStopPlaying(WMWidget *self, void *data) {
  FB("cbStopPlaying");
  myFrontend *f = (myFrontend*) data;
  Backend *b;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(f->backends, b, i) {
    (*b->stopNow)();
  }

  feResetDisplay(f);
  FE("cbStopPlaying");
}

void feMarkFile(myFrontend *f, char *name) {
  WMUnselectAllListItems(f->datalist);
  WMArrayIterator i;
  WMListItem *item;
  int row = WANotFound;
  WM_ITERATE_ARRAY (WMGetListItems(f->datalist), item, i) {
    if (strncmp(name, item->text, strlen(name)) == 0) {
      row = (int)i;
      break;
    }
  }
  if (row != WANotFound) WMSelectListItem(f->datalist, row);
}

void cbNextSong(WMWidget *self, void *data) {
  FB("cbNextSong");
  myFrontend *f = (myFrontend*) data;

  if (f->activeBackend) (*f->activeBackend->stopNow)();
  //beStopNow(f->activeBackend);
  feResetDisplay(f);
  int next = findNextPlayableFile(f);

  if (next != WANotFound) {
    WMUnselectAllListItems(f->datalist);
    WMSelectListItem(f->datalist, next);
    cbPlaySong(NULL, f);
  }
  FE("cbNextSong");
}

void cbPrevSong(WMWidget *self, void *data) {
  FB("cbPrevSong");
  myFrontend *f = (myFrontend*) data;

  if (f->activeBackend) (*f->activeBackend->stopNow)();
  feResetDisplay(f);
  int next = findPrevPlayableFile(f);

  if (next != WANotFound) {
    WMUnselectAllListItems(f->datalist);
    WMSelectListItem(f->datalist, next);
    cbPlaySong(NULL, f);
  }
  FE("cbPrevSong");
}

void cbChangeSize(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  if (f->bigsize) {
    f->WinHeightIfBig = WMWidgetHeight(f->win);
    WMResizeWidget(f->win, WMWidgetWidth(f->win), WinHeightIfSmall);
  } else {
  	if (f->WinHeightIfBig <= WinHeightIfSmall + 30) {
  	  f->WinHeightIfBig = 500;
  	}
    WMResizeWidget(f->win, WMWidgetWidth(f->win), f->WinHeightIfBig);
  }
}

void cbSizeChanged(void *self, WMNotification *notif) {
  myFrontend *f = (myFrontend*)self;
  int w = WMWidgetWidth(f->win);
  int h = WMWidgetHeight(f->win);
  
  if (h <= WinHeightIfSmall + 30) {
    // collapse, if too small
    if (h != WinHeightIfSmall) {
      WMResizeWidget(f->win, w, WinHeightIfSmall);
    }
    WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, down_xpm));
    f->bigsize = 0;
  } else {
    WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, up_xpm));
    /*WMResizeWidget(f->datalist, WMWidgetWidth(f->datalist),
                   WMWidgetHeight(f->win) - WinHeightIfSmall + 2);*/
    f->ListHeight = WMWidgetHeight(f->datalist) / WMGetListItemHeight(f->datalist);
    f->WinHeightIfBig = h;
    if (WMGetListSelectedItemRow(f->datalist) >= WMGetListPosition(f->datalist) + f->ListHeight) {
      WMSetListPosition(f->datalist, WMGetListSelectedItemRow(f->datalist) - f->ListHeight + 1);
    }

    f->bigsize = 1;
  }
  
  if (f->haveCover) {
    WMResizeWidget(f->songartist, w - 66, 15);
    WMMoveWidget(f->songartist, 60, 7);
    WMResizeWidget(f->songtitle, w - 66, 20);
    WMMoveWidget(f->songtitle, 60, 22);
    WMResizeWidget(f->coverImage, 50, 50);
    WMMoveWidget(f->coverImage, 7, 7);
    WMResizeWidget(f->songtime, w - 148, 14);
    WMMoveWidget(f->songtime, 60, 49);
  } else {
    WMResizeWidget(f->songartist, w - 11, 15);
    WMMoveWidget(f->songartist, 5, 7);
    WMResizeWidget(f->songtitle, w - 11, 20);
    WMMoveWidget(f->songtitle, 5, 22);
    WMResizeWidget(f->coverImage, 1, 1);
    WMMoveWidget(f->coverImage, w+1, h+1);
    WMResizeWidget(f->songtime, w - 93, 14);
    WMMoveWidget(f->songtime, 5, 49);
  }

  WMResizeWidget(f->prevsongbutton, 20, 20);
  WMMoveWidget(f->prevsongbutton, w - 86, 44);
  WMResizeWidget(f->stopsongbutton, 20, 20);
  WMMoveWidget(f->stopsongbutton, w - 66, 44);
  WMResizeWidget(f->playsongbutton, 20, 20);
  WMMoveWidget(f->playsongbutton, w - 46, 44);
  WMResizeWidget(f->nextsongbutton, 20, 20);
  WMMoveWidget(f->nextsongbutton, w - 26, 44);
  
  WMResizeWidget(f->sizebutton, 30, 14);
  WMMoveWidget(f->sizebutton, 7, 57);

  WMMoveWidget(f->dirlabel, 7, WinHeightIfSmall+1);
  WMMoveWidget(f->datalist, 7, WinHeightIfSmall+15);
  if (f->bigsize) {
    WMResizeWidget(f->datalist, w - 14, h - WinHeightIfSmall-15);
    WMResizeWidget(f->dirlabel, w - 14, 14);
    setDirLabel(f, f->currentdir);
  }
}

void setDirLabel(myFrontend *f, const char *text) {
  char *dirp;
  Bool clipped = False;
  char buf[MAXPATHLEN];

  if (!f || !text) return;

  strncpy(buf, text, sizeof(buf)-1);
  buf[sizeof(buf)-1] = '\0';

  for (dirp = buf; dirp != '\0'; dirp++) {
    if (WMWidthOfString(WMDefaultSystemFont(f->scr), dirp, strlen(dirp)) < WMWidgetWidth(f->dirlabel)-3) {
      break;
    }
  }

  if (dirp > buf) {
    if (strlen(dirp) >= 3) {
      strncpy(dirp, "...", 3);
      WMSetLabelText(f->dirlabel, dirp);
    }
  }
  if (!WMGetLabelText(f->dirlabel) || strncmp(WMGetLabelText(f->dirlabel), dirp, sizeof(buf)-(dirp-buf)) != 0)
    WMSetLabelText(f->dirlabel, dirp);
}

void cbQuit(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  cbStopPlaying(NULL, f);
  
  saveConfig(f);
  exit(0);
}

WMArray* DropDataTypes(WMView *view) {
  WMArray *res = WMCreateArray(3);
  WMReplaceInArray(res, 0, "text/uri-list");
  WMReplaceInArray(res, 1, "text/x-dnd-username");
  WMReplaceInArray(res, 2, "text/plain");
  return res;
}

WMDragOperationType WantedDropOperation(WMView *view) {
  return WDOperationCopy;
}

Bool AcceptDropOperation(WMView *view, WMDragOperationType type) {
  printf("accepting operation.\n");
  return True;
}

void BeganDrag(WMView *view, WMPoint *point) {
  printf("began drag...\n");
}

void EndedDrag(WMView *view, WMPoint *point, Bool deposited) {
  printf("ended drag...%s\n", (deposited == True? " (deposited)" : ""));
}

WMData* FetchDragData(WMView *view, char *type) {
  char buf[1024];
  char hostname[256];
  char *filename = NULL;
  myFrontend *f = (myFrontend*) WMGetHangedData(WMWidgetOfView(view));
  if (!f) return NULL;
  
  printf("should send out data of type %s\n", type);
  if (strncmp(type, "text/uri-list", 13) == 0) {
    gethostname(hostname, 255);
    hostname[256] = '\0';
    filename = WMGetListSelectedItem(f->datalist)->text;
    if (strncmp(filename, " > ", 3) == 0) filename += 3;
    snprintf(buf, 1024, "file://%s%s/%s",
             hostname, f->currentdir, filename);
    printf("sending: %s\n", buf);
    return WMCreateDataWithBytes((void*)buf, strlen(buf)+1);
  }
  return WMCreateDataWithBytes(NULL, 0);
}

void DrawListItem(WMList *lPtr, int index, Drawable d, char *text,
                  int state, WMRect *rect) {
  myFrontend *f = WMGetHangedData(lPtr);
  assert(f != NULL);
  WMListItem *itemPtr = WMGetListItem(lPtr, index);
  WMView *view = WMWidgetView(lPtr);
  WMScreen *screen = WMWidgetScreen(lPtr);
  Display *dpy = WMScreenDisplay(screen);
  WMColor *back = (itemPtr->selected ?
                   f->colorSelectionBack :
                   f->colorListBack);
  XFillRectangle(dpy,
                 d, WMColorGC(back),
                 0, 0, rect->size.width, rect->size.height);
  if (f->playingSongItem == itemPtr) {
    if (itemPtr->selected) {
      XFillRectangle(dpy, d, WMColorGC(f->colorPlayed), 4,
                     2, (unsigned int)ceil((rect->size.width-6)*f->currentRatio), rect->size.height-4);
    } else {
      XFillRectangle(dpy, d, WMColorGC(f->colorPlayed), 0,
                     0, (unsigned int)ceil((rect->size.width)*f->currentRatio), rect->size.height);
    }
  }

  WMFont *font = NULL;
  if (itemPtr->uflags & IsDirectory) {
    font = WMDefaultBoldSystemFont(screen);
  } else {
    font = WMDefaultSystemFont(screen);
  }
  W_PaintText(view, d,
              font,
              4, 0, rect->size.width, WALeft,
              itemPtr->uflags & IsUnsupported ? WMDarkGrayColor(screen) : WMBlackColor(screen),
              False, text, strlen(text));
  
  if (itemPtr->uflags & IsLink) {
    int tw = WMWidthOfString(font, text, strlen(text));
    if (tw > rect->size.width - 4)
      tw = rect->size.width - 4;
    int ly = itemPtr->uflags & IsBrokenLink ? rect->size.height / 2 : rect->size.height - 2;
    XDrawLine(dpy, d, WMColorGC(WMDarkGrayColor(screen)), 4, ly, 2 + tw, ly);
  }
}

int CompareListItems(const void *item1, const void *item2) {
  int ignoreCase = 1;
  int dirsFirst = 1;
  const WMListItem *iLeft = *(const void**)item1;
  const WMListItem *iRight = *(const void**)item2;

  if (strcmp(iLeft->text, "..") == 0) return -1;
  if (strcmp(iRight->text, "..") == 0) return 1;

  if (dirsFirst) {
    if (iLeft->uflags & IsDirectory) {
      if (iRight->uflags & IsDirectory) {
        return ignoreCase ?
               strcasecmp(iLeft->text, iRight->text) :
               strcmp(iLeft->text, iRight->text);
      } else {
        return -1;
      }
    } else {
      if (iRight->uflags & IsDirectory) {
        return 1;
      } else {
        // both no dir? fall through....
      }
    }
  }

  return ignoreCase ?
         strcasecmp(iLeft->text, iRight->text) :
         strcmp(iLeft->text, iRight->text);
}
