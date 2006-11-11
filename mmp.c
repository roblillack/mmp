#include <mmp.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
  Frontend *fe;
  Backend *be;

  //_Xdebug = True;
  WMInitializeApplication("mmp", &argc, argv);
  fe = feCreate();
  be = beCreate();

  if (!beInit(be)) {
    return 1;
  } else {
    feAddBackend(fe, be);
  }

  feInit(fe);
  if (argv[1]) {
    struct stat sb;
    stat(argv[1], &sb);
    if (S_ISREG(sb.st_mode)) {
      char *sep;
      char *name = NULL;
      for (sep = &argv[1][strlen(argv[1])-1]; sep > argv[1]; sep--) {
        if (*sep == '/') {
          *sep = '\0';
          if (strlen(argv[1])) {
            feShowDir(fe, argv[1]);
          }
          feMarkFile(fe, sep+1);
          cbPlaySong(NULL, fe);
          break;
        }
      }
    } else if (S_ISDIR(sb.st_mode)) {
      feShowDir(fe, argv[1]);
    } else {
      feShowDir(fe, ".");
    }
  } else {
    feShowDir(fe, NULL);
  }
  feRun(fe);

  return 0;
}
