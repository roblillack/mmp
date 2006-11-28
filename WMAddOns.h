#ifndef WMADDONS__H
#define WMADDONS__H

#include <WINGs/WINGs.h>

typedef struct W_MaskedEvents WMMaskedEvents;

WMMaskedEvents* WMMaskEvents(WMView*);
void WMFreeMaskedEvents(WMMaskedEvents*);

Window WMGetLeader(WMScreen*);
void WMWindowChangeStyle(WMWindow*,int);

Atom WMGetXdndPositionAtom(WMScreen*);
Atom WMGetXdndLeaveAtom(WMScreen*);
void WMScreenAbortDrag(WMScreen*);
Window WMGetRootWin(WMScreen*);

Bool WMSetGrayColor(WMScreen*, WMColor*);
Bool WMSetWhiteColor(WMScreen*, WMColor*);
Bool WMSetBlackColor(WMScreen*, WMColor*);
Bool WMSetDarkGrayColor(WMScreen*, WMColor*);

void WMSetUDColorForKey(WMUserDefaults*, WMColor*, char *);
WMColor* WMGetUDColorForKey(WMUserDefaults *, char *, WMScreen*);

WMHandlerID WMAddIdleSignalHandler(int, WMCallback*, void*);
void WMDeleteIdleSignalHandler(WMHandlerID);
WMHandlerID WMAddPipedSignalHandler(int, WMCallback*, void*);
void WMDeletePipedSignalHandler(WMHandlerID);
#define WMAddSignalHandler(x,y,z) WMAddPipedSignalHandler(x,y,z)
#define WMDeleteSignalHandler(x) WMDeletePipedSignalHandler(x)
#endif
