// omx.cpp - simplified omxPlayer
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

#include "bcm_host.h"
#include "cPcmRemap.h"

#define AV_NOWARN_DEPRECATED
#include "avLibs.h"

#include "cOmx.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cAudio.h"
#include "cVideo.h"

#include "../shared/nanoVg/cRaspWindow.h"
#include "../shared/widgets/cTextBox.h"
#include "../shared/widgets/cListWidget.h"

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
  cAppWindow() {
    mKeyboard.setKeymap (cKeyConfig::getKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (const string& root, unsigned int fileNum) {

    mFileNum = fileNum;
    nftw (root.c_str(), addFile, 20, 0);

    if (!mFileNames.empty()) {
      initialise (1.f, 0);
      add (new cTextBox (mDebugStr, 0.f));
      addBelow (new cListWidget (mFileNames, mFileNum, mFileChanged, 0.f,0.f));

      thread ([=]() { player (mFileNames[mFileNum]); } ).detach();
      cRaspWindow::run();
      }
    }
  //}}}

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
      ACT_TOGGLE_VSYNC, ACT_TOGGLE_PERF, ACT_TOGGLE_STATS, ACT_TOGGLE_TESTS,
      ACT_TOGGLE_SOLID, ACT_TOGGLE_EDGES,
      ACT_LESS_FRINGE, ACT_MORE_FRINGE,
      ACT_LOG1, ACT_LOG2, ACT_LOG3, ACT_LOG4, ACT_LOG5, ACT_LOG6,
      };
    //}}}
    //{{{
    static map<int,int> getKeymap() {

      map<int,int> keymap;

      keymap['q'] = ACT_EXIT;
      keymap['Q'] = ACT_EXIT;
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

      keymap['v'] = ACT_TOGGLE_VSYNC;
      keymap['V'] = ACT_TOGGLE_VSYNC;
      keymap['p'] = ACT_TOGGLE_PERF;
      keymap['P'] = ACT_TOGGLE_PERF;
      keymap['s'] = ACT_TOGGLE_STATS;
      keymap['S'] = ACT_TOGGLE_STATS;
      keymap['t'] = ACT_TOGGLE_TESTS;
      keymap['T'] = ACT_TOGGLE_TESTS;
      keymap['i'] = ACT_TOGGLE_SOLID;
      keymap['I'] = ACT_TOGGLE_SOLID;
      keymap['a'] = ACT_TOGGLE_EDGES;
      keymap['A'] = ACT_TOGGLE_EDGES;
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

    auto event = mKeyboard.getEvent();
    switch (event) {
      //{{{
      case cKeyConfig::ACT_PREV_FILE:
        if (mFileNum > 0) {
          mFileNum--;
          mFileChanged = true;
          //mEntered = true;
          }
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_NEXT_FILE:
        if (mFileNum < mFileNames.size()-1) {
          mFileNum++;
          mFileChanged = true;
          //mEntered = true;
          }
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_ENTER:
        cLog::log (LOGNOTICE, "enter");
        mEntered = true;
        break;
      //}}}

      case cKeyConfig::ACT_PLAYPAUSE: mPause = !mPause; break;
      case cKeyConfig::ACT_STEP: mClock.step(); break;
      case cKeyConfig::ACT_SEEK_DEC_SMALL: mSeekIncSec = -10.0; break;
      case cKeyConfig::ACT_SEEK_INC_SMALL: mSeekIncSec = +10.0; break;
      case cKeyConfig::ACT_SEEK_DEC_LARGE: mSeekIncSec = -60.0; break;
      case cKeyConfig::ACT_SEEK_INC_LARGE: mSeekIncSec = +60.0; break;

      //{{{
      case cKeyConfig::ACT_DEC_VOLUME:
        mVolume -= 300;
        mPlayerAudio->setVolume (pow (10, mVolume / 2000.0));
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_INC_VOLUME:
        mVolume += 300;
        mPlayerAudio->setVolume (pow (10, mVolume / 2000.0));
        break;
      //}}}

      case cKeyConfig::ACT_TOGGLE_VSYNC: toggleVsync(); break; // v
      case cKeyConfig::ACT_TOGGLE_PERF:  togglePerf();  break; // p
      case cKeyConfig::ACT_TOGGLE_STATS: toggleStats(); break; // s
      case cKeyConfig::ACT_TOGGLE_TESTS: toggleTests(); break; // y

      case cKeyConfig::ACT_TOGGLE_SOLID: toggleSolid(); break; // i
      case cKeyConfig::ACT_TOGGLE_EDGES: toggleEdges(); break; // a
      case cKeyConfig::ACT_LESS_FRINGE:  fringeWidth (getFringeWidth() - 0.25f); break; // q
      case cKeyConfig::ACT_MORE_FRINGE:  fringeWidth (getFringeWidth() + 0.25f); break; // w

      case cKeyConfig::ACT_LOG1: cLog::setLogLevel (LOGNOTICE); break;
      case cKeyConfig::ACT_LOG2: cLog::setLogLevel (LOGERROR); break;
      case cKeyConfig::ACT_LOG3: cLog::setLogLevel (LOGINFO); break;
      case cKeyConfig::ACT_LOG4: cLog::setLogLevel (LOGINFO1); break;
      case cKeyConfig::ACT_LOG5: cLog::setLogLevel (LOGINFO2); break;
      case cKeyConfig::ACT_LOG6: cLog::setLogLevel (LOGINFO3); break;

      case cKeyConfig::ACT_NONE: break;
      case cKeyConfig::ACT_EXIT: gAbort = true; mExit = true; break;
      default: cLog::log (LOGNOTICE, "pollKeyboard - unused event %d", event); break;
      }
    }
  //}}}

private:
  //{{{
  static int addFile (const char* fileName, const struct stat* statBuf, int flag, struct FTW* ftw) {

    // ftw->base - offset of base in path
    // (intmax_t)statBuf->st_size,
    if (flag == FTW_F) {
      cLog::log (LOGINFO, fileName);
      mFileNames.push_back (fileName);
      }

    return 0;
    }
  //}}}
  //{{{
  void player (string fileName) {

    cLog::setThreadName ("play");

    cOmxVideoConfig videoConfig;
    cOmxAudioConfig audioConfig;
    //{{{  set videoConfig aspect
    TV_DISPLAY_STATE_T state;
    memset (&state, 0, sizeof(TV_DISPLAY_STATE_T));
    vc_tv_get_display_state (&state);

    switch ((HDMI_ASPECT_T)state.display.hdmi.aspect_ratio) {
      case HDMI_ASPECT_4_3:   videoConfig.display_aspect =  4.f / 3.f;  break;
      case HDMI_ASPECT_14_9:  videoConfig.display_aspect = 14.f / 9.f;  break;
      case HDMI_ASPECT_5_4:   videoConfig.display_aspect =  5.f / 4.f;  break;
      case HDMI_ASPECT_16_10: videoConfig.display_aspect = 16.f / 10.f; break;
      case HDMI_ASPECT_15_9:  videoConfig.display_aspect = 15.f /  9.f; break;
      case HDMI_ASPECT_64_27: videoConfig.display_aspect = 64.f / 27.f; break;
      case HDMI_ASPECT_16_9:
      default:                videoConfig.display_aspect = 16.f /  9.f; break;
      }

    videoConfig.display_aspect *= (float)state.display.hdmi.height / (float)state.display.hdmi.width;
    //}}}
    //{{{  create 1x1 black pixel, added to display just behind video
    auto display = vc_dispmanx_display_open (videoConfig.display);

    uint32_t vc_image_ptr;
    auto resource = vc_dispmanx_resource_create (VC_IMAGE_ARGB8888, 1, 1, &vc_image_ptr);

    uint32_t rgba = 0;
    VC_RECT_T dst_rect;
    vc_dispmanx_rect_set (&dst_rect, 0, 0, 1, 1);
    vc_dispmanx_resource_write_data (resource, VC_IMAGE_ARGB8888, sizeof(rgba), &rgba, &dst_rect);

    VC_RECT_T src_rect;
    vc_dispmanx_rect_set (&src_rect, 0, 0, 1<<16, 1<<16);
    vc_dispmanx_rect_set (&dst_rect, 0, 0, 0, 0);

    auto update = vc_dispmanx_update_start (0);
    vc_dispmanx_element_add (update, display, videoConfig.layer-1,
                             &dst_rect, resource, &src_rect,
                             DISPMANX_PROTECTION_NONE, NULL, NULL,
                             DISPMANX_STEREOSCOPIC_MONO);

    vc_dispmanx_update_submit_sync (update);
    //}}}

    //audioConfig.is_live = true;
    //audioConfig.hwdecode = true;
    while (true) {
      cOmxPlayerVideo* mPlayerVideo = nullptr;
      cOmxPlayerAudio* mPlayerAudio = nullptr;
      if (mReader.open (fileName, false, audioConfig.is_live, 5.f, "","","probesize:1000000","")) {
        //{{{  start play
        mClock.stateIdle();
        mClock.stop();
        mClock.pause();

        if (mReader.getAudioStreamCount())
          mPlayerAudio = new cOmxPlayerAudio();
        if (mReader.getVideoStreamCount())
          mPlayerVideo = new cOmxPlayerVideo();

        mReader.getHints (OMXSTREAM_AUDIO, audioConfig.hints);
        mReader.getHints (OMXSTREAM_VIDEO, videoConfig.hints);

        if (mPlayerVideo && mPlayerVideo->open (&mClock, videoConfig))
          thread ([=]() { mPlayerVideo->run(); } ).detach();

        audioConfig.device = "omx:local";
        if (mPlayerAudio && mPlayerAudio->open (&mClock, audioConfig, &mReader)) {
          thread ([=]() { mPlayerAudio->run(); } ).detach();
          mPlayerAudio->setVolume (pow (10, mVolume / 2000.0));
          //mPlayerAudio.SetDynamicRangeCompression (m_Amplification);
          }

        auto loadThreshold = audioConfig.is_live ? 0.7f : 0.2f;
        float loadLatency = 0.f;

        mClock.reset (mPlayerVideo, mPlayerAudio);
        mClock.stateExecute();
        //}}}

        bool sentStarted = true;
        bool submitEos = false;
        double lastSeekPosSec = 0.0;
        OMXPacket* packet = nullptr;
        while (!mEntered && !mExit && !gAbort) {
          //{{{  play loop
          if (mSeekIncSec != 0.0) {
            //{{{  seek
            double pts = mClock.getMediaTime();
            double seekPosSec = (pts ? (pts / 1000000.0) : lastSeekPosSec) + mSeekIncSec;
            lastSeekPosSec = seekPosSec;

            double seekPts = 0;
            if (mReader.seek (seekPosSec, seekPts)) {
              mClock.stop();
              mClock.pause();

              if (mPlayerVideo)
                mPlayerVideo->flush();
              if (mPlayerAudio)
                mPlayerAudio->flush();
              mReader.freePacket (packet);

              if (pts != DVD_NOPTS_VALUE)
                mClock.setMediaTime (seekPts);
              }

            sentStarted = false;

            if (mPlayerVideo)
              mPlayerVideo->reset();
            mClock.pause();

            cLog::log (LOGINFO, "seekPos:"  + decFrac(seekPosSec,6,5,' '));
            mSeekIncSec = 0.0;
            }
            //}}}

          //{{{  pts, fifos
          auto clockPts = mClock.getMediaTime();
          auto threshold = mPlayerAudio ? min (0.1f, (float)mPlayerAudio->getCacheTotal() * 0.1f) : 0.1f;

          // audio
          auto audio_fifo = 0.f;
          auto audio_fifo_low = false;
          auto audio_fifo_high = false;
          auto audio_pts = mPlayerAudio ? mPlayerAudio->getCurrentPTS() : DVD_NOPTS_VALUE;
          if (audio_pts != DVD_NOPTS_VALUE) {
            audio_fifo = (audio_pts - clockPts) / 1000000.0;
            audio_fifo_low = mPlayerAudio && (audio_fifo < threshold);
            audio_fifo_high = !mPlayerAudio ||
                              ((audio_pts != DVD_NOPTS_VALUE) && (audio_fifo > loadThreshold));
            }

          // video
          auto video_fifo = 0.f;
          auto video_fifo_low = false;
          auto video_fifo_high = false;
          auto video_pts = mPlayerVideo ? mPlayerVideo->getCurrentPTS() : DVD_NOPTS_VALUE;
          if (video_pts != DVD_NOPTS_VALUE) {
            video_fifo = (video_pts - clockPts) / 1000000.0;
            video_fifo_low = mPlayerVideo && (video_fifo < threshold);
            video_fifo_high = !mPlayerVideo ||
                              ((video_pts != DVD_NOPTS_VALUE) && (video_fifo > loadThreshold));
            }
          // debug
          auto str = decFrac(clockPts/1000000.0,6,5,' ') +
                     " a:"  + decFrac(audio_pts/1000000.0,6,5,' ') +
                     " v:"  + decFrac(video_pts/1000000.0,6,5,' ') +
                     " af:" + decFrac(audio_fifo,6,5,' ') +
                     " vf:" + decFrac(video_fifo,6,5,' ') +
                     " al:" + dec(mPlayerAudio->getLevel()) +
                     " vl:" + dec(mPlayerVideo->getLevel()) +
                     " ad:" + dec(mPlayerAudio->getDelay()) +
                     " ac:" + dec(mPlayerAudio->getCacheTotal());
          mDebugStr = str;
          //}}}

          if (audioConfig.is_live) {
            //{{{  live latency controlled by adjusting clock
            float latency = DVD_NOPTS_VALUE;
            if (mPlayerAudio && (audio_pts != DVD_NOPTS_VALUE))
              latency = audio_fifo;
            else if (!mPlayerAudio && mPlayerVideo && video_pts != DVD_NOPTS_VALUE)
              latency = video_fifo;

            if (!mPause && (latency != DVD_NOPTS_VALUE)) {
              if (mClock.isPaused()) {
                if (latency > loadThreshold) {
                  cLog::log (LOGINFO, "resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p",
                             audio_fifo, video_fifo,
                             audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high,
                             mReader.isEof(), packet);
                  mClock.resume();
                  loadLatency = latency;
                  }
                }

              else {
                loadLatency = loadLatency * 0.99f + latency * 0.01f;
                float speed = 1.f;
                if (loadLatency < 0.5f * loadThreshold)
                  speed = 0.990f;
                else if (loadLatency < 0.9f*loadThreshold)
                  speed = 0.999f;
                else if (loadLatency > 2.f*loadThreshold)
                  speed = 1.010f;
                else if (loadLatency > 1.1f*loadThreshold)
                  speed = 1.001f;

                mClock.setSpeed (DVD_PLAYSPEED_NORMAL * speed, false);
                mClock.setSpeed (DVD_PLAYSPEED_NORMAL * speed, true);
                cLog::log (LOGINFO1, "omxPlayer live: %.2f (%.2f) S:%.3f T:%.2f",
                           loadLatency, latency, speed, loadThreshold);
                }
              }
            }
            //}}}
          else if (!mPause && (mReader.isEof() || packet || (audio_fifo_high && video_fifo_high))) {
            //{{{  resume
            if (mClock.isPaused()) {
              cLog::log (LOGINFO, "resume aFifo:%.2f vFifo:%.2f %s%s%s%s%s%s",
                         audio_fifo, video_fifo,
                         audio_fifo_low ? "aFifoLo ":"",
                         video_fifo_low ? "vFifoLo ":"",
                         audio_fifo_high ? "aFifoHi ":"",
                         video_fifo_high ? "vFifoHi ":"",
                         mReader.isEof() ? "eof " : "",
                         packet ? "" : "emptyPkt");

              mClock.resume();
              }
            }
            //}}}
          else if (mPause || audio_fifo_low || video_fifo_low) {
            //{{{  pause
            if (!mClock.isPaused()) {
              if (!mPause)
                loadThreshold = min(2.f*loadThreshold, 16.f);

              cLog::log (LOGINFO, "pause aFifo:%.2f vFifo:%.2f %s%s%s%s thresh:%.2f",
                         audio_fifo, video_fifo,
                         audio_fifo_low ? "aFifoLo ":"",
                         video_fifo_low ? "vFifoLo ":"",
                         audio_fifo_high ? "aFifoHi ":"",
                         video_fifo_high ? "vFifoHi ":"",
                         loadThreshold);

              mClock.pause();
              }
            }
            //}}}

          if (!sentStarted) {
            //{{{  omx reset
            mClock.reset (mPlayerVideo, mPlayerAudio);
            sentStarted = true;
            }
            //}}}

          // packet reader
          if (!packet)
            packet = mReader.readPacket();

          if (packet) {
            //{{{  got packet
            submitEos = false;
            if (mPlayerVideo && mReader.isActive (OMXSTREAM_VIDEO, packet->stream_index)) {
              if (mPlayerVideo->addPacket (packet))
                packet = NULL;
              else
                cOmxClock::sleep (10);
              }

            else if (mPlayerAudio && (packet->codec_type == AVMEDIA_TYPE_AUDIO)) {
              if (mPlayerAudio->addPacket (packet))
                packet = NULL;
              else
                cOmxClock::sleep (10);
              }

            else
              mReader.freePacket (packet);
            }
            //}}}
          else if (mReader.isEof()) {
            //{{{  EOF, may still be playing out
            if (!(mPlayerVideo && mPlayerVideo->getCached()) && !(mPlayerAudio && mPlayerAudio->getCached())) {
              if (!submitEos) {
                submitEos = true;
                if (mPlayerVideo)
                  mPlayerVideo->submitEOS();
                if (mPlayerAudio)
                  mPlayerAudio->submitEOS();
                }
              if ((!mPlayerVideo || mPlayerVideo->isEOS()) && (!mPlayerAudio || mPlayerAudio->isEOS()))
                break;
              }

            // wait about another frame
            cOmxClock::sleep (20);
            }
            //}}}
          else // wait for another packet
            cOmxClock::sleep (10);
          }
          //}}}

        //{{{  stop play
        mClock.stop();
        mClock.stateIdle();
        mReader.freePacket (packet);
        //}}}
        }
      delete (mPlayerVideo);
      delete (mPlayerAudio);

      if (mExit || gAbort)
        break;
      else if (mEntered) 
        mEntered = false;
      else if (mFileNum >= mFileNames.size()-1)
        break;
      else
        mFileNum++;
      fileName = mFileNames[mFileNum];
      }

    cLog::log (LOGNOTICE, "player - exit " + string(mExit ? "mExit" : "") +
                          " " + string(gAbort ? "gAbort" : ""));
    mExit = true;
    }
  //}}}
  //{{{  vars
  cKeyboard mKeyboard;
  cOmxClock mClock;
  cOmxReader mReader;

  cOmxPlayerVideo* mPlayerVideo = nullptr;
  cOmxPlayerAudio* mPlayerAudio = nullptr;

  long mVolume = 0;
  bool mPause = false;
  double mSeekIncSec = 0.0;
  string mDebugStr;

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
  int fileNum = 0;
  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logLevel = eLogLevel(atoi (argv[++arg]));
    else if (!strcmp(argv[arg], "e")) logLevel = LOGERROR;
    else if (!strcmp(argv[arg], "i1")) logLevel = LOGINFO1;
    else if (!strcmp(argv[arg], "i2")) logLevel = LOGINFO2;
    else if (!strcmp(argv[arg], "i3")) logLevel = LOGINFO3;
    else if (!strcmp(argv[arg], "r")) root = argv[++arg];
    else fileNum = atoi (argv[arg]);

  cLog::init (logLevel, false, "");
  cLog::log (LOGNOTICE, "omx " + root + string(VERSION_DATE));

  cAppWindow appWindow;
  appWindow.run (root, fileNum);

  return EXIT_SUCCESS;
  }
//}}}
