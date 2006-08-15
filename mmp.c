#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <unistd.h>

#include <WINGs/WINGs.h>
#include <locale.h>
#include <dirent.h>
#include <signal.h>

#include "WMAddOns.h"

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

#define		WinHeightIfSmall	100
#define		APP_LONG		"minimalistic music player"
#define		APP_SHORT		"mmp"
#define		APP_VERSION		"v0.1"
#define		STOPPED			0
#define		PLAYING			1

void loadFiles(char*);
void nextSong(WMWidget*, void*);
void stopPlaying(WMWidget*, void*);

//Display		*dpy;
WMWindow  *win;
WMScreen  *scr;
WMLabel   *songtitlelabel, *songtitle, *songartist, *statuslabel, *songtime;
WMList    *datalist;
WMButton  *sizebutton, *playsongbutton, *prevsongbutton, *nextsongbutton,
          *stopsongbutton, *dirupbutton, *quitbutton;

WMHandlerID	playerHandlerID;
int bigsize = 0, largestnumber, VisibleRecord, ListHeight,
    WinHeightIfBig, fieldsChanged;
int pipeToPlayer[2], pipeFromPlayer[2];
pid_t childpid;
int motherkilledit;
int playmode=STOPPED;

char *currentdir;
char teststring[100];

void handlePlayerInput(int fd, int mask, void *clientData) {
  char bigbuf[8192], *buf, buf2[100], buf3[100], *laststop;
  char *str=NULL, *str_start=NULL, *str_end=NULL;
  int len, a;
  long secpassed=0, sectotal=0;
	
  for (a=sizeof(bigbuf)/sizeof(char); a>0; a--)
    bigbuf[a-1]='\0';

  len=read(pipeFromPlayer[0], bigbuf, 8192);
  for (buf=strtok_r(bigbuf, "\n", &laststop); buf; buf=strtok_r(NULL, "\n", &laststop)) {

		if(buf[0]=='@') {
			if(buf[1]=='F') {
				strcpy(buf2, WMGetLabelText(songtime));
				str_end=rindex(buf, ' ');
				str_end+=sizeof(char);
				*(str_end-1)='\0'; 
				str_start=rindex(buf, ' ')+sizeof(char);
				secpassed=atol(str_start);
				sectotal=atol(str_start)+atol(str_end);
				
				str=index(buf2, '/');
				if(str) {
					sprintf(buf3, "%.2d:%.2d %s", (int)secpassed/60, (int)secpassed%60, str);
				} else {
					sprintf(buf3, "%.2d:%.2d / %.2d:%.2d",
						(int)secpassed/60, (int)secpassed%60,
						(int)sectotal/60, (int)sectotal%60);
				}
				if(strcmp(buf3, buf2)!=0) {
					WMSetLabelText(songtime, buf3);
					/* printf("new time: %s\n", buf); */
				}
			} else if(buf[1]=='I') {
				if(buf[2]==' ' && buf[3]=='I' && buf[4]=='D'
				&& buf[5]=='3' && buf[6]==':') {
					/* get title */
					strncpy(buf2, buf+7, 100);
					buf2[30]='\0';
					while(buf2[strlen(buf2)-1]==' ')
						buf2[strlen(buf2)-1]='\0';
					WMSetLabelText(songtitle, buf2);
					/* get artist */
					strncpy(buf2, buf+37, 100);
					buf2[30]='\0';
					while(buf2[strlen(buf2)-1]==' ')
						buf2[strlen(buf2)-1]='\0';
					WMSetLabelText(songartist, buf2);
				}
			} else if(buf[1]=='P' && buf[3]=='0') {
				if (!motherkilledit) nextSong(NULL, NULL);
			} else if(buf[1]=='R') {
			} else if(buf[1]=='S') {
			} else {
				printf("MPG123 INPUT: %s\n", buf);
			}
		} else printf("UNKNOWN: %s\n", buf);
	}
}

void loadFiles(char *dirname) {
  DIR *dirptr;
  struct dirent *entry;
  char realdirname[PATH_MAX];
  char *buf;

  WMClearList(datalist);
  assert(realpath(dirname, realdirname));

  dirptr = opendir(realdirname);
  if (dirptr) {
    while ((entry = readdir(dirptr))) {
      if(strcmp(entry->d_name+strlen(entry->d_name)-4, ".mp3")==0) {
        /* add the file name */
        WMAddListItem(datalist, entry->d_name);
      } else if((strcmp(entry->d_name, ".")!=0) && (entry->d_type==DT_DIR)) {
        /* root dir? don't show .. */
        if (strcmp(entry->d_name, "..") == 0 && strcmp(realdirname, "/") == 0)
          continue;
        /* add " > " to the directory name and add it */
        buf = wstrconcat(" > ", entry->d_name);
        WMAddListItem(datalist, buf);
        wfree(buf);
      }
    }
    closedir(dirptr);
  }

  wfree(currentdir);
  currentdir = wstrdup(realdirname);

  WMSortListItems(datalist);
}

void playSong(WMWidget *self, void *data) {
  char buf[PATH_MAX];

  /* selected entry is a dir? */
  if (strncmp(WMGetListSelectedItem(datalist)->text, " > ", 3)==0) {
    snprintf(buf, PATH_MAX, "%s/%s", currentdir,
             WMGetListSelectedItem(datalist)->text+3);
    loadFiles(buf);
    return;
  }

  /* are we playing something? stop it! */
  stopPlaying(NULL, NULL);

  /* in case we have no id3-tag, set song name to filename minus .mp3 */
  strncpy(buf, WMGetListSelectedItem(datalist)->text, PATH_MAX);
  buf[strlen(buf)-4]='\0';
  WMSetLabelText(songtitle, buf);
  WMSetLabelText(songartist, APP_SHORT" "APP_VERSION);

  /* list playing. so stopping will play the next song. */
  motherkilledit = 0;

  /* create a pipe for the child to talk to us. */
  if (pipe(pipeFromPlayer) == -1) {
    perror("[mmp] could not set up ipc.");
    exit(1);
  }
  if (pipe(pipeToPlayer) == -1) {
    perror("[mmp] could not set up ipc.");
    exit(1);
  }
  /* set up a handler for the reading pipe */
  playerHandlerID = WMAddInputHandler(pipeFromPlayer[0], 1, handlePlayerInput, NULL);
  /* now, start the new process */
  childpid = fork();
  if (childpid == 0) {
    /* redirect stdout to pipeFromPlayer */
    close(1);
    dup(pipeFromPlayer[1]);
    close(pipeFromPlayer[1]);
    /* don't need this end of the pipe. */
    close(pipeFromPlayer[0]);
    /* close stdin to pipeToPlayer */
    close(0);
    dup(pipeToPlayer[0]);
    close(pipeToPlayer[0]);
    close(pipeToPlayer[1]);
    execlp("mpg123", "mpg123", "-R", "/dev/null", NULL);
    perror("[mmp] error running mpg123\n");
    exit(1);
  } else if (childpid == -1) {
    printf("[mmp] error. could not fork....\n");
    childpid=0;
  }

  /* we don't need to write to this pipe */
  close(pipeFromPlayer[1]);
  close(pipeToPlayer[0]);
  /* now send the file to play to the child process */
  snprintf(buf, PATH_MAX, "L %s/%s\n", currentdir, WMGetListSelectedItem(datalist)->text);
  write(pipeToPlayer[1], buf, strlen(buf));
  playmode = PLAYING;
}

void stopPlaying(WMWidget *self, void *data) {
  if (childpid) {
    motherkilledit = 1;
    kill(childpid, SIGINT);
    kill(childpid, SIGTERM);
    kill(childpid, SIGKILL);
    waitpid(childpid, NULL, 0);
    close(pipeFromPlayer[0]);
    close(pipeToPlayer[1]);
    WMDeleteInputHandler(playerHandlerID);
    WMSetLabelText(songtitle, APP_LONG);
    WMSetLabelText(songartist, "no file loaded.");
    WMSetLabelText(songtime, "");
  }
  childpid = 0;
}

void nextSong(WMWidget *self, void *data) {
  /*int actnum = WMGetListSelectedItemRow(datalist);

  stopPlaying(NULL, NULL);
  WMUnselectAllListItems(datalist);
  actnum++;
  if (actnum > WMGetListNumberOfRows(datalist) - 1)
    actnum = WMGetListNumberOfRows(datalist) - 1;
  WMSelectListItem(datalist, actnum);
  playSong(NULL, NULL);*/

  static WMPixmap *appicon = NULL;
  static WMPixmap *appicon_playing = NULL;
  static WMPixmap *appicon_small = NULL;
  static WMPixmap *appicon_smallplaying = NULL;

  if (!appicon) {
    appicon = WMCreatePixmapFromXPMData(scr, appicon_xpm);
    appicon_playing = WMCreatePixmapFromXPMData(scr, appicon_playing_xpm);
    appicon_small = WMCreatePixmapFromXPMData(scr, appicon_small_xpm);
    appicon_smallplaying = WMCreatePixmapFromXPMData(scr, appicon_smallplaying_xpm);
  }

  WMPixmap *bla = NULL;

  static int count = 0;
  switch (count) {
    case 0: bla = appicon; break;
    case 1: bla = appicon_small; break;
    case 2: bla = appicon_playing; break;
    default:
      bla = appicon_smallplaying;
      count = -1;
  }
  count++;
  printf("new count: %d\n", count);

  WMSetApplicationIconPixmap(scr, bla);
  WMSetWindowMiniwindowPixmap(win, bla);
}

void prevSong(WMWidget *self, void *data) {
  int actnum = WMGetListSelectedItemRow(datalist);

  stopPlaying(NULL, NULL);
  WMUnselectAllListItems(datalist);
  actnum--;
  if(actnum < 0) actnum = 0;
  WMSelectListItem(datalist, actnum);
  playSong(NULL, datalist);
}

void changesize(WMWidget *self, void *data) {
  if(bigsize) {
    WMResizeWidget(win, 350, WinHeightIfSmall);
  } else {
    WMResizeWidget(win, 350, WinHeightIfBig);
  }
}

void SizeChanged(void *self, WMNotification *notif) {
  if(WMWidgetHeight(win)<=WinHeightIfSmall+30) {
    if(WMWidgetHeight(win) != WinHeightIfSmall) {
      WMResizeWidget(win, WMWidgetWidth(win), WinHeightIfSmall);
    }
    WMSetButtonImage(sizebutton, WMCreatePixmapFromXPMData(scr, down_xpm));
    bigsize = 0;
  } else {
    WMSetButtonImage(sizebutton, WMCreatePixmapFromXPMData(scr, up_xpm));
    WMResizeWidget(datalist, WMWidgetWidth(datalist), WMWidgetHeight(win)-WinHeightIfSmall+2);
    ListHeight = WMWidgetHeight(datalist) / WMGetListItemHeight(datalist);
    WinHeightIfBig = WMWidgetHeight(win);
    if(WMGetListSelectedItemRow(datalist) >= WMGetListPosition(datalist) + ListHeight) {
      WMSetListPosition(datalist, WMGetListSelectedItemRow(datalist)-ListHeight+1);
    }
    bigsize = 1;
  }
}

void quit(WMWidget *self, void *data) {
  stopPlaying(NULL, NULL);
  exit(0);
}

void childDead(int sig) {
  if (!motherkilledit &&
      (WMGetListSelectedItemRow(datalist) < WMGetListNumberOfRows(datalist) - 1)
     ) {
    nextSong(NULL, NULL);
  }
}

WMArray* DropDataTypes(WMView *view) {
  WMArray *res = WMCreateArray(2);
  WMReplaceInArray(res, 0, "text/uri-list");
  WMReplaceInArray(res, 1, "text/x-dnd-username");
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
  
  printf("should send out data of type %s\n", type);
  if (strncmp(type, "text/uri-list", 13) == 0) {
    gethostname(hostname, 255);
    hostname[256] = '\0';
    filename = WMGetListSelectedItem(datalist)->text;
    if (strncmp(filename, " > ", 3) == 0) filename += 3;
    snprintf(buf, 1024, "file://%s%s/%s",
             hostname, currentdir, filename);
    printf("sending: %s\n", buf);
    return WMCreateDataWithBytes((void*)buf, strlen(buf)+1);
  }
  return WMCreateDataWithBytes(NULL, 0);
}

int main(int argc, char *argv[]) {
  char buf[1024];
  XEvent event;
  int i;
  RContext rcontext;
  Display *dpy;

  setlocale(LC_ALL, "");

  WMInitializeApplication("mmp", &argc, argv);
  dpy = XOpenDisplay(NULL);
  //scr = WMCreateSimpleApplicationScreen(dpy);
  //scr = WMOpenScreen(NULL);
  // the only way to get an appicon seems to be:
  scr = WMCreateScreen(dpy, DefaultScreen(dpy));
  assert(scr != NULL);
  assert(WMScreenRContext(scr) != NULL);

  if(signal(SIGCHLD, childDead) == SIG_ERR) {
    perror("could not setup signal handler");
    exit(1);
  }
  
  WMSetApplicationHasAppIcon(scr, True);
  WMSetApplicationIconPixmap(scr, WMCreatePixmapFromXPMData(scr, appicon_xpm));

  win = WMCreateWindow (scr, APP_SHORT);
  WMSetWindowTitle(win, APP_LONG);
  WMSetWindowCloseAction(win, quit, NULL);
  WMSetWindowMinSize(win, 280, WinHeightIfSmall);
  WMSetWindowMaxSize(win, 280, 2000);
  WMSetWindowMiniwindowPixmap(win, WMCreatePixmapFromXPMData(scr, appicon_xpm));
  WMSetWindowMiniwindowTitle(win, APP_SHORT);

  songtitlelabel = WMCreateLabel(win);
  WMSetLabelText(songtitlelabel, "currently playing:");
  WMSetLabelTextColor(songtitlelabel, WMDarkGrayColor(scr));
  WMResizeWidget(songtitlelabel, 260, 16);
  WMMoveWidget(songtitlelabel, 10, 15);

  songtitle = WMCreateLabel(win);
  WMSetLabelText(songtitle, APP_LONG);
  WMSetLabelTextAlignment(songtitle, WARight);
  WMSetLabelTextColor(songtitle, WMCreateRGBColor(scr, 128<<8, 0, 0, False));
  WMResizeWidget(songtitle, 260, 20);
  WMMoveWidget(songtitle, 10, 40);
  WMSetLabelFont(songtitle, WMSystemFontOfSize(scr, 18));

  songartist = WMCreateLabel(win);
  WMSetLabelText(songartist, APP_SHORT" "APP_VERSION);
  WMSetLabelTextAlignment(songartist, WARight);
  /*WMSetWidgetBackgroundColor(songartist, WMCreateRGBColor(scr, 222<<8, 0, 0, False));*/
  WMSetLabelTextColor(songartist, WMCreateRGBColor(scr, 64<<8, 0, 0, False));
  WMResizeWidget(songartist, 150, 15);
  WMMoveWidget(songartist, 120, 25);

  songtime = WMCreateLabel(win);
  WMSetLabelText(songtime, "");
  WMSetLabelTextColor(songtime, WMDarkGrayColor(scr));
  WMResizeWidget(songtime, 100, 16);
  WMMoveWidget(songtime, 10, 65);

  statuslabel = WMCreateLabel(win);
  WMSetLabelTextColor(statuslabel, WMDarkGrayColor(scr));
  WMSetLabelFont(statuslabel, WMSystemFontOfSize(scr, 10));
  WMResizeWidget(statuslabel, 200, 14);
  WMMoveWidget(statuslabel, 7, 242);

  prevsongbutton = WMCreateButton(win, WBTMomentaryPush);
  WMSetButtonImage(prevsongbutton, WMCreatePixmapFromXPMData(scr, prev_xpm));
  WMSetButtonImagePosition(prevsongbutton, WIPImageOnly);
  WMSetButtonAction(prevsongbutton, prevSong, datalist);
  WMSetBalloonTextForView("play the previous song.", WMWidgetView(prevsongbutton));
  WMSetButtonBordered(prevsongbutton, False);
  WMResizeWidget(prevsongbutton, 20, 20);
  WMMoveWidget(prevsongbutton, 190, 60);

  stopsongbutton = WMCreateButton(win, WBTMomentaryPush);
  WMSetButtonImage(stopsongbutton, WMCreatePixmapFromXPMData(scr, stop_xpm));
  WMSetButtonImagePosition(stopsongbutton, WIPImageOnly);
  WMSetButtonAction(stopsongbutton, stopPlaying, NULL);
  WMSetBalloonTextForView("stops playback.", WMWidgetView(stopsongbutton));
  WMSetButtonBordered(stopsongbutton, False);
  WMResizeWidget(stopsongbutton, 20, 20);
  WMMoveWidget(stopsongbutton, 210, 60);

  playsongbutton = WMCreateButton(win, WBTMomentaryPush);
  WMSetButtonImage(playsongbutton, WMCreatePixmapFromXPMData(scr, play_xpm));
  WMSetButtonImagePosition(playsongbutton, WIPImageOnly);
  WMSetButtonAction(playsongbutton, playSong, datalist);
  WMSetBalloonTextForView("play the selected song.", WMWidgetView(playsongbutton));
  WMSetButtonBordered(playsongbutton, False);
  WMResizeWidget(playsongbutton, 20, 20);
  WMMoveWidget(playsongbutton, 230, 60);

  nextsongbutton = WMCreateButton(win, WBTMomentaryPush);
  WMSetButtonImage(nextsongbutton, WMCreatePixmapFromXPMData(scr, next_xpm));
  WMSetButtonImagePosition(nextsongbutton, WIPImageOnly);
  WMSetBalloonTextForView("play the next song.", WMWidgetView(nextsongbutton));
  WMSetButtonAction(nextsongbutton, nextSong, datalist);
  WMSetButtonBordered(nextsongbutton, False);
  WMResizeWidget(nextsongbutton, 20, 20);
  WMMoveWidget(nextsongbutton, 250, 60);

  quitbutton = WMCreateButton(win, WBTMomentaryPush);
  WMSetButtonText(quitbutton, "quit");
  WMSetButtonBordered(quitbutton, False);
  WMSetButtonAction(quitbutton, quit, NULL);
  WMSetBalloonTextForView("quit the program.", WMWidgetView(quitbutton));
  WMSetButtonFont(quitbutton, WMSystemFontOfSize(scr, 10));
  WMResizeWidget(quitbutton, 30, 15);
  WMMoveWidget(quitbutton, 240, WinHeightIfSmall-15);

  sizebutton = WMCreateCustomButton(win, WBBPushInMask);
  WMSetButtonImage(sizebutton, WMCreatePixmapFromXPMData(scr, down_xpm));
  WMSetButtonImagePosition(sizebutton, WIPImageOnly);
  WMSetButtonBordered(sizebutton, False);
  WMSetButtonAction(sizebutton, changesize, NULL);
  WMSetBalloonTextForView("show/hide the song list.", WMWidgetView(sizebutton));
  WMResizeWidget(sizebutton, 30, 15);
  WMMoveWidget(sizebutton, 10, WinHeightIfSmall-15);

  datalist = WMCreateList(win);
  WMSetListAllowMultipleSelection(datalist, 0);
  WMSetListAllowEmptySelection(datalist, 0);
  WMSetListDoubleAction(datalist, playSong, NULL);
  WMResizeWidget(datalist, 260, 10);
  WMMoveWidget(datalist, 10, WinHeightIfSmall+1);

  /* mask mouse events while dragging */
  WMMaskedEvents *mask = WMMaskEvents(WMWidgetView(datalist));

  WMDragSourceProcs procs;
  procs.dropDataTypes = DropDataTypes;
  procs.wantedDropOperation = WantedDropOperation;
  procs.askedOperations = NULL;
  procs.acceptDropOperation = AcceptDropOperation;
  procs.beganDrag = BeganDrag;
  procs.endedDrag = EndedDrag;
  procs.fetchDragData = FetchDragData;
  WMSetViewDraggable(WMWidgetView(datalist), &procs,
                     WMCreatePixmapFromXPMData(scr, dnd_xpm));

  WMSetViewNotifySizeChanges(WMWidgetView(win), True);
  WMAddNotificationObserver(SizeChanged, NULL, WMViewSizeDidChangeNotification, WMWidgetView(win));

  WMRealizeWidget(win);
  WMMapSubwidgets(win);
  WMMapWidget(win);

  /*changesize(NULL, NULL);*/

  loadFiles(argv[1] ? argv[1] : ".");


  WMScreenMainLoop(scr);

  WMFreeMaskedEvents(mask);
  return 0;
}
