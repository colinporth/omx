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

#include "version.h"

using namespace std;
//}}}

volatile sig_atomic_t gAbort = false;
//{{{
void sigHandler (int s) {

  if (s == SIGINT && !gAbort) {
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

int displayInfo (const char* path, const struct stat* statBuf, int tflag, struct FTW* ftw) {

  // tflag == FTW_D
  // tflag == FTW_DNR
  // tflag == FTW_DP
  // tflag == FTW_F
  // tflag == FTW_NS
  // tflag == FTW_SL
  // tflag == FTW_SLN
  // ftw->level,
  // (intmax_t)statBuf->st_size,
  if (tflag == FTW_F)
    cLog::log (LOGINFO, path + ftw->base);
  return 0;  
  }

class cAppWindow : public cRaspWindow {
public:
  //{{{
  cAppWindow() {
    mKeyboard.setKeymap (cKeyConfig::getKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (const string& fileName) {

    mDebugStr = fileName;

    initialise (1.f, 0);
    add (new cTextBox (mDebugStr, 0.f));

    thread ([=]() { player (fileName); } ).detach();

    cRaspWindow::run();
    }
  //}}}

protected:
  //{{{
  void pollKeyboard() {

    auto event = mKeyboard.getEvent();
    switch (event) {
      case cKeyConfig::ACT_PLAYPAUSE: mPause = !mPause; break;

      case cKeyConfig::ACT_STEP: mClock.step(); break;
      case cKeyConfig::ACT_SEEK_BACK_SMALL:    mSeekIncSec = -10.0; break;
      case cKeyConfig::ACT_SEEK_FORWARD_SMALL: mSeekIncSec = +10.0; break;
      case cKeyConfig::ACT_SEEK_BACK_LARGE :   mSeekIncSec = -60.0; break;
      case cKeyConfig::ACT_SEEK_FORWARD_LARGE: mSeekIncSec = +60.0; break;

      //{{{
      case cKeyConfig::ACT_DECREASE_VOLUME:
        mVolume -= 300;
        mPlayerAudio.setVolume (pow (10, mVolume / 2000.0));
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_INCREASE_VOLUME:
        mVolume += 300;
        mPlayerAudio.setVolume (pow (10, mVolume / 2000.0));
        break;
      //}}}

      //{{{
      case cKeyConfig::ACT_PREVIOUS_AUDIO:
        if (mReader.getAudioIndex() > 0)
          mReader.setActiveStream (OMXSTREAM_AUDIO, mReader.getAudioIndex()-1);
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_NEXT_AUDIO:
        mReader.setActiveStream (OMXSTREAM_AUDIO, mReader.getAudioIndex()+1);
        break;
      //}}}

      //{{{
      case cKeyConfig::ACT_PREVIOUS_VIDEO:
        if (mReader.getVideoIndex() > 0)
          mReader.setActiveStream (OMXSTREAM_VIDEO, mReader.getVideoIndex()-1);
        break;
      //}}}
      //{{{
      case cKeyConfig::ACT_NEXT_VIDEO:
        mReader.setActiveStream (OMXSTREAM_VIDEO, mReader.getVideoIndex()+1);
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
  class cKeyConfig {
  public:
    #define KEY_ESC   27
    #define KEY_UP    0x5b42
    #define KEY_DOWN  0x5b41
    #define KEY_LEFT  0x5b44
    #define KEY_RIGHT 0x5b43

    enum eKeyAction { ACT_NONE, ACT_EXIT,
                      ACT_PLAYPAUSE, ACT_STEP,
                      ACT_SEEK_BACK_SMALL, ACT_SEEK_FORWARD_SMALL,
                      ACT_SEEK_BACK_LARGE, ACT_SEEK_FORWARD_LARGE,
                      ACT_PREVIOUS_VIDEO, ACT_NEXT_VIDEO,
                      ACT_PREVIOUS_AUDIO, ACT_NEXT_AUDIO,
                      ACT_DECREASE_VOLUME, ACT_INCREASE_VOLUME,
                      ACT_TOGGLE_VSYNC, ACT_TOGGLE_PERF, ACT_TOGGLE_STATS, ACT_TOGGLE_TESTS,
                      ACT_TOGGLE_SOLID, ACT_TOGGLE_EDGES,
                      ACT_LESS_FRINGE, ACT_MORE_FRINGE,
                      ACT_LOG1, ACT_LOG2,ACT_LOG3, ACT_LOG4, ACT_LOG5, ACT_LOG6,
                      };

    //{{{
    static map<int,int> getKeymap() {
      map<int,int> keymap;
      keymap['q'] = ACT_EXIT;
      keymap['Q'] = ACT_EXIT;
      keymap[KEY_ESC] = ACT_EXIT;

      keymap[' '] = ACT_PLAYPAUSE;
      keymap['>'] = ACT_STEP;
      keymap['.'] = ACT_STEP;

      keymap[KEY_LEFT] = ACT_SEEK_BACK_SMALL;
      keymap[KEY_RIGHT] = ACT_SEEK_FORWARD_SMALL;
      keymap[KEY_DOWN] = ACT_SEEK_BACK_LARGE;
      keymap[KEY_UP] = ACT_SEEK_FORWARD_LARGE;

      keymap['n'] = ACT_PREVIOUS_VIDEO;
      keymap['N'] = ACT_PREVIOUS_VIDEO;
      keymap['m'] = ACT_NEXT_VIDEO;
      keymap['M'] = ACT_NEXT_VIDEO;

      keymap['j'] = ACT_PREVIOUS_AUDIO;
      keymap['J'] = ACT_PREVIOUS_AUDIO;
      keymap['k'] = ACT_NEXT_AUDIO;
      keymap['K'] = ACT_NEXT_AUDIO;

      keymap['-'] = ACT_DECREASE_VOLUME;
      keymap['+'] = ACT_INCREASE_VOLUME;
      keymap['='] = ACT_INCREASE_VOLUME;

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
  bool exists (const string& path) {

    struct stat buf;
    auto error = stat (path.c_str(), &buf);
    return !error || errno != ENOENT;
    }
  //}}}
  //{{{
  bool isURL (const string& str) {

    auto result = str.find ("://");
    if (result == string::npos || result == 0)
      return false;

    for (size_t i = 0; i < result; ++i)
      if (!isalpha (str[i]))
        return false;

    return true;
    }
  //}}}
  //{{{
  bool isPipe (const string& str) {

    if (str.compare (0, 5, "pipe:") == 0)
      return true;
    return false;
    }
  //}}}
  //{{{
  float getAspectRatio (HDMI_ASPECT_T aspect) {

    switch (aspect) {
      case HDMI_ASPECT_4_3:   return  4.f / 3.f;  break;
      case HDMI_ASPECT_14_9:  return 14.f / 9.f;  break;
      case HDMI_ASPECT_5_4:   return  5.f / 4.f;  break;
      case HDMI_ASPECT_16_10: return 16.f / 10.f; break;
      case HDMI_ASPECT_15_9:  return 15.f / 9.f;  break;
      case HDMI_ASPECT_64_27: return 64.f / 27.f; break;
      case HDMI_ASPECT_16_9:
      default:                return 16.f / 9.f;  break;
      }
    }
  //}}}
  //{{{
  void player (const string& fileName) {

    cLog::setThreadName ("play");

    cOmxVideoConfig videoConfig;
    cOmxAudioConfig audioConfig;
    //{{{  set videoConfig aspect
    TV_DISPLAY_STATE_T tvDisplayState;
    memset (&tvDisplayState, 0, sizeof(TV_DISPLAY_STATE_T));
    vc_tv_get_display_state (&tvDisplayState);

    videoConfig.display_aspect =
      getAspectRatio ((HDMI_ASPECT_T)tvDisplayState.display.hdmi.aspect_ratio);
    videoConfig.display_aspect *=
      (float)tvDisplayState.display.hdmi.height / (float)tvDisplayState.display.hdmi.width;
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
    if ((isURL (fileName) || isPipe (fileName) || exists (fileName)) &&
        mReader.open (fileName, false, audioConfig.is_live, 5.f, "","","probesize:400000","")) {
      mClock.stateIdle();
      mClock.stop();
      mClock.pause();

      bool hasAudio = mReader.getAudioStreamCount();
      bool hasVideo = mReader.getVideoStreamCount();
      mReader.getHints (OMXSTREAM_AUDIO, audioConfig.hints);
      mReader.getHints (OMXSTREAM_VIDEO, videoConfig.hints);

      if (hasVideo && mPlayerVideo.open (&mClock, videoConfig))
        thread ([=]() { mPlayerVideo.run(); } ).detach();

      audioConfig.device = "omx:local";
      if (hasAudio && mPlayerAudio.open (&mClock, audioConfig, &mReader)) {
        thread ([=]() { mPlayerAudio.run(); } ).detach();
        mPlayerAudio.setVolume (pow (10, mVolume / 2000.0));
        }

      auto loadThreshold = audioConfig.is_live ? 0.7f : 0.2f;
      float loadLatency = 0.f;

      mClock.reset (hasVideo, hasAudio);
      mClock.stateExecute();

      bool sendEos = false;
      double lastSeekPosSec = 0.0;
      OMXPacket* omxPacket = nullptr;
      while (!mExit && !gAbort && !mPlayerAudio.getError()) {
        if (mSeekIncSec != 0.0) {
          //{{{  seek
          double pts = mClock.getMediaTime();
          double seekPosSec = (pts ? (pts / 1000000.0) : lastSeekPosSec) + mSeekIncSec;
          lastSeekPosSec = seekPosSec;

          double seekPts = 0;
          if (mReader.seek (seekPosSec, seekPts)) {
            mClock.stop();
            mClock.pause();

            if (hasVideo)
              mPlayerVideo.flush();
            if (hasAudio)
              mPlayerAudio.flush();
            mReader.freePacket (omxPacket);

            if (pts != DVD_NOPTS_VALUE)
              mClock.setMediaTime (seekPts);
            mClock.reset (hasVideo, hasAudio);
            }

          cLog::log (LOGINFO, "seekPos:"  + decFrac(seekPosSec,6,5,' '));
          mSeekIncSec = 0.0;
          }
          //}}}

        //{{{  pts, fifos
        auto clockPts = mClock.getMediaTime();
        auto threshold = min (0.1f, (float)mPlayerAudio.getCacheTotal() * 0.1f);

        // audio
        auto audio_fifo = 0.f;
        auto audio_fifo_low = false;
        auto audio_fifo_high = false;
        auto audio_pts = mPlayerAudio.getCurrentPTS();
        if (audio_pts != DVD_NOPTS_VALUE) {
          audio_fifo = (audio_pts - clockPts) / 1000000.0;
          audio_fifo_low = hasAudio && (audio_fifo < threshold);
          audio_fifo_high = !hasAudio ||
                            ((audio_pts != DVD_NOPTS_VALUE) && (audio_fifo > loadThreshold));
          }

        // video
        auto video_fifo = 0.f;
        auto video_fifo_low = false;
        auto video_fifo_high = false;
        auto video_pts = mPlayerVideo.getCurrentPTS();
        if (video_pts != DVD_NOPTS_VALUE) {
          video_fifo = (video_pts - clockPts) / 1000000.0;
          video_fifo_low = hasVideo && (video_fifo < threshold);
          video_fifo_high = !hasVideo ||
                            ((video_pts != DVD_NOPTS_VALUE) && (video_fifo > loadThreshold));
          }
        // debug
        auto str = decFrac(clockPts/1000000.0,6,5,' ') +
                   " a:"  + decFrac(audio_pts/1000000.0,6,5,' ') +
                   " v:"  + decFrac(video_pts/1000000.0,6,5,' ') +
                   " af:" + decFrac(audio_fifo,6,5,' ') +
                   " vf:" + decFrac(video_fifo,6,5,' ') +
                   " al:" + dec(mPlayerAudio.getLevel()) +
                   " vl:" + dec(mPlayerVideo.getLevel()) +
                   " ad:" + dec(mPlayerAudio.getDelay()) +
                   " ac:" + dec(mPlayerAudio.getCacheTotal());
        mDebugStr = str;
        //}}}
        if (audioConfig.is_live) {
          //{{{  live latency controlled by adjusting clock
          float latency = DVD_NOPTS_VALUE;
          if (hasAudio && (audio_pts != DVD_NOPTS_VALUE))
            latency = audio_fifo;
          else if (!hasAudio && hasVideo && video_pts != DVD_NOPTS_VALUE)
            latency = video_fifo;

          if (!mPause && (latency != DVD_NOPTS_VALUE)) {
            if (mClock.isPaused()) {
              if (latency > loadThreshold) {
                cLog::log (LOGINFO, "resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p",
                           audio_fifo, video_fifo,
                           audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high,
                           mReader.isEof(), omxPacket);
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
        else if (!mPause && (mReader.isEof() || omxPacket || (audio_fifo_high && video_fifo_high))) {
          //{{{  resume
          if (mClock.isPaused()) {
            cLog::log (LOGINFO, "resume aFifo:%.2f vFifo:%.2f %s%s%s%s%s%s",
                       audio_fifo, video_fifo,
                       audio_fifo_low ? "aFifoLo ":"",
                       video_fifo_low ? "vFifoLo ":"",
                       audio_fifo_high ? "aFifoHi ":"",
                       video_fifo_high ? "vFifoHi ":"",
                       mReader.isEof() ? "eof " : "",
                       omxPacket ? "" : "emptyPkt");

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

        //{{{  packet reader
        if (!omxPacket)
          omxPacket = mReader.readPacket();

        if (omxPacket) {
          sendEos = false;

          if (hasVideo && mReader.isActive (OMXSTREAM_VIDEO, omxPacket->stream_index)) {
            if (mPlayerVideo.addPacket (omxPacket))
              omxPacket = NULL;
            else
              cOmxClock::sleep (10);
            }
          else if (hasAudio && (omxPacket->codec_type == AVMEDIA_TYPE_AUDIO)) {
            if (mPlayerAudio.addPacket (omxPacket))
              omxPacket = NULL;
            else
              cOmxClock::sleep (10);
            }
          else
            mReader.freePacket (omxPacket);
          }

        else {
          if (mReader.isEof()) {
            // demuxer EOF, but may have not played out data yet
            if ( (hasVideo && mPlayerVideo.getCached()) || (hasAudio && mPlayerAudio.getCached()) ) {
              cOmxClock::sleep (10);
              return;
              }
            if (!sendEos && hasVideo)
              mPlayerVideo.submitEOS();
            if (!sendEos && hasAudio)
              mPlayerAudio.submitEOS();

            sendEos = true;
            if ((hasVideo && !mPlayerVideo.isEOS()) || (hasAudio && !mPlayerAudio.isEOS())) {
              cOmxClock::sleep (10);
              return;
              }
            return;
            }

          cOmxClock::sleep (10);
          }
        //}}}
        }

      mClock.stop();
      mClock.stateIdle();
      mReader.freePacket (omxPacket);
      }

    cLog::log (LOGNOTICE, "player - exit mExit:%d gAbort:%d mPlayerAudio.getError:%d",
                          mExit, gAbort, mPlayerAudio.getError());
    mExit = true;
    }
  //}}}
  //{{{  vars
  cKeyboard mKeyboard;
  cOmxClock mClock;
  cOmxReader mReader;
  cOmxPlayerVideo mPlayerVideo;
  cOmxPlayerAudio mPlayerAudio;
  long mVolume = 0;
  bool mPause = false;
  double mSeekIncSec = 0.0;
  string mDebugStr;
  //}}}
  };

//{{{
int main (int argc, char* argv[]) {

  //{{{  set signals
  signal (SIGSEGV, sigHandler);
  signal (SIGABRT, sigHandler);
  signal (SIGFPE, sigHandler);
  signal (SIGINT, sigHandler);
  //}}}
  string root = "/home/pi/tv";
  bool logInfo = false;
  string fileName;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "r")) root = argv[++arg];
    else fileName = argv[arg];

  cLog::init (logInfo ? LOGINFO1 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "omx " + string(VERSION_DATE) + " " + fileName);

  //int flags = 0; // | FTW_DEPTH | FTW_PHYS;
  if (nftw (root.c_str(), displayInfo, 20, 0) == -1)
    cLog::log (LOGERROR, "nftw");

  cAppWindow appWindow;
  appWindow.run (root + "/" + fileName);

  return EXIT_SUCCESS;
  }
//}}}
