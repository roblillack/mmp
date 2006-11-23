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

typedef struct Backend {
  WMArray *frontends;
  int pipeToPlayer[2],
      pipeFromPlayer[2];
  pid_t childPid;
  Bool childLives;
  Bool forcedKill;
  Bool isPlaying;
  WMArray *clipInfo;
  WMHandlerID playerHandlerID;
} mplayerBackend;

//static mplayerBackend *firstBackend = NULL;
void handleSigchld(int);
void handlePlayerInput(int, int, void*);
void PlayingStopped(mplayerBackend*);
void SetArtist(mplayerBackend*, char*);
void SetSongName(mplayerBackend*, char*);
void SetFileLength(mplayerBackend*, long);
void SetCurrentPosition(mplayerBackend*, long);

// -----------------------------------------------------------------------------

Backend *beCreate() {
  mplayerBackend *b = wmalloc(sizeof(mplayerBackend));
  b->frontends = WMCreateArray(0);
  b->clipInfo = NULL;
  return b;
}

Bool beIsPlaying(mplayerBackend *b) {
  return (b->childPid > 0);
}

Bool beInit(mplayerBackend *b) {
  b->childPid = 0;
  if (b->clipInfo) WMFreeArray(b->clipInfo);
  b->clipInfo = WMCreateArray(0);
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
  /* should we're playing st. stop it */
  beStop(b);

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
    execlp("mplayer", "mplayer", "-framedrop", "-identify", filename, NULL);
    perror("[mmp] error running mplayer\n");
    return;
  } else if (b->childPid == -1) {
    printf("[mmp] error. could not fork....\n");
    b->childPid = 0;
  }

  /* we don't need to write to this pipe */
  close(b->pipeFromPlayer[1]);
  close(b->pipeToPlayer[0]);
}

void beStop(mplayerBackend* b) {
  if (b->childPid) {
    b->forcedKill = True;
    kill(b->childPid, SIGINT);
    int status;
    if (waitpid(b->childPid, &status, WNOHANG) == 0) {
      sleep(1);
      if (b->childPid == 0) return;
      kill(b->childPid, SIGTERM);
      if (waitpid(b->childPid, &status, WNOHANG) == 0) {
        sleep(1);
        if (b->childPid == 0) return;
        kill(b->childPid, SIGKILL);
      }
    }
  }
}

void beHandleSigChild(mplayerBackend* b) {
  close(b->pipeFromPlayer[0]);
  close(b->pipeToPlayer[1]);
  WMDeleteInputHandler(b->playerHandlerID);
  b->childPid = 0;
  beInit(b);
  PlayingStopped(b);
}

WMArray* beGetSupportedExtensions(Backend* b) {
  WMArray* types = WMCreateArray(12);
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
  WMAddToArray(types, "3gp");
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
  char *start;
  int len;;
  
  if (b->childLives == False) {
    PlayingStopped(b);
  }
  
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

    if (strncmp(buf, "A: ", 3) == 0 || strncmp(buf, "V: ", 3) == 0) {
      start = findNextNumber(buf+3, 0);
      if (start) {
        SetCurrentPosition(b, strtol(start, NULL, 10));
      }
    } else if (strncmp(buf, "ID_", 3) == 0) {
      if (strncmp(buf+3, "LENGTH=", 7) == 0) {
        start = findNextNumber(buf+3, 0);
        SetFileLength(b, strtol(start, NULL, 10));
      } else if (strncmp(buf+3, "CLIP_INFO", 9) == 0) {
        if (strncmp(buf+12, "_NAME", 5) == 0) {
          start = findNextNumber(buf+17, 0);
          if (!start) continue;
          int index = (int) strtol(start, &start, 10);
          if (*start != '=') continue;
          //printf("adding %s\n", start+1);
          start = WMReplaceInArray(b->clipInfo, index, wstrdup(start+1));
          ucfree(start);
        } else if (strncmp(buf+12, "_VALUE", 6) == 0) {
          start = findNextNumber(buf+18, 0);
          if (!start) continue;
          int index = (int) strtol(start, &start, 10);
          if (*start != '=') continue;
          //printf("getting %i: %s\n", index, start+1);
          if (strncmp(WMGetFromArray(b->clipInfo, index), "Artist", 6) == 0) {
            SetArtist(b, start+1);
          } else if (strncmp(WMGetFromArray(b->clipInfo, index), "Title", 5) == 0) {
            SetSongName(b, start+1);
          }
        }
      } else {
        //printf("unknown: %s\n", buf+3);
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

void SetCurrentPosition(mplayerBackend *b, long p) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetCurrentPosition(f, p);
  }
}

void SetFileLength(mplayerBackend *b, long l) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetFileLength(f, l);
  }
}


