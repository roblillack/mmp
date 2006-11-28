#include "mmp.h"
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_BACKEND_MPLAYER
Backend* mplayer_create(Frontend*);
#endif

int main(int argc, char *argv[]) {
  Frontend *frontend;

  //_Xdebug = True;
  WMInitializeApplication("mmp", &argc, argv);
  frontend = feCreate();

#ifdef HAVE_BACKEND_MPG123
  feAddBackend(frontend, mpg123_create(frontend));
#endif
#ifdef HAVE_BACKEND_MPLAYER
  feAddBackend(frontend, mplayer_create(frontend));
#endif

  feInit(frontend);
  struct stat sb;
  if (argv[1] && stat(argv[1], &sb) == 0) {
    stat(argv[1], &sb);
    if (S_ISREG(sb.st_mode)) {
      char *sep;
      char *name = NULL;
      for (sep = &argv[1][strlen(argv[1])-1]; sep > argv[1]; sep--) {
        if (*sep == '/') {
          *sep = '\0';
          if (strlen(argv[1])) {
            feShowDir(frontend, argv[1]);
          }
          feMarkFile(frontend, sep+1);
          cbPlaySong(NULL, frontend);
          break;
        }
      }
    } else if (S_ISDIR(sb.st_mode)) {
      feShowDir(frontend, argv[1]);
    }
  } else {
    feShowDir(frontend, NULL);
  }


  feRun(frontend);

  return 0;
}
