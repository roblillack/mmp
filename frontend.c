#include "mmp.h"
#include "WMAddOns.h"

#include <dirent.h>
#include <assert.h>

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

#define   WinHeightIfSmall  100


#define ucfree(x) if(x) { wfree(x); x = NULL; }

// callbacks
void cbChangeSize(WMWidget*, void*);
void cbNextSong(WMWidget*, void*);
void cbPlaySong(WMWidget*, void*);
void cbPrevSong(WMWidget*, void*);
void cbSizeChanged(void*, WMNotification*);
void cbStopPlaying(WMWidget*, void*);
void cbQuit(WMWidget*, void*);

// other stuff
int CompareListItems(const void*, const void*);
void DrawListItem(WMList*, int, Drawable, char*, int, WMRect*);
WMArray* DropDataTypes(WMView*);
WMDragOperationType WantedDropOperation(WMView*);
Bool AcceptDropOperation(WMView*, WMDragOperationType);
void BeganDrag(WMView*, WMPoint*);
void EndedDrag(WMView*, WMPoint*, Bool);
WMData* FetchDragData(WMView*, char*);

typedef enum ItemFlags {
  IsFile = 0,
  IsDirectory = 1,
  IsALink = 2,
  IsPlaying = 4
} ItemFlags;

typedef struct Frontend {
  WMArray *backends;
  // ---
  Display *dpy;
  WMWindow *win;
  WMScreen *scr;
  WMLabel *songtitlelabel,
          *songtitle,
          *songartist,
          *statuslabel,
          *songtime;
  WMList *datalist;

  WMButton *sizebutton,
           *playsongbutton,
           *prevsongbutton,
           *nextsongbutton,
           *stopsongbutton,
           *dirupbutton,
           *quitbutton;
  WMColor *playedColor;
  char *currentdir;
  int bigsize, largestnumber, VisibleRecord, ListHeight,
    WinHeightIfBig, fieldsChanged;
  WMMaskedEvents *mask;

  WMListItem *playingSongItem;
  char *playingSongDir, *playingSongFile;
  float currentRatio;

  Bool playing;
  Bool rewind;
} myFrontend;

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
  return f;
}

void feAddBackend(myFrontend *f, Backend *b) {
  if (WMFindInArray(f->backends, NULL, b) == WANotFound) {
    WMAddToArray(f->backends, b);
    beAddFrontend(b, f);
  }
}

void feRemoveBackend(myFrontend *f, Backend *b) {
  int index;
  if ((index = WMFindInArray(f->backends, NULL, b)) != WANotFound) {
    WMDeleteFromArray(f->backends, index);
    beRemoveFrontend(b, f);
  }
}

void feSetArtist(myFrontend *f, char *text) {
  WMSetLabelText(f->songartist, text);
}

void feSetSongName(myFrontend *f, char *text) {
  WMSetLabelText(f->songtitle, text);
}

//void feSetSongLength(myFrontend*, int sec);

void fePlayingStopped(myFrontend *f) {
  if (f->playing) {
    // song may be over, but we want the next :)
    cbNextSong(NULL, f);
  }
}

void feSetCurrentPosition(myFrontend *f, float r, unsigned int passed, unsigned int total) {
  char buf[20];
  snprintf(buf, 20, "%.2u:%.2u / %.2u:%.2u", passed/60, passed%60, total/60, total%60);

  if (r < 0) {
    f->currentRatio = (float)((double)passed/(double)total);
  } else {
    f->currentRatio = r;
  }

  if (strncmp(WMGetLabelText(f->songtime), buf, 20)) {
    WMSetLabelText(f->songtime, buf);
    if (f->playingSongItem)
      // but i don't want to redisplay the whole widget :(
      WMRedisplayWidget(f->datalist);
  }
}

Bool feInit(myFrontend *f) {
  f->dpy = XOpenDisplay(NULL);
  //scr = WMCreateSimpleApplicationScreen(dpy);
  //scr = WMOpenScreen(NULL);
  // the only way to get an appicon seems to be:
  f->scr = WMCreateScreen(f->dpy, DefaultScreen(f->dpy));

  WMSetApplicationHasAppIcon(f->scr, True);
  WMSetApplicationIconPixmap(f->scr, WMCreatePixmapFromXPMData(f->scr, appicon_xpm));

  f->win = WMCreateWindow(f->scr, APP_SHORT);
  WMSetWindowTitle(f->win, APP_LONG);
  WMSetWindowCloseAction(f->win, cbQuit, f);
  WMSetWindowMinSize(f->win, 280, WinHeightIfSmall);
  WMSetWindowMaxSize(f->win, 280, 2000);
  WMSetWindowMiniwindowPixmap(f->win, WMCreatePixmapFromXPMData(f->scr, appicon_xpm));
  WMSetWindowMiniwindowTitle(f->win, APP_SHORT);

  f->songtitlelabel = WMCreateLabel(f->win);
  WMSetLabelText(f->songtitlelabel, "currently playing:");
  WMSetLabelTextColor(f->songtitlelabel, WMDarkGrayColor(f->scr));
  WMResizeWidget(f->songtitlelabel, 260, 16);
  WMMoveWidget(f->songtitlelabel, 10, 15);

  f->songtitle = WMCreateLabel(f->win);
  WMSetLabelText(f->songtitle, APP_LONG);
  WMSetLabelTextAlignment(f->songtitle, WARight);
  WMSetLabelTextColor(f->songtitle, WMCreateRGBColor(f->scr, 128<<8, 0, 0, False));
  WMResizeWidget(f->songtitle, 260, 20);
  WMMoveWidget(f->songtitle, 10, 40);
  WMSetLabelFont(f->songtitle, WMSystemFontOfSize(f->scr, 18));

  f->songartist = WMCreateLabel(f->win);
  WMSetLabelText(f->songartist, APP_SHORT" "APP_VERSION);
  WMSetLabelTextAlignment(f->songartist, WARight);
  /*WMSetWidgetBackgroundColor(songartist, WMCreateRGBColor(scr, 222<<8, 0, 0, False));*/
  WMSetLabelTextColor(f->songartist, WMCreateRGBColor(f->scr, 64<<8, 0, 0, False));
  WMResizeWidget(f->songartist, 150, 15);
  WMMoveWidget(f->songartist, 120, 25);

  f->songtime = WMCreateLabel(f->win);
  WMSetLabelText(f->songtime, "");
  WMSetLabelTextColor(f->songtime, WMDarkGrayColor(f->scr));
  WMResizeWidget(f->songtime, 100, 16);
  WMMoveWidget(f->songtime, 10, 65);

  f->statuslabel = WMCreateLabel(f->win);
  WMSetLabelTextColor(f->statuslabel, WMDarkGrayColor(f->scr));
  WMSetLabelFont(f->statuslabel, WMSystemFontOfSize(f->scr, 10));
  WMResizeWidget(f->statuslabel, 200, 14);
  WMMoveWidget(f->statuslabel, 7, 242);

  f->prevsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->prevsongbutton, WMCreatePixmapFromXPMData(f->scr, prev_xpm));
  WMSetButtonImagePosition(f->prevsongbutton, WIPImageOnly);
  WMSetButtonAction(f->prevsongbutton, cbPrevSong, f);
  WMSetBalloonTextForView("play the previous song.", WMWidgetView(f->prevsongbutton));
  WMSetButtonBordered(f->prevsongbutton, False);
  WMResizeWidget(f->prevsongbutton, 20, 20);
  WMMoveWidget(f->prevsongbutton, 190, 60);

  f->stopsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->stopsongbutton, WMCreatePixmapFromXPMData(f->scr, stop_xpm));
  WMSetButtonImagePosition(f->stopsongbutton, WIPImageOnly);
  WMSetButtonAction(f->stopsongbutton, cbStopPlaying, f);
  WMSetBalloonTextForView("stops playback.", WMWidgetView(f->stopsongbutton));
  WMSetButtonBordered(f->stopsongbutton, False);
  WMResizeWidget(f->stopsongbutton, 20, 20);
  WMMoveWidget(f->stopsongbutton, 210, 60);

  f->playsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->playsongbutton, WMCreatePixmapFromXPMData(f->scr, play_xpm));
  WMSetButtonImagePosition(f->playsongbutton, WIPImageOnly);
  WMSetButtonAction(f->playsongbutton, cbPlaySong, f);
  WMSetBalloonTextForView("play the selected song.", WMWidgetView(f->playsongbutton));
  WMSetButtonBordered(f->playsongbutton, False);
  WMResizeWidget(f->playsongbutton, 20, 20);
  WMMoveWidget(f->playsongbutton, 230, 60);

  f->nextsongbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonImage(f->nextsongbutton, WMCreatePixmapFromXPMData(f->scr, next_xpm));
  WMSetButtonImagePosition(f->nextsongbutton, WIPImageOnly);
  WMSetBalloonTextForView("play the next song.", WMWidgetView(f->nextsongbutton));
  WMSetButtonAction(f->nextsongbutton, cbNextSong, f);
  WMSetButtonBordered(f->nextsongbutton, False);
  WMResizeWidget(f->nextsongbutton, 20, 20);
  WMMoveWidget(f->nextsongbutton, 250, 60);

  f->quitbutton = WMCreateButton(f->win, WBTMomentaryPush);
  WMSetButtonText(f->quitbutton, "quit");
  WMSetButtonBordered(f->quitbutton, False);
  WMSetButtonAction(f->quitbutton, cbQuit, f);
  WMSetBalloonTextForView("quit the program.", WMWidgetView(f->quitbutton));
  WMSetButtonFont(f->quitbutton, WMSystemFontOfSize(f->scr, 10));
  WMResizeWidget(f->quitbutton, 30, 15);
  WMMoveWidget(f->quitbutton, 240, WinHeightIfSmall-15);

  f->sizebutton = WMCreateCustomButton(f->win, WBBPushInMask);
  WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, down_xpm));
  WMSetButtonImagePosition(f->sizebutton, WIPImageOnly);
  WMSetButtonBordered(f->sizebutton, False);
  WMSetButtonAction(f->sizebutton, cbChangeSize, f);
  WMSetBalloonTextForView("show/hide the song list.", WMWidgetView(f->sizebutton));
  WMResizeWidget(f->sizebutton, 30, 15);
  WMMoveWidget(f->sizebutton, 10, WinHeightIfSmall-15);

  f->datalist = WMCreateList(f->win);
  WMHangData(f->datalist, (void*)f);
  WMSetListAllowMultipleSelection(f->datalist, 0);
  WMSetListAllowEmptySelection(f->datalist, 0);
  WMSetListDoubleAction(f->datalist, cbPlaySong, f);
  WMResizeWidget(f->datalist, 260, 10);
  WMMoveWidget(f->datalist, 10, WinHeightIfSmall+1);
  WMSetListUserDrawProc(f->datalist, DrawListItem);

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

  f->playedColor = WMCreateRGBColor(f->scr, 240<<8, 220<<8, 220<<8, False);
}

void feRun(myFrontend *f) {
  WMScreenMainLoop(f->scr);
  WMFreeMaskedEvents(f->mask);
}

Bool ExtensionIs(const char *filename, const char *ext) {
  return (strncasecmp(filename + strlen(filename) - strlen(ext),
                      ext, strlen(ext)) == 0);
}

Bool ExtensionSupported(myFrontend *f, const char *ext) {
  return True;
}

void feShowDir(myFrontend *f, char *dirname) {
  DIR *dirptr;
  struct dirent *entry;
  char realdirname[PATH_MAX];
  char *buf;
  WMListItem *item = NULL;

  WMClearList(f->datalist);
  f->playingSongItem = NULL;

  if (!realpath(dirname, realdirname)) {
    perror("unable to expand %s\n", dirname);
    return;
  }

  dirptr = opendir(realdirname);
  if (dirptr) {
    while ((entry = readdir(dirptr))) {
      // skip hidden entries and .. and .
      if (strlen(entry->d_name) < 1 ||
          entry->d_name[0] == '.') continue;
      if (ExtensionSupported(f, "") || entry->d_type == DT_DIR) {
        item = WMAddListItem(f->datalist, entry->d_name);
        if (entry->d_type == DT_DIR) {
          item->uflags = IsDirectory;
        } else {
          item->uflags = IsFile;
          if (f->playingSongFile && f->playingSongDir &&
              !strcmp(f->playingSongFile, entry->d_name) &&
              !strcmp(f->playingSongDir, realdirname)) {
            f->playingSongItem = item;
          }
        }
      }
    }
    closedir(dirptr);
  }

  ucfree(f->currentdir);
  f->currentdir = wstrdup(realdirname);

  if (strcmp(realdirname, "/") != 0) {
    item = WMAddListItem(f->datalist, "..");
    item->uflags = IsDirectory;
  }
  
  WMSortListItemsWithComparer(f->datalist, CompareListItems);
}

void cbPlaySong(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  char buf[PATH_MAX];

  /* selected entry is a dir? */
  if (WMGetListSelectedItem(f->datalist)->uflags && IsDirectory) {
    snprintf(buf, PATH_MAX, "%s/%s", f->currentdir,
             WMGetListSelectedItem(f->datalist)->text);
    feShowDir(f, buf);
    return;
  }

  /* in case we have no id3-tag, set song name to filename minus .mp3 */
  strncpy(buf, WMGetListSelectedItem(f->datalist)->text, PATH_MAX);
  buf[strlen(buf)-4]='\0';
  WMSetLabelText(f->songtitle, buf);
  WMSetLabelText(f->songartist, APP_SHORT" "APP_VERSION);

  snprintf(buf, PATH_MAX, "%s/%s", f->currentdir,
           WMGetListSelectedItem(f->datalist)->text);

  ucfree(f->playingSongDir);
  f->playingSongDir = wstrdup(f->currentdir);
  ucfree(f->playingSongFile);
  f->playingSongFile = wstrdup(WMGetListSelectedItem(f->datalist)->text);
  f->playingSongItem = WMGetListSelectedItem(f->datalist);
  f->playing = True;
  f->currentRatio = 0.0f;

  Backend *b;
  char *ext;
  WMArrayIterator i, j;

  WM_ITERATE_ARRAY(f->backends, b, i) {
    // FIXME: extension may not be 3 chars in every case
    WM_ITERATE_ARRAY(beGetSupportedExtensions(b), ext, j) {
      printf(".%s\n",ext);
    }
    if (WMFindInArray(beGetSupportedExtensions(b), NULL, buf + strlen(buf) - 3)) {
      bePlay(b, buf);
      return;
    }
  }
  
  printf("no suitable backend found for: %s\n", buf + strlen(buf) - 3);
}

void cbStopPlaying(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  Backend *b;
  WMArrayIterator i;

  WMSetLabelText(f->songtitle, APP_LONG);
  WMSetLabelText(f->songartist, "no file loaded.");
  WMSetLabelText(f->songtime, "©2001-2006 by Robert Lillack");

  ucfree(f->playingSongDir);
  ucfree(f->playingSongFile);
  f->playingSongItem = NULL;
  f->playing = False;

  WM_ITERATE_ARRAY(f->backends, b, i) {
    beStop(b);
  }
}

void cbNextSong(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  int actnum = WMGetListSelectedItemRow(f->datalist);
  Bool wasPlaying = f->playing;
  Bool endReached = False;

  if (wasPlaying) cbStopPlaying(NULL, f);
  WMUnselectAllListItems(f->datalist);
  actnum++;
  if (actnum > WMGetListNumberOfRows(f->datalist) - 1) {
    actnum = WMGetListNumberOfRows(f->datalist) - 1;
    endReached = True;
  }
  WMSelectListItem(f->datalist, actnum);
  if (wasPlaying && !endReached)
    cbPlaySong(NULL, f);
}

void cbPrevSong(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  int actnum = WMGetListSelectedItemRow(f->datalist);
  Bool wasPlaying = f->playing;

  if (wasPlaying) cbStopPlaying(NULL, f);
  WMUnselectAllListItems(f->datalist);
  actnum--;
  if(actnum < 0) actnum = 0;
  WMSelectListItem(f->datalist, actnum);
  if (wasPlaying)
    cbPlaySong(NULL, f);
}

void cbChangeSize(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  if (f->bigsize) {
    WMResizeWidget(f->win, 350, WinHeightIfSmall);
  } else {
    WMResizeWidget(f->win, 350, f->WinHeightIfBig);
  }
}

void cbSizeChanged(void *self, WMNotification *notif) {
  myFrontend *f = (myFrontend*)self;

  if (WMWidgetHeight(f->win) <= WinHeightIfSmall + 30) {
    if (WMWidgetHeight(f->win) != WinHeightIfSmall) {
      WMResizeWidget(f->win, WMWidgetWidth(f->win), WinHeightIfSmall);
    }
    WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, down_xpm));
    f->bigsize = 0;
  } else {
    WMSetButtonImage(f->sizebutton, WMCreatePixmapFromXPMData(f->scr, up_xpm));
    WMResizeWidget(f->datalist, WMWidgetWidth(f->datalist),
                   WMWidgetHeight(f->win) - WinHeightIfSmall + 2);
    f->ListHeight = WMWidgetHeight(f->datalist) / WMGetListItemHeight(f->datalist);
    f->WinHeightIfBig = WMWidgetHeight(f->win);
    if (WMGetListSelectedItemRow(f->datalist) >= WMGetListPosition(f->datalist) + f->ListHeight) {
      WMSetListPosition(f->datalist, WMGetListSelectedItemRow(f->datalist) - f->ListHeight + 1);
    }
    f->bigsize = 1;
  }
}

void cbQuit(WMWidget *self, void *data) {
  myFrontend *f = (myFrontend*) data;
  cbStopPlaying(NULL, f);
  
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
                   WMWhiteColor(screen) :
                   WMGetWidgetBackgroundColor(lPtr));
  XFillRectangle(dpy,
                 d, WMColorGC(back),
                 0, 0, rect->size.width, rect->size.height);
  if (f->playingSongItem == itemPtr) {
    if (itemPtr->selected) {
      XFillRectangle(dpy, d, WMColorGC(f->playedColor), 4,
                     2, (unsigned int)ceil((rect->size.width-6)*f->currentRatio), rect->size.height-4);
    } else {
      XFillRectangle(dpy, d, WMColorGC(f->playedColor), 0,
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
              WMBlackColor(screen),
              False, text, strlen(text));
}

/*void PrintArray(WMArray* array) {
  WMListItem *bla;
  WMArrayIterator i;
  WM_ITERATE_ARRAY(array, bla, i) {
    printf("%s\n", bla->text);
  }
}*/

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
