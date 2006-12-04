#include "mmp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <unistd.h>

#include <locale.h>

#define ucfree(x) if(x) { wfree(x); x = NULL; }

typedef struct mpg123Backend {
  // public
  WMArray* (*GetSupportedExtensions)();
      void (*Init)                  ();
      Bool (*IsPlaying)             ();
      void (*Play)                  (const char*);
      void (*StopNow)               ();
      void (*switchFullscreen)       ();
      void (*pause)                  ();
      void (*seekForward)            ();
      void (*seekBackward)           ();
  // private
  Frontend *frontend;
  int pipeToPlayer[2], pipeFromPlayer[2];
  pid_t childPid;
  WMHandlerID playerHandlerID;
  WMHandlerID signalHID;
} mpg123Backend;

mpg123Backend *mpg123_create(Frontend*);
WMArray *mpg123_getSupportedExtensions();
void mpg123_init();
Bool mpg123_isPlaying();
void mpg123_play(const char*);
void mpg123_stopNow();

void mpg123_stop();
void mpg123_cb_stopped(void*);
void mpg123_cleanup();
void mpg123_handle_input(int, int, void*);
char* mpg123_find_next_number(char*, unsigned int);

static mpg123Backend mpg123_backend;
// -----------------------------------------------------------------------------

mpg123Backend *mpg123_create(Frontend *f) {
  mpg123_backend.GetSupportedExtensions = mpg123_getSupportedExtensions;
  mpg123_backend.Init = mpg123_init;
  mpg123_backend.IsPlaying = mpg123_isPlaying;
  mpg123_backend.Play = mpg123_play;
  mpg123_backend.StopNow = mpg123_stopNow;
  mpg123_backend.switchFullscreen = NULL;
  mpg123_backend.pause = NULL;
  mpg123_backend.seekForward = NULL;
  mpg123_backend.seekBackward = NULL;

  mpg123_backend.frontend = f;
  mpg123_init();
  return &mpg123_backend;
}

Bool mpg123_isPlaying() {
  return (mpg123_backend.childPid > 0);
}

void mpg123_init() {
  FB("beInit");
  mpg123_backend.childPid = 0;
  mpg123_backend.signalHID = NULL;
  mpg123_backend.playerHandlerID = NULL;
  FE("beInit");
}

void mpg123_play(const char *filename) {
  FB("bePlay");

  /* should we're playing st. stop it */
  if (mpg123_backend.childPid) {
    D1("bePlay needs to Stop child process.\n");
    mpg123_stopNow();
  }

  /* create a pipe for the child to talk to us. */
  if (pipe(mpg123_backend.pipeFromPlayer) == -1 || pipe(mpg123_backend.pipeToPlayer) == -1) {
    perror("[mmp] could not set up ipc.");
    return;
  }

  /* set up a handler for the reading pipe */
  mpg123_backend.playerHandlerID = WMAddInputHandler(mpg123_backend.pipeFromPlayer[0], 1,
                                                      mpg123_handle_input, NULL);

  /* now, start the new process */
  mpg123_backend.childPid = fork();
  if (mpg123_backend.childPid == 0) {
    /* redirect stdout to pipeFromPlayer */
    close(1);
    dup(mpg123_backend.pipeFromPlayer[1]);
    close(mpg123_backend.pipeFromPlayer[1]);
    /* don't need this end of the pipe. */
    close(mpg123_backend.pipeFromPlayer[0]);
    /* close stdin to pipeToPlayer */
    close(0);
    dup(mpg123_backend.pipeToPlayer[0]);
    close(mpg123_backend.pipeToPlayer[0]);
    close(mpg123_backend.pipeToPlayer[1]);
    execlp("mpg123", "mpg123", "-R", "/dev/null", NULL);
    perror("[mmp] error running mpg123\n");
    return;
  } else if (mpg123_backend.childPid == -1) {
    printf("[mmp] error. could not fork....\n");
    mpg123_backend.childPid = 0;
    close(mpg123_backend.pipeFromPlayer[0]);
    close(mpg123_backend.pipeToPlayer[1]);
  }

  /* we don't need to write to this pipe */
  close(mpg123_backend.pipeFromPlayer[1]);
  close(mpg123_backend.pipeToPlayer[0]);

  mpg123_backend.signalHID = WMAddSignalHandler(SIGCHLD, mpg123_cb_stopped, NULL);

  /* now send the file to play to the child process */
  write(mpg123_backend.pipeToPlayer[1], "L ", 2);
  write(mpg123_backend.pipeToPlayer[1], filename, strlen(filename));
  write(mpg123_backend.pipeToPlayer[1], "\n", 1);

  FE("bePlay");
}

void mpg123_cb_stopped(void *cdata) {
  FB("cbStopped");
  if (mpg123_backend.childPid) {
    mpg123_cleanup();
    WMAddIdleHandler(fePlayingStopped, mpg123_backend.frontend);
  }
  FE("cbStopped");
}

void mpg123_stop() {
  FB("beStop");
  if (mpg123_backend.childPid) {
    D1("sending SIGINT...\n");
    kill(mpg123_backend.childPid, SIGINT);
  }
  FE("beStop\n");
}

void mpg123_stopNow() {
  FB("beStopNow");
  if (mpg123_backend.childPid) {
    kill(mpg123_backend.childPid, SIGINT);
    int status;
    do {
      wait(&status);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    mpg123_cleanup();
  }
  FE("beStopNow");
}

void mpg123_cleanup() {
  FB("cleanUpChild");
  close(mpg123_backend.pipeFromPlayer[0]);
  close(mpg123_backend.pipeToPlayer[1]);
  WMDeleteInputHandler(mpg123_backend.playerHandlerID);
  WMDeletePipedSignalHandler(mpg123_backend.signalHID);
  mpg123_init();
  FE("cleanUpChild");
}

WMArray* mpg123_getSupportedExtensions() {
  WMArray* types = WMCreateArray(1);
  WMAddToArray(types, "mp3");
  return types;
}

// -----------------------------------------------------------------------------

char *mpg123_find_next_number(char *text, unsigned int ignore) {
  char *i;
  unsigned char ignoring = 0;

  if (!text) return NULL;

  for (i = text; ; i++) {
    if (*i == '\0') return NULL;
    if (isdigit(*i)) {
      if (!ignoring) {
        if (ignore) {
          ignore--;
          ignoring = 1;
        } else {
          return i;
        }
      }
    } else {
      if (ignoring) {
        ignoring = 0;
      }
    }
  }

  return NULL;
}

void mpg123_handle_input(int fd, int mask, void *clientData) {
  //FB("handlePlayerInput");
#define BUFSIZE 8192

  char bigbuf[BUFSIZE];   // the whole buffer
  char *buf;              // pointer the line we work with
  char *laststop = NULL;
  char *start;
  int len;

  if (mpg123_backend.childPid == 0) {
    D1("no child alive in handlePlayerInput.\n");
    return;
  }

  len = read(mpg123_backend.pipeFromPlayer[0], bigbuf, BUFSIZE-1);
  // drop last (incomplete) line, if buffer full
  if (len == BUFSIZE-1 && bigbuf[BUFSIZE-1] != '\n') {
    char *i;
    for (i = &bigbuf[BUFSIZE-2]; i > bigbuf; i--) {
      if (*i == '\n') {
        *i = '\0';
        break;
      }
    }
  } else {
    bigbuf[len] = '\0';
  }
  
  //printf("read %d bytes. bigbuf: %s\n", len, bigbuf);
  for (buf = strtok_r(bigbuf, "\n", &laststop); buf;
       buf = strtok_r(NULL, "\n", &laststop)) {
    if (buf[0] == '@') {
      if (buf[1] == 'F') {
        char *sep = NULL;
        float ratio = (float) strtol(strtok_r(buf+3, " ", &sep), NULL, 10);
        ratio = (float) (ratio / (double) (ratio + (double)strtol(strtok_r(NULL, " ", &sep), NULL, 10)));
        unsigned int secpassed = (unsigned int) strtol(strtok_r(NULL, " ", &sep), NULL, 10);
        unsigned int sectotal = (unsigned int) (secpassed + strtol(strtok_r(NULL, " ", &sep), NULL, 10));
        feSetFileLength(mpg123_backend.frontend, sectotal);
        feSetCurrentPosition(mpg123_backend.frontend, secpassed);
      } else if (buf[1] == 'I') {
        /* format: "@I ID3:<30-chars-songname><30-chars-artist><32-chars-year???><genre>"
                           ^ buf+7            ^ buf+37         ^ buf+67          ^ buf+99 */
        if (strncmp(buf+3, "ID3", 3) == 0) {
          buf[36] = '\0';
          while (strlen(buf+7) > 0 && buf[strlen(buf)-1] == ' ') {
            buf[strlen(buf)-1] = '\0';
          }
          feSetTitle(mpg123_backend.frontend, buf+7);
          buf[66] = '\0';
          while (strlen(buf+37) > 0 && buf[36+strlen(buf+37)] == ' ') {
            buf[36+strlen(buf+37)] = '\0';
          }
          feSetArtist(mpg123_backend.frontend, buf+37);
        }
      } else if(buf[1]=='P' && buf[3]=='0') {
        mpg123_stop();
      } else if(buf[1]=='R') {
        // @R MPG123
      } else if(buf[1]=='S') {
        // ?
      } else {
        printf("MPG123 INPUT: %s\n", buf);
      }
    } else printf("UNKNOWN: %s\n", buf);
  }
}
