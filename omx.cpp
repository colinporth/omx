// omx.cpp
//{{{  includes
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>

#include <string>
#include <chrono>
#include <thread>

#include "../shared/utils/date.h"
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "../shared/utils/cSemaphore.h"
#include "../shared/utils/cKeyboard.h"
#include "../shared/dvb/cDvb.h"

#include "bcm_host.h"

#define AV_NOWARN_DEPRECATED
#include "avLibs.h"

#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxAv.h"

#include "../shared/nanoVg/cRaspWindow.h"
#include "../shared/widgets/cTextBox.h"
#include "../shared/widgets/cListWidget.h"
#include "../shared/widgets/cTextBox.h"
#include "../shared/widgets/cTransportStreamBox.h"
#include "../shared/widgets/cTimecodeBox.h"

#include "version.h"

using namespace std;
//}}}

volatile sig_atomic_t gAbort = false;
//{{{
void sigHandler (int sig) {

  if (sig == SIGINT && !gAbort) {
    signal (SIGINT, SIG_DFL);
    gAbort = true;
    return;
    }

  signal (SIGABRT, SIG_DFL);
  signal (SIGSEGV, SIG_DFL);
  signal (SIGFPE, SIG_DFL);
  abort();
  }
//}}}

class cAppWindow : public cRaspWindow {
public:
  //{{{
  cAppWindow (const string& root) : mDvb(root), mRoot(root) {

    mDebugStr = "omx " + root + " " + string(VERSION_DATE);

    mKeyboard.setKeymap (cKeyConfig::getKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (const string& inTs, int frequency) {

    initialise (1.f, 0);
    setChangeCountDown (10);

    add (new cTextBox (mDebugStr, 0.f));
    if (frequency) {
      add (new cTextBox (mDvb.mPacketStr, 15.f));
      add (new cTextBox (mDvb.mSignalStr, 14.f));
      add (new cTextBox (mDvb.mTuneStr, 13.f));
      mTsBox = add (new cTransportStreamBox (&mDvb.mTs, 0.f,-2.f));
      }
    float list = frequency ? 2.f : 1.0f;
    mListWidget = addAt (new cListWidget (mFileNames, mFileNum, mFileChanged, 0.f,-list), 0.f,list);
    addBottomRight (new cTimecodeBox (mPlayPts, mLengthPts, 17.f, 2.f));

    updateFileNames();

    thread dvbCaptureThread;
    thread dvbGrabThread;
    if (frequency) {
      // launch dvbThread
      dvbCaptureThread = thread ([=]() { mDvb.captureThread (frequency); });
      sched_param sch_params;
      sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
      pthread_setschedparam (dvbCaptureThread.native_handle(), SCHED_RR, &sch_params);
      dvbCaptureThread.detach();

      dvbGrabThread = thread ([=]() { mDvb.grabThread(); } );
      sch_params.sched_priority = sched_get_priority_max (SCHED_RR)-1;
      pthread_setschedparam (dvbGrabThread.native_handle(), SCHED_RR, &sch_params);
      dvbGrabThread.detach();
      }
    else if (!inTs.empty())
      thread ([=]() { mDvb.readThread (inTs); } ).detach();

    thread ([=]() { player (mFileNames[mFileNum]); } ).detach();

    cRaspWindow::run();
    }
  //}}}
  cOmxVideoConfig mVideoConfig;
  cOmxAudioConfig mAudioConfig;

protected:
  //{{{
  class cKeyConfig {
  public:
    //{{{  keys
    #define KEY_ENTER    0x0a
    #define KEY_ESC      0x1b

    #define KEY_HOME     0x317e
    #define KEY_INSERT   0x327e
    #define KEY_DELETE   0x337e
    #define KEY_END      0x347e
    #define KEY_PAGEUP   0x357e
    #define KEY_PAGEDOWN 0x367e

    #define KEY_UP       0x5b41
    #define KEY_DOWN     0x5b42
    #define KEY_RIGHT    0x5b43
    #define KEY_LEFT     0x5b44
    //}}}
    //{{{  actions
    enum eKeyAction {
      ACT_NONE,
      ACT_EXIT,

      ACT_PREV_FILE, ACT_NEXT_FILE, ACT_ENTER,
      ACT_PLAYPAUSE, ACT_STEP,
      ACT_SEEK_DEC_SMALL, ACT_SEEK_INC_SMALL,
      ACT_SEEK_DEC_LARGE, ACT_SEEK_INC_LARGE,
      ACT_DEC_VOLUME, ACT_INC_VOLUME,
      ACT_TOGGLE_TS, ACT_TOGGLE_LIST,

      ACT_TOGGLE_VSYNC, ACT_TOGGLE_PERF, ACT_TOGGLE_STATS, ACT_TOGGLE_TESTS,
      ACT_TOGGLE_SOLID, ACT_TOGGLE_EDGES, ACT_TOGGLE_TRIANGLES,
      ACT_LESS_FRINGE, ACT_MORE_FRINGE,

      ACT_LOG1, ACT_LOG2, ACT_LOG3, ACT_LOG4, ACT_LOG5, ACT_LOG6,
      };
    //}}}
    //{{{
    static map<int,int> getKeymap() {

      map<int,int> keymap;

      keymap[KEY_ESC] = ACT_EXIT;

      keymap[KEY_UP]    = ACT_PREV_FILE;
      keymap[KEY_DOWN]  = ACT_NEXT_FILE;
      keymap[KEY_ENTER] = ACT_ENTER;

      keymap[' '] = ACT_PLAYPAUSE;
      keymap['>'] = ACT_STEP;
      keymap['.'] = ACT_STEP;

      keymap[KEY_LEFT]  = ACT_SEEK_DEC_SMALL;
      keymap[KEY_RIGHT] = ACT_SEEK_INC_SMALL;
      keymap[KEY_PAGEUP]   = ACT_SEEK_DEC_LARGE;
      keymap[KEY_PAGEDOWN] = ACT_SEEK_INC_LARGE;

      keymap['-'] = ACT_DEC_VOLUME;
      keymap['+'] = ACT_INC_VOLUME;
      keymap['='] = ACT_INC_VOLUME;

      keymap['g'] = ACT_TOGGLE_TS;
      keymap['G'] = ACT_TOGGLE_TS;
      keymap['f'] = ACT_TOGGLE_LIST;
      keymap['F'] = ACT_TOGGLE_LIST;

      keymap['v'] = ACT_TOGGLE_VSYNC;
      keymap['V'] = ACT_TOGGLE_VSYNC;
      keymap['p'] = ACT_TOGGLE_PERF;
      keymap['P'] = ACT_TOGGLE_PERF;
      keymap['t'] = ACT_TOGGLE_STATS;
      keymap['T'] = ACT_TOGGLE_STATS;
      keymap['e'] = ACT_TOGGLE_TESTS;
      keymap['E'] = ACT_TOGGLE_TESTS;

      keymap['s'] = ACT_TOGGLE_SOLID;
      keymap['S'] = ACT_TOGGLE_SOLID;
      keymap['a'] = ACT_TOGGLE_EDGES;
      keymap['A'] = ACT_TOGGLE_EDGES;
      keymap['d'] = ACT_TOGGLE_TRIANGLES;
      keymap['D'] = ACT_TOGGLE_TRIANGLES;
      keymap['q'] = ACT_LESS_FRINGE;
      keymap['Q'] = ACT_LESS_FRINGE;
      keymap['w'] = ACT_MORE_FRINGE;
      keymap['W'] = ACT_MORE_FRINGE;

      keymap['1'] = ACT_LOG1;
      keymap['2'] = ACT_LOG2;
      keymap['3'] = ACT_LOG3;
      keymap['4'] = ACT_LOG4;
      keymap['5'] = ACT_LOG5;
      keymap['6'] = ACT_LOG6;

      return keymap;
      }
    //}}}
    };
  //}}}
  //{{{
  void pollKeyboard() {

    //cLog::log (LOGINFO, "pollKeyboard");
    switch (mKeyboard.getEvent()) {
      //{{{
      case cKeyConfig::ACT_PREV_FILE:
        if (mFileNum > 0) {
          mFileNum--;
          mFileChanged = true;
          updateFileNames();
          }
        if (mListWidget)
          mListWidget->setVisible (true);
        changed();
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_NEXT_FILE:
        if (mFileNum < mFileNames.size()-1) {
          mFileNum++;
          mFileChanged = true;
          updateFileNames();
          }
        if (mListWidget)
          mListWidget->setVisible (true);
        changed();
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_ENTER:
        cLog::log (LOGNOTICE, "enter");
        mEntered = true;
        if (mListWidget)
          mListWidget->setVisible (false);
        changed();
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_TOGGLE_LIST:
        if (mListWidget) {
          mListWidget->setVisible (!mListWidget->isVisible());
          changed();
          }
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_TOGGLE_TS:
        if (mTsBox) {
          mTsBox->setVisible (!mTsBox->isVisible());
          changed();
          }
        break;
      //}}}

      case cKeyConfig::ACT_PLAYPAUSE: mPause = !mPause; break;
      case cKeyConfig::ACT_STEP: mOmxClock.step (1); break;
      case cKeyConfig::ACT_SEEK_DEC_SMALL: mSeekIncSec = -10.0; break;
      case cKeyConfig::ACT_SEEK_INC_SMALL: mSeekIncSec = +10.0; break;
      case cKeyConfig::ACT_SEEK_DEC_LARGE: mSeekIncSec = -60.0; break;
      case cKeyConfig::ACT_SEEK_INC_LARGE: mSeekIncSec = +60.0; break;

      //{{{
      case cKeyConfig::ACT_DEC_VOLUME:
        if (mOmxAudioPlayer)
          mOmxAudioPlayer->setVolume (mOmxAudioPlayer->getVolume()*4.f/5.f);
        changed();
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_INC_VOLUME:
        if (mOmxAudioPlayer)
          mOmxAudioPlayer->setVolume (mOmxAudioPlayer->getVolume()*5.f/4.f);
        changed();
        break;
      //}}}

      case cKeyConfig::ACT_TOGGLE_VSYNC: toggleVsync(); changed(); break; // v
      case cKeyConfig::ACT_TOGGLE_PERF:  togglePerf();  changed(); break; // p
      case cKeyConfig::ACT_TOGGLE_STATS: toggleStats(); changed(); break; // t
      case cKeyConfig::ACT_TOGGLE_TESTS: toggleTests(); changed(); break; // e

      case cKeyConfig::ACT_TOGGLE_SOLID: toggleSolid(); break;            // s
      case cKeyConfig::ACT_TOGGLE_EDGES: toggleEdges(); break;            // a
      case cKeyConfig::ACT_TOGGLE_TRIANGLES: toggleTriangles(); break;    // d
      case cKeyConfig::ACT_LESS_FRINGE: fringeWidth (getFringeWidth() - 0.25f); changed(); break; // q
      case cKeyConfig::ACT_MORE_FRINGE: fringeWidth (getFringeWidth() + 0.25f); changed(); break; // w

      case cKeyConfig::ACT_LOG1: cLog::setLogLevel (LOGNOTICE); break;
      case cKeyConfig::ACT_LOG2: cLog::setLogLevel (LOGERROR); break;
      case cKeyConfig::ACT_LOG3: cLog::setLogLevel (LOGINFO); break;
      case cKeyConfig::ACT_LOG4: cLog::setLogLevel (LOGINFO1); break;
      case cKeyConfig::ACT_LOG5: cLog::setLogLevel (LOGINFO2); break;
      case cKeyConfig::ACT_LOG6: cLog::setLogLevel (LOGINFO3); break;

      case cKeyConfig::ACT_EXIT: gAbort = true; mExit = true; break;
      }
    }
  //}}}

private:
  //{{{
  static int addFile (const char* fileName, const struct stat* statBuf, int flag, struct FTW* ftw) {

    // ftw->base - offset of base in path
    // (intmax_t)statBuf->st_size,
    if (flag == FTW_F)
      mFileNames.push_back (fileName);
    return 0;
    }
  //}}}
  //{{{
  void updateFileNames() {

    mFileNames.clear();
    nftw (mRoot.c_str(), addFile, 20, 0);
    changed();
    }
  //}}}

  //{{{
  void player (string fileName) {

    cLog::setThreadName ("play");

    //{{{  set videoConfig aspect
    TV_DISPLAY_STATE_T state;
    memset (&state, 0, sizeof(TV_DISPLAY_STATE_T));
    vc_tv_get_display_state (&state);

    switch ((HDMI_ASPECT_T)state.display.hdmi.aspect_ratio) {
      case HDMI_ASPECT_4_3:   mVideoConfig.mDisplayAspect =  4.f / 3.f;  break;
      case HDMI_ASPECT_14_9:  mVideoConfig.mDisplayAspect = 14.f / 9.f;  break;
      case HDMI_ASPECT_5_4:   mVideoConfig.mDisplayAspect =  5.f / 4.f;  break;
      case HDMI_ASPECT_16_10: mVideoConfig.mDisplayAspect = 16.f / 10.f; break;
      case HDMI_ASPECT_15_9:  mVideoConfig.mDisplayAspect = 15.f /  9.f; break;
      case HDMI_ASPECT_64_27: mVideoConfig.mDisplayAspect = 64.f / 27.f; break;
      case HDMI_ASPECT_16_9:
      default:                mVideoConfig.mDisplayAspect = 16.f /  9.f; break;
      }

    mVideoConfig.mDisplayAspect *= (float)state.display.hdmi.height / (float)state.display.hdmi.width;
    //}}}
    //{{{  create 1x1 black pixel, added to display just behind video
    auto display = vc_dispmanx_display_open (mVideoConfig.mDisplay);

    uint32_t vc_image_ptr;
    auto resource = vc_dispmanx_resource_create (VC_IMAGE_ARGB8888, 1, 1, &vc_image_ptr);

    uint32_t rgba = 0;
    VC_RECT_T dstRect;
    vc_dispmanx_rect_set (&dstRect, 0, 0, 1, 1);
    vc_dispmanx_resource_write_data (resource, VC_IMAGE_ARGB8888, sizeof(rgba), &rgba, &dstRect);

    VC_RECT_T srcRect;
    vc_dispmanx_rect_set (&srcRect, 0,0, 1<<16,1<<16);
    vc_dispmanx_rect_set (&dstRect, 0,0, 0,0);

    auto update = vc_dispmanx_update_start (0);
    vc_dispmanx_element_add (update, display, -1, &dstRect, resource, &srcRect,
                             DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_STEREOSCOPIC_MONO);

    vc_dispmanx_update_submit_sync (update);
    //}}}

    bool ok = true;
    while (ok) {
      cLog::log (LOGINFO, "opening " + fileName);
      if (mOmxReader.open (fileName, false, true, 5.f, "","","probesize:1000000","")) {
        cLog::log (LOGINFO, "opened " + fileName);
        beginPlay();
        playLoop();
        endPlay();
        mOmxReader.close();
        }

      updateFileNames();
      if (mExit || gAbort)
        ok = false;
      else if (mEntered) {
        mEntered = false;
        mPause = false;
        }
      else if (mFileNum >= mFileNames.size()-1)
        ok = false;
      else
        mFileNum++;
      fileName = mFileNames[mFileNum];
      }

    cLog::log (LOGNOTICE, "player - exit");

    // make sure everybody sees exit
    mExit = true;
    }
  //}}}
  //{{{
  void beginPlay() {

    mOmxClock.stateIdle();
    mOmxClock.stop();
    mOmxClock.pause();
    //mOmxClock.setSpeed (4.0, false);
    //mOmxReader.setSpeed (4.0);

    // get video streams,config and start videoPlayer
    if (mOmxReader.getVideoStreamCount())
      mOmxVideoPlayer = new cOmxVideoPlayer();
    mOmxReader.getHints (OMXSTREAM_VIDEO, mVideoConfig.mHints);

    if (mOmxVideoPlayer) {
      if (mOmxVideoPlayer->open (&mOmxClock, mVideoConfig))
        thread ([=]() { mOmxVideoPlayer->run ("vid "); } ).detach();
      else {
        delete (mOmxVideoPlayer);  // crashes
        mOmxVideoPlayer = nullptr;
        }
      }

    // get audio streams,config and start audioPlayer
    if (mOmxReader.getAudioStreamCount())
      mOmxAudioPlayer = new cOmxAudioPlayer();
    mOmxReader.getHints (OMXSTREAM_AUDIO, mAudioConfig.mHints);

    if (mOmxAudioPlayer) {
      if (mOmxAudioPlayer->open (&mOmxClock, mAudioConfig))
        thread ([=]() { mOmxAudioPlayer->run("aud "); } ).detach();
      else {
        delete (mOmxAudioPlayer);  // crashes ?
        mOmxAudioPlayer = nullptr;
        }
      }

    mOmxClock.reset (mOmxVideoPlayer, mOmxAudioPlayer);
    mOmxClock.stateExecute();
    }
  //}}}
  //{{{
  void playLoop() {

    bool sentStarted = true;
    bool submitEos = false;
    double lastSeekPosSec = 0.0;

    cOmxPacket* packet = nullptr;
    while (!mEntered && !mExit && !gAbort) {
      if (mSeekIncSec != 0.0) {
        //{{{  seek
        double pts = mOmxClock.getMediaTime();
        double seekPosSec = (pts ? (pts / 1000000.0) : lastSeekPosSec) + mSeekIncSec;
        lastSeekPosSec = seekPosSec;

        double seekPts = 0;
        if (mOmxReader.seek (seekPosSec, seekPts)) {
          mOmxClock.stop();
          mOmxClock.pause();

          if (mOmxVideoPlayer)
            mOmxVideoPlayer->flush();
          if (mOmxAudioPlayer)
            mOmxAudioPlayer->flush();
          delete (packet);
          packet = nullptr;

          if (pts != kNoPts)
            mOmxClock.setMediaTime (seekPts);
          }

        sentStarted = false;

        if (mOmxVideoPlayer)
          mOmxVideoPlayer->reset();
        mOmxClock.pause();

        cLog::log (LOGINFO, "seekPos:"  + frac(seekPosSec,6,5,' '));
        mSeekIncSec = 0.0;
        }
        //}}}

      mPlayPts = mOmxClock.getMediaTime();
      mLengthPts = mOmxReader.getStreamLength() * 1000.0;

      // debugStr
      auto audio_pts = mOmxAudioPlayer ? mOmxAudioPlayer->getCurPTS() : kNoPts;
      auto video_pts = mOmxVideoPlayer ? mOmxVideoPlayer->getCurPTS() : kNoPts;
      auto str = frac(audio_pts/1000000.0,6,2,' ') +
                 ":" + frac(video_pts/1000000.0,6,2,' ') +
                 " vol:" + frac(mOmxAudioPlayer ? mOmxAudioPlayer->getVolume() : 0.f, 3,2,' ') +
                 " " + string(mOmxVideoPlayer ? mOmxVideoPlayer->getDebugString() : "noVideo") +
                 " " + string(mOmxAudioPlayer ? mOmxAudioPlayer->getDebugString() : "noAudio") +
                 " " + string(mPause ? "paused":"playing");
      mDebugStr = str;

      // pause control
      if (mPause && !mOmxClock.isPaused()) {
        //{{{  pause
        cLog::log (LOGINFO, "pause");
        mOmxClock.pause();
        }
        //}}}
      if (!mPause && mOmxClock.isPaused()) {
        //{{{  resume
        cLog::log (LOGINFO, "resume");
        mOmxClock.resume();
        }
        //}}}

      if (!sentStarted) {
        //{{{  clock reset
        mOmxClock.reset (mOmxVideoPlayer, mOmxAudioPlayer);
        sentStarted = true;
        }
        //}}}

      if (!packet)
        packet = mOmxReader.readPacket();

      if (packet) {
        //{{{  got packet
        submitEos = false;

        if (mOmxVideoPlayer && mOmxReader.isActive (OMXSTREAM_VIDEO, packet->mStreamIndex)) {
          if (mOmxVideoPlayer->addPacket (packet))
            packet = NULL;
          else
            mOmxClock.msSleep (20);
          }

        else if (mOmxAudioPlayer && mOmxReader.isActive (OMXSTREAM_AUDIO, packet->mStreamIndex)) {
          if (mOmxAudioPlayer->addPacket (packet))
            packet = NULL;
          else
            mOmxClock.msSleep (20);
          }

        else {
          delete (packet);
          packet = nullptr;
          }
        }
        //}}}
      else if (mOmxReader.isEof()) {
        //{{{  EOF, may still be playing out
        if (!(mOmxVideoPlayer && mOmxVideoPlayer->getPacketCacheSize()) &&
            !(mOmxAudioPlayer && mOmxAudioPlayer->getPacketCacheSize())) {
          if (!submitEos) {
            submitEos = true;
            if (mOmxVideoPlayer)
              mOmxVideoPlayer->submitEOS();
            if (mOmxAudioPlayer)
              mOmxAudioPlayer->submitEOS();
            }
          if ((!mOmxVideoPlayer || mOmxVideoPlayer->isEOS()) &&
              (!mOmxAudioPlayer || mOmxAudioPlayer->isEOS()))
            break;
          }

        // wait about another frame
        mOmxClock.msSleep (20);
        }
        //}}}
      else // wait for another packet
        mOmxClock.msSleep (10);
      }

    delete (packet);
    }
  //}}}
  //{{{
  void endPlay() {

    mOmxClock.stop();
    mOmxClock.stateIdle();

    delete (mOmxVideoPlayer);
    mOmxVideoPlayer = nullptr;

    delete (mOmxAudioPlayer);
    mOmxAudioPlayer = nullptr;
    }
  //}}}

  //{{{  vars
  string mDebugStr;

  cOmxClock mOmxClock;
  cOmxReader mOmxReader;
  cOmxVideoPlayer* mOmxVideoPlayer = nullptr;
  cOmxAudioPlayer* mOmxAudioPlayer = nullptr;

  cKeyboard mKeyboard;

  cDvb mDvb;
  string mRoot;

  cWidget* mListWidget = nullptr;
  cWidget* mTsBox = nullptr;

  bool mPause = false;
  double mSeekIncSec = 0.0;
  double mPlayPts = 0.0;
  double mLengthPts = 0.0;

  static vector<string> mFileNames;
  unsigned int mFileNum = 0;
  bool mFileChanged = false;
  bool mEntered = false;
  //}}}
  };
vector<string> cAppWindow::mFileNames;

//{{{
int main (int argc, char* argv[]) {

  //{{{  set signals
  signal (SIGSEGV, sigHandler);
  signal (SIGABRT, sigHandler);
  signal (SIGFPE, sigHandler);
  signal (SIGINT, sigHandler);
  //}}}

  eLogLevel logLevel = LOGINFO;
  string root = "/home/pi/tv";
  string inTs;
  int frequency = 0;
  int vFifo = 1024;
  int vCache = 2 * 1024;
  int aCache = 512;
  eDeInterlaceMode deInterlaceMode = eDeInterlaceAuto;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logLevel = eLogLevel(atoi (argv[++arg]));
    else if (!strcmp(argv[arg], "n"))  logLevel = LOGNOTICE;
    else if (!strcmp(argv[arg], "e"))  logLevel = LOGERROR;
    else if (!strcmp(argv[arg], "i"))  logLevel = LOGINFO;
    else if (!strcmp(argv[arg], "i1")) logLevel = LOGINFO1;
    else if (!strcmp(argv[arg], "i2")) logLevel = LOGINFO2;
    else if (!strcmp(argv[arg], "i3")) logLevel = LOGINFO3;
    else if (!strcmp(argv[arg], "r"))  root = argv[++arg];
    else if (!strcmp(argv[arg], "i"))  inTs = argv[++arg];
    else if (!strcmp(argv[arg], "itv")) frequency = 650;
    else if (!strcmp(argv[arg], "bbc")) frequency = 674;
    else if (!strcmp(argv[arg], "hd"))  frequency = 706;
    else if (!strcmp(argv[arg], "ac")) aCache = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "vc")) vCache = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "vf")) vFifo = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "d")) deInterlaceMode = (eDeInterlaceMode)atoi (argv[++arg]);

  cLog::init (logLevel, false, "");
  cLog::log (LOGNOTICE, "omx " + root + " " + string(VERSION_DATE));

  cAppWindow appWindow (root);
  appWindow.mAudioConfig.mDevice = "omx:local";
  appWindow.mAudioConfig.mPacketMaxCacheSize = aCache * 1024;
  appWindow.mVideoConfig.mPacketMaxCacheSize = vCache * 1024;
  appWindow.mVideoConfig.mFifoSize = vFifo * 1024;
  appWindow.mVideoConfig.mDeInterlaceMode = deInterlaceMode;
  appWindow.run (inTs, frequency);

  return EXIT_SUCCESS;
  }
//}}}
