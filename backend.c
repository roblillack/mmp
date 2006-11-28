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

typedef struct Backend {
  WMArray *frontends;
  int pipeToPlayer[2],
      pipeFromPlayer[2];
  pid_t childPid;
  WMHandlerID playerHandlerID;
  Bool gotSigChild;
} myBackend;

static myBackend *firstBackend = NULL;
void handlePlayerInput(int, int, void*);
void PlayingStopped(myBackend*);
void SetArtist(myBackend*, char*);
void SetSongName(myBackend*, char*);
void SetCurrentPosition(myBackend*, long);
void SetFileLength(myBackend*, long);

// -----------------------------------------------------------------------------

Backend *beCreate() {
  myBackend *b = wmalloc(sizeof(myBackend));
  b->frontends = WMCreateArray(0);
  return b;
}

Bool beInit(myBackend *b) {
  // search for mpg123....
  b->childPid = 0;
  b->gotSigChild = False;
  return True;
}

void beAddFrontend(myBackend *b, Frontend *f) {
  if (WMFindInArray(b->frontends, NULL, f) == WANotFound) {
    WMAddToArray(b->frontends, f);
    feAddBackend(f, b);
  }
}

Bool beIsPlaying(myBackend *b) {
  return (b->childPid > 0);
}

void beRemoveFrontend(myBackend *b, Frontend *f) {
  int index;
  if ((index = WMFindInArray(b->frontends, NULL, f)) != WANotFound) {
    WMDeleteFromArray(b->frontends, index);
    feRemoveBackend(f, b);
  }
}

void bePlay(myBackend *b, char *filename) {
  char buf[PATH_MAX];

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
    execlp("mpg123", "mpg123", "-R", "/dev/null", NULL);
    perror("[mmp] error running mpg123\n");
    return;
  } else if (b->childPid == -1) {
    printf("[mmp] error. could not fork....\n");
    b->childPid=0;
  }

  /* we don't need to write to this pipe */
  close(b->pipeFromPlayer[1]);
  close(b->pipeToPlayer[0]);

  /* now send the file to play to the child process */
  snprintf(buf, PATH_MAX, "L %s\n", filename);
  write(b->pipeToPlayer[1], buf, strlen(buf));
}

void beStop(myBackend* b) {
  if (b->childPid) {
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

void beGotSigChild(myBackend *b) {
  if (b) {
    b->gotSigChild = True;
  }
}

void beHandleSigChild(myBackend* b) {
  if (b->gotSigChild) {
    close(b->pipeFromPlayer[0]);
    close(b->pipeToPlayer[1]);
    WMDeleteInputHandler(b->playerHandlerID);
    b->childPid = 0;
    beInit(b);
    PlayingStopped(b);
  }
}

WMArray* beGetSupportedExtensions(Backend* b) {
  WMArray* types = WMCreateArray(1);
  WMAddToArray(types, "mp3");
  return types;
}

// -----------------------------------------------------------------------------

void handlePlayerInput(int fd, int mask, void *clientData) {
  myBackend *b = (myBackend*)clientData;
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
    if (buf[0] == '@') {
      if (buf[1] == 'F') {
        char *sep = NULL;
        //printf("%s\n", buf);
        //printf("%s / %s = ", strtok_r(buf+3, " \t", &sep), strtok_r(NULL, " \t", &sep));
        float ratio = (float) strtol(strtok_r(buf+3, " ", &sep), NULL, 10);
        ratio = (float) (ratio / (double) (ratio + (double)strtol(strtok_r(NULL, " ", &sep), NULL, 10)));
        unsigned int secpassed = (unsigned int) strtol(strtok_r(NULL, " ", &sep), NULL, 10);
        unsigned int sectotal = (unsigned int) (secpassed + strtol(strtok_r(NULL, " ", &sep), NULL, 10));
        //printf("%.03f\n", ratio);
        SetFileLength(b, sectotal);
        SetCurrentPosition(b, secpassed);
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
        beStop(b);
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

void PlayingStopped(myBackend *b) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    fePlayingStopped(f);
  }
}

void SetArtist(myBackend *b, char *text) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetArtist(f, text);
  }
}

void SetSongName(myBackend *b, char *text) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetSongName(f, text);
  }
}

void SetFileLength(myBackend *b, long l) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetFileLength(f, l);
  }
}

void SetCurrentPosition(myBackend *b, long p) {
  Frontend *f;
  WMArrayIterator i;

  WM_ITERATE_ARRAY(b->frontends, f, i) {
    feSetCurrentPosition(f, p);
  }
}
