#include <mmp.h>

int main(int argc, char *argv[]) {
  Frontend *fe;
  Backend *be;

  WMInitializeApplication("mmp", &argc, argv);
  fe = feCreate();
  be = beCreate();

  if (!beInit(be)) {
    return 1;
  } else {
    feAddBackend(fe, be);
  }

  feInit(fe);
  feShowDir(fe, argv[1] ? argv[1] : ".");
  feRun(fe);

  return 0;
}
