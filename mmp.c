#include <mmp.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>

static Frontend *frontend = NULL;

void handleSigchld(int sig) {
  if (frontend) {
    feHandleSigChild(frontend);
  }
}

int main(int argc, char *argv[]) {
  Backend *be;

  //_Xdebug = True;
  WMInitializeApplication("mmp", &argc, argv);
  frontend = feCreate();
  be = beCreate();

  if (!beInit(be)) {
    return 1;
  } else {
    feAddBackend(frontend, be);
  }

  feInit(frontend);
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
            feShowDir(frontend, argv[1]);
          }
          feMarkFile(frontend, sep+1);
          cbPlaySong(NULL, frontend);
          break;
        }
      }
    } else if (S_ISDIR(sb.st_mode)) {
      feShowDir(frontend, argv[1]);
    } else {
      feShowDir(frontend, ".");
    }
  } else {
    feShowDir(frontend, NULL);
  }

  if (signal(SIGCHLD, handleSigchld) == SIG_ERR) {
    perror("could not setup signal handler");
    return False;
  }

  feRun(frontend);

  return 0;
}
