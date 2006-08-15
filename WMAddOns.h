#include <WINGs/WINGs.h>

typedef struct W_MaskedEvents WMMaskedEvents;

WMMaskedEvents* WMMaskEvents(WMView*);
void WMFreeMaskedEvents(WMMaskedEvents*);

