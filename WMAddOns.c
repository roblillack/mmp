#include <X11/Xlib.h>
#include <X11/Xmd.h>

#include <WINGs/WINGsP.h>
#include "WMAddOns.h"

#define W_GetViewEventHandlers(x) (WMArray*)(((W_View*)x)->eventHandlers)

typedef struct W_MaskedEvents {
  WMView *view;
  WMArray *procs;
  WMArray *data;
} W_MaskedEvents;

/*
 * The following structure is taken from wlist.c of the version 0.92.0
 * Window Maker sources. I don't think so but it _may_ fall under the LGPL:
 *   WINGs is copyright (c) Alfredo K. Kojima and is licensed through the GNU
 *   Library General Public License (LGPL).
 */
typedef struct W_List {
    W_Class widgetClass;
    W_View *view;
    WMArray *items;
    WMArray *selectedItems;
    short itemHeight;
    int topItem;
    short fullFitLines;
    void *clientData;
    WMAction *action;
    void *doubleClientData;
    WMAction *doubleAction;
    WMListDrawProc *draw;
    WMHandlerID *idleID;
    WMHandlerID *selectID;
    WMScroller *vScroller;
    Pixmap doubleBuffer;
    struct {
        unsigned int allowMultipleSelection:1;
        unsigned int allowEmptySelection:1;
        unsigned int userDrawn:1;
        unsigned int userItemHeight:1;
        unsigned int dontFitAll:1;
        unsigned int redrawPending:1;
        unsigned int buttonPressed:1;
        unsigned int buttonWasPressed:1;
    } flags;
} W_List;

typedef struct W_Window {
    W_Class widgetClass;
    W_View *view;

    struct W_Window *nextPtr;          /* next in the window list */

    struct W_Window *owner;

    char *title;

    WMPixmap *miniImage;               /* miniwindow */
    char *miniTitle;

    char *wname;

    WMSize resizeIncrement;
    WMSize baseSize;
    WMSize minSize;
    WMSize maxSize;
    WMPoint minAspect;
    WMPoint maxAspect;

    WMPoint upos;
    WMPoint ppos;

    WMAction *closeAction;
    void *closeData;

    int level;

    struct {
        unsigned style:4;
        unsigned configured:1;
        unsigned documentEdited:1;

        unsigned setUPos:1;
        unsigned setPPos:1;
        unsigned setAspect:1;
    } flags;
} W_Window;

typedef struct {
    CARD32 flags;
    CARD32 window_style;
    CARD32 window_level;
    CARD32 reserved;
    Pixmap miniaturize_pixmap;         /* pixmap for miniaturize button */
    Pixmap close_pixmap;               /* pixmap for close button */
    Pixmap miniaturize_mask;           /* miniaturize pixmap mask */
    Pixmap close_mask;                 /* close pixmap mask */
    CARD32 extra_flags;
} GNUstepWMAttributes;

#define GSWindowStyleAttr       (1<<0)
#define GSWindowLevelAttr       (1<<1)
#define GSMiniaturizePixmapAttr (1<<3)
#define GSClosePixmapAttr       (1<<4)
#define GSMiniaturizeMaskAttr   (1<<5)
#define GSCloseMaskAttr         (1<<6)
#define GSExtraFlagsAttr        (1<<7)

void W_MaskedEventHandler(XEvent *event, void *data) {
  W_MaskedEvents *events = (W_MaskedEvents*)data;
  WMEventProc *proc = NULL;
  unsigned int i;
  
  for (i = 0; i < WMGetArrayItemCount(events->procs); i++) {
    if (WMIsDraggingFromView(events->view) == False) {
      proc = WMGetFromArray(events->procs, i);
      (*proc)(event, WMGetFromArray(events->data, i));
    } else {
      /* *cough* evil *cough* hack... */
      if (W_CLASS(events->view->self) == WC_List) {
        ((W_List*)(events->view->self))->flags.buttonWasPressed = 0;
        ((W_List*)(events->view->self))->flags.buttonPressed = 0;
      }
    }
  }
}

WMMaskedEvents* WMMaskEvents(WMView* view) {
  W_MaskedEvents *mask;
  unsigned int i;
  Bool changed = False;

  mask = wmalloc(sizeof(W_MaskedEvents));
  mask->view = view;
  mask->procs = WMCreateArray(0);
  mask->data = WMCreateArray(0);

  for (i = 0; i < WMGetArrayItemCount(W_GetViewEventHandlers(view)); i++) {
    W_EventHandler *h = (W_EventHandler*) WMGetFromArray(W_GetViewEventHandlers(view), i);
    if (h->eventMask == (ButtonPressMask|ButtonReleaseMask|
                        EnterWindowMask|LeaveWindowMask|ButtonMotionMask)) {
      WMAddToArray(mask->procs, h->proc);
      WMAddToArray(mask->data, h->clientData);

      /* we change only the first handler to our one, because they seem
         to be processed upside-down and we want the dnd-handler to be processed
         first. */
      if (changed == False) {
        h->proc = W_MaskedEventHandler;
        h->clientData = (void*) mask;
        changed = True;
      } else {
        WMDeleteEventHandler(view, h->eventMask, h->proc, h->clientData);
      }
    }
  }

  return mask;
}

void WMFreeMaskedEvents(WMMaskedEvents* maskev) {
  W_MaskedEvents* mask = (W_MaskedEvents*)maskev;
  WMFreeArray(mask->procs);
  WMFreeArray(mask->data);
  wfree(mask);
  return;
}

Window WMGetLeader(W_Screen *scr) {
  return scr->groupLeader;
}

void WMWindowChangeStyle(W_Window *win, int style) {
  if (win->flags.style == style) return;
  XFlush(win->view->screen->display);
  win->flags.style = style;
  if (win->view->flags.realized) {
    WMSetWindowLevel(win, WMNormalWindowLevel);
  /*  GNUstepWMAttributes data;
    data.flags = GSWindowStyleAttr; // GSWindowStyleAttr
    data.window_style = style;
    XChangeProperty(win->view->screen->display, WMWidgetXID(win), win->view->screen->attribsAtom,
                    win->view->screen->attribsAtom, 32, PropModeReplace, (unsigned char*)&data, 9);
  }
  if (win->view->flags.mapped) {
    // FIXME: this does not work :(
    XPropertyEvent xev;
    memset(&xev, 0, sizeof(XPropertyEvent));
    xev.type = PropertyNotify;
    xev.state = PropertyNewValue;
    xev.atom = win->view->screen->attribsAtom;
    XSendEvent(win->view->screen->display, win->view->screen->rootWin, False, PropertyChangeMask, (XEvent*)&xev);
    //WMPoint pos = win->view->ppos;
    //WMUnmapWidget(win);
    //WMSetWindowInitialPosition(win, pos.x, pos.y);
    //WMMapWidget(win);*/
  }
}

void WMScreenAbortDrag(W_Screen *screen) {
  if (!screen || !screen->dragInfo.sourceInfo) return;
  W_DragSourceInfo *source = screen->dragInfo.sourceInfo;

  W_DragSourceStopTimer();

  // inform the other client
  if (source->destinationWindow)
    W_SendDnDClientMessage(screen->display, source->destinationWindow,
                           screen->xdndLeaveAtom, WMViewXID(source->sourceView), 0, 0, 0, 0);

  WMDeleteSelectionHandler(source->sourceView, screen->xdndSelectionAtom, CurrentTime);
  wfree(source->selectionProcs);

  if (source->sourceView->dragSourceProcs->endedDrag)
    source->sourceView->dragSourceProcs->endedDrag(source->sourceView, &source->imageLocation, False);

  if (source->icon)
    XDestroyWindow(screen->display, source->icon);

  if (source->dragCursor != None) {
    XDefineCursor(screen->display, screen->rootWin, screen->defaultCursor);
    XFreeCursor(screen->display, source->dragCursor);
  }

  wfree(source);
  screen->dragInfo.sourceInfo = NULL;
}

Atom WMGetXdndPositionAtom(W_Screen *screen) {
  return screen->xdndPositionAtom;
}

Atom WMGetXdndLeaveAtom(W_Screen *screen) {
  return screen->xdndLeaveAtom;
}

Window WMGetRootWin(W_Screen *screen) {
  return screen->rootWin;
}

Bool WMSetGrayColor(W_Screen *scr, WMColor *col) {
  if (!scr || !col) return False;
  WMReleaseColor(scr->gray);
  scr->gray = col;
  return True;
}

Bool WMSetDarkGrayColor(W_Screen *scr, WMColor *col) {
  if (!scr || !col) return False;
  WMReleaseColor(scr->darkGray);
  scr->darkGray = col;
  return True;
}

Bool WMSetWhiteColor(W_Screen *scr, WMColor *col) {
  if (!scr || !col) return False;
  WMReleaseColor(scr->white);
  scr->white = col;
  return True;
}

Bool WMSetBlackColor(W_Screen *scr, WMColor *col) {
  if (!scr || !col) return False;
  WMReleaseColor(scr->black);
  scr->black = col;
  return True;
}

void WMSetUDColorForKey(WMUserDefaults *s, WMColor *col, char *key) {
  static char colstr[8];
  snprintf(colstr, sizeof(colstr), "#%02x%02x%02x",
           WMRedComponentOfColor(col) >> 8,
           WMBlueComponentOfColor(col) >> 8,
           WMGreenComponentOfColor(col) >> 8);
  WMSetUDStringForKey(s, colstr, key);
}

WMColor* WMGetUDColorForKey(WMUserDefaults *s, char *key, WMScreen *scr) {
   char *str = WMGetUDStringForKey(s, key);
  if (!str || strlen(str) != 7) return NULL;
  char *nptr; 
  long color = strtol(str+1, &nptr, 16);
  if (*nptr != '\0') return NULL;
  return WMCreateRGBColor(scr, (color >> 16) << 8, color & 0xff00, (color & 0xff) << 8, False);
}

