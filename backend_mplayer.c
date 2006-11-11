#include "mmp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <unistd.h>

#include <locale.h>
#include <signal.h>

typedef struct Backend {
  WMArray *frontends;
  int pipeToPlayer[2],
      pipeFromPlayer[2];
  pid_t childPid;
  Bool childLives;
  Bool forcedKill;
  Bool isPlaying;
  WMHandlerID playerHandlerID;
} mplayerBackend;

static mplayerBackend *firstBackend = NULL;
void handleSigchld(int);
void handlePlayerInput(int, int, void*);
void PlayingStopped(mplayerBackend*);
void SetArtist(mplayerBackend*, char*);
void SetSongName(mplayerBackend*, char*);
void SetCurrentPosition(mplayerBackend*, float, unsigned int, unsigned int);

// -----------------------------------------------------------------------------

Backend *beCreate() {
  mplayerBackend *b = wmalloc(sizeof(mplayerBackend));
  b->frontends = WMCreateArray(0);
  b->childPid = 0;
  return b;
}

Bool beInit(mplayerBackend *b) {
  if (!firstBackend) {
    firstBackend = b;
    if (signal(SIGCHLD, handleSigchld) == SIG_ERR) {
      perror("could not setup signal handler");
      return False;
    }
  }

  // search for mpg123....
  return True;
}

void beAddFrontend(mplayerBackend *b, Frontend *f) {
  if (WMFindInArray(b->frontends, NULL, f) == WANotFound) {
    WMAddToArray(b->frontends, f);
    feAddBackend(f, b);
  }
}

void beRemoveFrontend(mplayerBackend *b, Frontend *f) {
  int index;
  if ((index = WMFindInArray(b->frontends, NULL, f)) != WANotFound) {
    WMDeleteFromArray(b->frontends, index);
    feRemoveBackend(f, b);
  }
}

void bePlay(mplayerBackend *b, char *filename) {
  char buf[PATH_MAX];

  /* should we're playing st. stop it */
  beStop(b);

  /* list playing. so stopping will play the next song. */
  b->forcedKill = False;

  /* create a pipe for the child to talk to us. */
  if (pipe(b->pipeFromPlayer) == -1 || pipe(b->pipeToPlayer) == -1) {
    perror("[mmp] could not set up ipc.");
    return;
  }

  /* set up a handler for the reading pipe */
  b->playerHandlerID = WMAddInputHandler(b->pipeFromPlayer[0], 1,
                                         handlePlayerInput, b);

  /* now, start the new process */
  b->childPid = fork();
  if (b->childPid == 0) {
    /* redirect stdout to pipeFromPlayer */
    close(1);
    dup(b->pipeFromPlayer[1]);
    close(b->pipeFromPlayer[1]);
    /* don't need this end of the pipe. */
    close(b->pipeFromPlayer[0]);
    /* close stdin to pipeToPlayer */
    close(0);
    dup(b->pipeToPlayer[0]);
    close(b->pipeToPlayer[0]);
    close(b->pipeToPlayer[1]);
    execlp("mplayer", "mplayer", filename, NULL);
    perror("[mmp] error running mplayer\n");
    return;
  } else if (b->childPid == -1) {
    printf("[mmp] error. could not fork....\n");
    b->childPid=0;
  }

  /* we don't need to write to this pipe */
  close(b->pipeFromPlayer[1]);
  close(b->pipeToPlayer[0]);

  /* now send the file to play to the child process */
  /*snprintf(buf, PATH_MAX, "L %s\n", filename);
  write(b->pipeToPlayer[1], buf, strlen(buf));*/
  
  b->childLives = True;
  b->isPlaying = True;
}

void beStop(mplayerBackend* b) {
  printf("BESTOP\n");
  if (b->childPid) {
    printf("KILLING\n");
    b->forcedKill = True;
    kill(b->childPid, SIGINT);
    kill(b->childPid, SIGTERM);
    kill(b->childPid, SIGKILL);
    waitpid(b->childPid, NULL, 0);
    close(b->pipeFromPlayer[0]);
    close(b->pipeToPlayer[1]);
    WMDeleteInputHandler(b->playerHandlerID);
    b->childPid = 0;
  }
}

WMArray* beGetSupportedExtensions(Backend* b) {
  WMArray* types = WMCreateArray(11);
  WMAddToArray(types, "mp3");
  WMAddToArray(types, "ogg");
  WMAddToArray(types, "flac");
  WMAddToArray(types, "aac");
  WMAddToArray(types, "m4a");
  WMAddToArray(types, "wma");
  WMAddToArray(types, "avi");
  WMAddToArray(types, "mpg");
  WMAddToArray(types, "flv");
  WMAddToArray(types, "wmv");
  WMAddToArray(types, "mov");
  return types;
}

// -----------------------------------------------------------------------------

char *findNextNumber(char *text, unsigned int ignore) {
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

void handlePlayerInput(int fd, int mask, void *clientData) {
  mplayerBackend *b = (mplayerBackend*)clientData;
  assert(b != NULL);
#define BUFSIZE 8192

  char bigbuf[BUFSIZE];   // the whole buffer
  char *buf;              // pointer the line we work with
  char *laststop = NULL;
  //char *str = NULL, *str_start = NULL, *str_end = NULL;
  int len; //, a;
  
  if (b->childLives == False) {
    PlayingStopped(b);
  }
  

  //bzero(bigbuf, BUFSIZE);

  len = read(b->pipeFromPlayer[0], bigbuf, BUFSIZE-1);
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
    //printf("buf: %s\n", buf);

    if (strncmp(buf, "A: ", 3) == 0) {
      // audio clips:
      // A:  36.3 (36.2) of 418.0 (06:58.0)  1.5%
      // A:  79.2 (01:19.2) of 418.0 (06:58.0)  1.0%
      // video clips:
      // A:   0.6 V:   0.4 A-V:  0.166 ct:  0.040  13/ 13 ??% ??% ??,?% 7 0
      //printf(">> %s\n", buf);
      char *start;
      char *end;
      double ratio1, ratio2;
      unsigned int secpassed, sectotal;
      start = findNextNumber(buf + 3, 0);
      if (start) {
        ratio1 = (double) strtol(start, &end, 10);
        start = findNextNumber(end, 0);
        if (start) {
          ratio1 += (double) strtol(start, &end, 10) / 10.0;
          secpassed = (unsigned int) floor(ratio1);
          end = strstr(end, " of ");
          if (end) {
            start = findNextNumber(end, 0);
            ratio2 = (double) strtol(start, &end, 10);
            start = findNextNumber(end, 0);
            if (start) {
              ratio2 += (double) strtol(start, NULL, 10) / 10.0;
              sectotal = (unsigned int) floor(ratio2);
              SetCurrentPosition(b, (float)(ratio1/ratio2), secpassed, sectotal);
            }
          }
        }
      }
    } else if (strncmp(buf, " Artist: ", 9) == 0) {
      SetArtist(b, buf+9);
    } else if (strncmp(buf, " Title: ", 8) == 0) {
      SetSongName(b, buf+8);
    } else if (buf[0] == '@') {
      if(buf[1]=='P' && buf[3]=='0') {
        //if (!motherkilledit) nextSong(NULL, NULL);
        PlayingStopped(b);
      } else if(buf[1]=='R') {
        // @R MPG123
      } else if(buf[1]=='S') {
        // ?
      } else {
        //printf("INPUT: %s\n", buf);
      }
    } //else printf("UNKNOWN: %s\n", buf);
  }
}

void PlayingStopped(mplayerBackend *b) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    fePlayingStopped(f);
  }
}

void SetArtist(mplayerBackend *b, char *text) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetArtist(f, text);
  }
}

void SetSongName(mplayerBackend *b, char *text) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetSongName(f, text);
  }
}

void SetCurrentPosition(mplayerBackend *b, float r, unsigned int p, unsigned int t) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetCurrentPosition(f, r, p, t);
  }
}

void handleSigchld(int sig) {
  printf("SIGCHLD\n");
  if (firstBackend) {
    //PlayingStopped(firstBackend);
    firstBackend->childLives = False;
  }
}
