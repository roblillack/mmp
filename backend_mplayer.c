#include "mmp.h"

#include <assert.h>
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

  b->isPlaying = True;
}

void beStop(mplayerBackend* b) {
  if (b->childPid) {
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
  WMArray* types = WMCreateArray(10);
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

  char bigbuf[BUFSIZE], *buf, buf2[100], buf3[100], *laststop;
  char *str = NULL, *str_start = NULL, *str_end = NULL;
  int len, a;

  //for (a = sizeof(bigbuf) / sizeof(char); a > 0; a--) bigbuf[a-1]='\0';
  bzero(bigbuf, BUFSIZE);

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
  }
  
  //printf("read %d bytes. bigbuf: %s\n", len, bigbuf);
  for (buf = strtok_r(bigbuf, "\n", &laststop); buf;
       buf = strtok_r(NULL, "\n", &laststop)) {
    //printf("buf: %s\n", buf);

    if (strncmp(buf, "A: ", 3) == 0) {
      // A:  36.3 (36.2) of 418.0 (06:58.0)  1.5%
      printf(">> %s\n", buf);
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
          start = findNextNumber(end, 2);
          ratio2 = (double) strtol(start, &end, 10);
          start = findNextNumber(end, 0);
          if (start) {
            ratio2 += (double) strtol(start, NULL, 10) / 10.0;
            sectotal = (unsigned int) floor(ratio2);
            SetCurrentPosition(b, (float)(ratio1/ratio2), secpassed, sectotal);
          }
        }
      }
    } else if (buf[0] == '@') {
      if (buf[1] == 'F') {
        char *sep = NULL;
        //printf("%s\n", buf);
        //printf("%s / %s = ", strtok_r(buf+3, " \t", &sep), strtok_r(NULL, " \t", &sep));
        float ratio = (float) strtol(strtok_r(buf+3, " ", &sep), NULL, 10);
        ratio = (float) (ratio / (double) (ratio + (double)strtol(strtok_r(NULL, " ", &sep), NULL, 10)));
        unsigned int secpassed = (unsigned int) strtol(strtok_r(NULL, " ", &sep), NULL, 10);
        unsigned int sectotal = (unsigned int) (secpassed + strtol(strtok_r(NULL, " ", &sep), NULL, 10));
        //printf("%.03f\n", ratio);
        SetCurrentPosition(b, ratio, secpassed, sectotal);
      } else if (buf[1] == 'I') {
        /* format: "@I ID3:<30-chars-songname><30-chars-artist><32-chars-year???><genre>"
                           ^ buf+7            ^ buf+37         ^ buf+67          ^ buf+99 */
        if (strncmp(buf+3, "ID3", 3) == 0) {
          buf[36] = '\0';
          while (strlen(buf+7) > 0 && buf[strlen(buf)-1] == ' ') {
            buf[strlen(buf)-1] = '\0';
          }
          SetSongName(b, buf+7);
          buf[66] = '\0';
          while (strlen(buf+37) > 0 && buf[36+strlen(buf+37)] == ' ') {
            buf[36+strlen(buf+37)] = '\0';
          }
          SetArtist(b, buf+37);
        }
      } else if(buf[1]=='P' && buf[3]=='0') {
        //if (!motherkilledit) nextSong(NULL, NULL);
        PlayingStopped(b);
      } else if(buf[1]=='R') {
        // @R MPG123
      } else if(buf[1]=='S') {
        // ?
      } else {
        printf("INPUT: %s\n", buf);
      }
    } else printf("UNKNOWN: %s\n", buf);
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
  if (firstBackend) {
    PlayingStopped(firstBackend);
  }
}
