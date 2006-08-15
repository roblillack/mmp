#include <WINGs/WINGsP.h>
#include "WMAddOns.h"

#define W_GetViewEventHandlers(x) (WMArray*)(((W_View*)x)->eventHandlers)

typedef struct W_MaskedEvents {
  WMView *view;
  WMArray *procs;
  WMArray *data;
} W_MaskedEvents;

/* taken from wlist.c */
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
} List;

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
        ((List*)(events->view->self))->flags.buttonWasPressed = 0;
        ((List*)(events->view->self))->flags.buttonPressed = 0;
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
