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

typedef struct mplayerBackend {
  // public
  WMArray* (*GetSupportedExtensions)();
      void (*Init)                  ();
      Bool (*IsPlaying)             ();
      void (*Play)                  (const char*);
      void (*StopNow)               ();
  // private
  Frontend *frontend;
  int pipeToPlayer[2], pipeFromPlayer[2];
  pid_t childPid;
  WMArray *clipInfo;
  WMHandlerID playerHandlerID;
  WMHandlerID signalHID;
  Bool found_artist;
  Bool playing_began;
  char codec_video[10];
  char codec_audio[10];
  long bitrate_video;
  long bitrate_audio;
} mplayerBackend;

mplayerBackend *mplayer_create(Frontend*);
WMArray *mplayer_getSupportedExtensions();
void mplayer_init();
Bool mplayer_isPlaying();
void mplayer_play(const char*);
void mplayer_stopNow();

void mplayer_stop();
void mplayer_cb_stopped(void*);
void mplayer_show_codec_info();
void mplayer_cleanup();
void mplayer_handle_input(int, int, void*);
char* mplayer_find_next_number(char*, unsigned int);

static mplayerBackend mplayer_backend;
// -----------------------------------------------------------------------------

mplayerBackend *mplayer_create(Frontend *f) {
  mplayer_backend.GetSupportedExtensions = mplayer_getSupportedExtensions;
  mplayer_backend.Init = mplayer_init;
  mplayer_backend.IsPlaying = mplayer_isPlaying;
  mplayer_backend.Play = mplayer_play;
  mplayer_backend.StopNow = mplayer_stopNow;

  mplayer_backend.frontend = f;
  mplayer_backend.clipInfo = NULL;
  mplayer_init();
  return &mplayer_backend;
}

Bool mplayer_isPlaying() {
  return (mplayer_backend.childPid > 0);
}

void mplayer_init() {
  FB("beInit");
  mplayer_backend.childPid = 0;
  mplayer_backend.signalHID = NULL;
  mplayer_backend.playerHandlerID = NULL;
  if (mplayer_backend.clipInfo) WMFreeArray(mplayer_backend.clipInfo);
  mplayer_backend.clipInfo = WMCreateArray(0);
  mplayer_backend.found_artist = False;
  mplayer_backend.playing_began = False;
  mplayer_backend.codec_video[0] = '\0';
  mplayer_backend.codec_audio[0] = '\0';
  mplayer_backend.bitrate_video = 0;
  mplayer_backend.bitrate_audio = 0;
  FE("beInit");
}

void mplayer_play(const char *filename) {
  FB("bePlay");

  /* should we're playing st. stop it */
  if (mplayer_backend.childPid) {
    D1("bePlay needs to Stop child process.\n");
    mplayer_stopNow();
  }

  /* create a pipe for the child to talk to us. */
  if (pipe(mplayer_backend.pipeFromPlayer) == -1 || pipe(mplayer_backend.pipeToPlayer) == -1) {
    perror("[mmp] could not set up ipc.");
    return;
  }

  /* set up a handler for the reading pipe */
  mplayer_backend.playerHandlerID = WMAddInputHandler(mplayer_backend.pipeFromPlayer[0], 1,
                                                      mplayer_handle_input, NULL);
  mplayer_backend.signalHID = WMAddSignalHandler(SIGCHLD, mplayer_cb_stopped, NULL);

  /* now, start the new process */
  mplayer_backend.childPid = fork();
  if (mplayer_backend.childPid == 0) {
    /* redirect stdout to pipeFromPlayer */
    close(1);
    dup(mplayer_backend.pipeFromPlayer[1]);
    close(mplayer_backend.pipeFromPlayer[1]);
    /* don't need this end of the pipe. */
    close(mplayer_backend.pipeFromPlayer[0]);
    /* close stdin to pipeToPlayer */
    close(0);
    dup(mplayer_backend.pipeToPlayer[0]);
    close(mplayer_backend.pipeToPlayer[0]);
    close(mplayer_backend.pipeToPlayer[1]);
    execlp("mplayer", "mplayer", "-framedrop", "-identify", filename, NULL);
    perror("[mmp] error running mplayer\n");
    return;
  } else if (mplayer_backend.childPid == -1) {
    printf("[mmp] error. could not fork....\n");
    mplayer_backend.childPid = 0;
    close(mplayer_backend.pipeFromPlayer[0]);
    close(mplayer_backend.pipeToPlayer[1]);
  }

  /* we don't need to write to this pipe */
  close(mplayer_backend.pipeFromPlayer[1]);
  close(mplayer_backend.pipeToPlayer[0]);

  FE("bePlay");
}

void mplayer_show_codec_info() {
  if (!mplayer_backend.found_artist) {
    char info[100];
    info[0] = info[99] = '\0';
#define add_info(src) strncat(info, src, sizeof(info)-strlen(info)-1)
    if (mplayer_backend.codec_audio[0] != '\0' || mplayer_backend.bitrate_audio > 0) {
      if (mplayer_backend.codec_audio[0] != '\0') {
        if (strncmp(mplayer_backend.codec_audio, "85", 2) == 0) {
          add_info("MPEG Layer 3 Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "80", 2) == 0) {
          add_info("MPEG Layer 1/2 Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "1", 1) == 0) {
          add_info("PCM Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "2", 1) == 0) {
          add_info("ADPCM Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "mp4a", 4) == 0) {
          add_info("MPEG 4 Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "353", 3) == 0) {
          add_info("Windows Media Audio v2");
        } else if (strncmp(mplayer_backend.codec_audio, "8192", 4) == 0) {
          add_info("AC3 Audio");
        } else if (strncmp(mplayer_backend.codec_audio, "QDM2", 4) == 0) {
          add_info("QDesign Music v2 Audio");
        } else {
          add_info(mplayer_backend.codec_audio);
        }
      }
      if (mplayer_backend.bitrate_audio > 0) {
        snprintf(info+strlen(info), sizeof(info)-strlen(info),
                 " (%ukbit/s)", mplayer_backend.bitrate_audio/1000);
      }
      if (mplayer_backend.codec_video[0] != '\0' || mplayer_backend.bitrate_video > 0) {
        add_info(" / ");
      }
    }
    if (mplayer_backend.codec_video[0] != '\0' || mplayer_backend.bitrate_video > 0) {
      if (mplayer_backend.codec_video[0] != '\0') {
        if (strncmp(mplayer_backend.codec_video, "FLV1", 4) == 0) {
          add_info("Flash Video");
        } else if (strncmp(mplayer_backend.codec_video, "XVID", 4) == 0) {
          add_info("Xvid Video");
        } else if (strncmp(mplayer_backend.codec_video, "s263", 4) == 0) {
          add_info("H.263+ video");
        } else if (strncmp(mplayer_backend.codec_video, "0x10000001", 10) == 0) {
          add_info("MPEG 1 Video");
        } else if (strncmp(mplayer_backend.codec_video, "0x10000002", 10) == 0) {
          add_info("MPEG 2 Video");
        } else if (strncmp(mplayer_backend.codec_video, "mp4v", 4) == 0) {
          add_info("MPEG 4 Video");
        } else if (strncmp(mplayer_backend.codec_video, "MJPG", 4) == 0) {
          add_info("Motion JPEG video");
        } else if (strncmp(mplayer_backend.codec_video, "MP42", 4) == 0) {
          add_info("Microsoft MPEG 4 v2");
        } else if (strncmp(mplayer_backend.codec_video, "MP43", 4) == 0) {
          add_info("Microsoft MPEG 4 v3");
        } else if (strncmp(mplayer_backend.codec_video, "DIV3", 4) == 0) {
          add_info("Original DivX ;-) Video");
        } else if (strncmp(mplayer_backend.codec_video, "DIVX", 4) == 0) {
          add_info("DivX Video");
        } else if (strncmp(mplayer_backend.codec_video, "DX50", 4) == 0) {
          add_info("DivX 5.0 Video");
        } else if (strncmp(mplayer_backend.codec_video, "CRAM", 4) == 0) {
          add_info("Microsoft Video 1");
        } else if (strncmp(mplayer_backend.codec_video, "WMV1", 4) == 0) {
          add_info("Windows Media Video v1");
        } else if (strncmp(mplayer_backend.codec_video, "WMV3", 4) == 0) {
          add_info("Windows Media Video v3");
        } else if (strncmp(mplayer_backend.codec_video, "SVQ3", 4) == 0) {
          add_info("Sorenson Video v3");
        } else {
          add_info(mplayer_backend.codec_video);
        }
      }
      if (mplayer_backend.bitrate_video > 0) {
        snprintf(info+strlen(info), sizeof(info)-strlen(info),
                 " (%u kbit/s)", mplayer_backend.bitrate_video/1000);
      }
    }
    if (info[0] != '\0') {
      feSetArtist(mplayer_backend.frontend, info);
    }
  }     
}

void mplayer_cb_stopped(void *cdata) {
  FB("cbStopped");
  if (mplayer_backend.childPid) {
    mplayer_cleanup();
    WMAddIdleHandler(fePlayingStopped, mplayer_backend.frontend);
  }
  FE("cbStopped");
}

void mplayer_stop() {
  FB("beStop");
  if (mplayer_backend.childPid) {
    D1("sending SIGINT...\n");
    kill(mplayer_backend.childPid, SIGINT);
  }
  FE("beStop\n");
}

void mplayer_stopNow() {
  FB("beStopNow");
  if (mplayer_backend.childPid) {
    kill(mplayer_backend.childPid, SIGINT);
    int status;
    int i = 0;
    do {
      wait(&status);
      if (i == 100) kill(mplayer_backend.childPid, SIGTERM);
      if (i == 200) kill(mplayer_backend.childPid, SIGKILL);
      D1("waiting...\n");
      i++;
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    mplayer_cleanup();
  }
  FE("beStopNow");
}

void mplayer_cleanup() {
  FB("cleanUpChild");
  close(mplayer_backend.pipeFromPlayer[0]);
  close(mplayer_backend.pipeToPlayer[1]);
  WMDeleteInputHandler(mplayer_backend.playerHandlerID);
  WMDeletePipedSignalHandler(mplayer_backend.signalHID);
  mplayer_init();
  FE("cleanUpChild");
}

WMArray* mplayer_getSupportedExtensions() {
  WMArray* types = WMCreateArray(17);
  WMAddToArray(types, "3gp");
  WMAddToArray(types, "aac");
  WMAddToArray(types, "aiff");
  WMAddToArray(types, "avi");
  WMAddToArray(types, "flac");
  WMAddToArray(types, "flv");
  WMAddToArray(types, "m4a");
  WMAddToArray(types, "m4v");
  WMAddToArray(types, "mov");
  WMAddToArray(types, "mp3");
  WMAddToArray(types, "mpeg");
  WMAddToArray(types, "mpg");
  WMAddToArray(types, "ogg");
  WMAddToArray(types, "ogm");
  WMAddToArray(types, "wav");
  WMAddToArray(types, "wma");
  WMAddToArray(types, "wmv");
  return types;
}

// -----------------------------------------------------------------------------

char *mplayer_find_next_number(char *text, unsigned int ignore) {
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

void mplayer_handle_input(int fd, int mask, void *clientData) {
  //FB("handlePlayerInput");
#define BUFSIZE 8192

  char bigbuf[BUFSIZE];   // the whole buffer
  char *buf;              // pointer the line we work with
  char *laststop = NULL;
  char *start;
  int len;

  if (mplayer_backend.childPid == 0) {
    D1("no child alive in handlePlayerInput.\n");
    return;
  }

  len = read(mplayer_backend.pipeFromPlayer[0], bigbuf, BUFSIZE-1);
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

    if (strncmp(buf, "A:", 2) == 0 || strncmp(buf, "V:", 2) == 0) {
      if (!mplayer_backend.playing_began) {
        mplayer_show_codec_info();
        mplayer_backend.playing_began = True;
      }
      start = mplayer_find_next_number(buf+2, 0);
      if (start) {
        feSetCurrentPosition(mplayer_backend.frontend, strtol(start, NULL, 10));
      }
    /*} else if (strncmp(buf, "VIDEO: ", 7) == 0 || strncmp(buf, "AUDIO: ", 7) == 0) {
      feSetArtist(mplayer_backend.frontend, buf+8);*/
    } else if (strncmp(buf, "ID_", 3) == 0) {
      if (strncmp(buf+3, "LENGTH=", 7) == 0) {
        start = mplayer_find_next_number(buf+3, 0);
        feSetFileLength(mplayer_backend.frontend, strtol(start, NULL, 10));
      } else if (strncmp(buf+3, "CLIP_INFO", 9) == 0) {
        if (strncmp(buf+12, "_NAME", 5) == 0) {
          start = mplayer_find_next_number(buf+17, 0);
          if (!start) continue;
          int index = (int) strtol(start, &start, 10);
          if (*start != '=') continue;
          //printf("adding %s\n", start+1);
          start = WMReplaceInArray(mplayer_backend.clipInfo, index, wstrdup(start+1));
          ucfree(start);
        } else if (strncmp(buf+12, "_VALUE", 6) == 0) {
          start = mplayer_find_next_number(buf+18, 0);
          if (!start) continue;
          int index = (int) strtol(start, &start, 10);
          if (*start != '=' || strlen(start+1) == 0) continue;
          //printf("getting %i: %s\n", index, start+1);
          if (strncmp(WMGetFromArray(mplayer_backend.clipInfo, index), "Artist", 6) == 0) {
            feSetArtist(mplayer_backend.frontend, start+1);
            mplayer_backend.found_artist = True;
          } else if (strncmp(WMGetFromArray(mplayer_backend.clipInfo, index), "Title", 5) == 0 ||
                     strncmp(WMGetFromArray(mplayer_backend.clipInfo, index), "Name", 4) == 0) {
            feSetTitle(mplayer_backend.frontend, start+1);
          }
        }
      } else if (strncmp(buf+3, "VIDEO_FORMAT=", 13) == 0) {
        strncpy(mplayer_backend.codec_video, buf+16, sizeof(mplayer_backend.codec_video));
      } else if (strncmp(buf+3, "AUDIO_FORMAT=", 13) == 0) {
        strncpy(mplayer_backend.codec_audio, buf+16, sizeof(mplayer_backend.codec_audio));
      } else if (strncmp(buf+3, "VIDEO_BITRATE=", 14) == 0) {
        start = mplayer_find_next_number(buf+17, 0);
        if (start) mplayer_backend.bitrate_video = strtol(start, NULL, 10);
      } else if (strncmp(buf+3, "AUDIO_BITRATE=", 14) == 0) {
        start = mplayer_find_next_number(buf+17, 0);
        if (start) mplayer_backend.bitrate_audio = strtol(start, NULL, 10);
      }
    }
  }
}
