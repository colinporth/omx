// omx.cpp - simplified omxPlayer
//{{{  includes
#include <stdio.h>
#include <signal.h>
#include <stdint.h>

#include <string>
#include <chrono>
#include <thread>

#include "../shared/utils/date.h"
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "../shared/utils/cSemaphore.h"
#include "../shared/utils/cKeyboard.h"

#include "cBcmHost.h"
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

class cAppWindow : public cRaspWindow {
public:
  //{{{
  cAppWindow() {
    mKeyboard.setKeymap (cKeyConfig::buildDefaultKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (bool windowed, float scale, int alpha, const string& fileName) {

    mDebugStr = fileName;

    initialise (scale, alpha);
    add (new cTextBox (mDebugStr, 0.f));

    //mAudioConfig.is_live = true;
    if ((isURL (fileName) || isPipe (fileName) || exists (fileName)) &&
        mReader.Open (fileName.c_str(), true, mAudioConfig.is_live, 10.f)) {
      mClock.stateIdle();
      mClock.stop();
      mClock.pause();

      bool hasAudio = mReader.AudioStreamCount();
      bool hasVideo = mReader.VideoStreamCount();
      mReader.GetHints (OMXSTREAM_AUDIO, mAudioConfig.hints);
      mReader.GetHints (OMXSTREAM_VIDEO, mVideoConfig.hints);

      blankBackground (0);
      //{{{  get display aspect
      TV_DISPLAY_STATE_T current_tv_state;
      memset (&current_tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
      mBcmHost.vc_tv_get_display_state (&current_tv_state);
      mVideoConfig.display_aspect = getDisplayAspectRatio ((HDMI_ASPECT_T)current_tv_state.display.hdmi.aspect_ratio);
      mVideoConfig.display_aspect *= (float)current_tv_state.display.hdmi.height / (float)current_tv_state.display.hdmi.width;
      //}}}
      bool stop = hasVideo && !mPlayerVideo.Open (&mClock, mVideoConfig);

      mAudioConfig.device = "omx:local";
      if (mAudioConfig.device == "omx:alsa" && mAudioConfig.subdevice.empty())
        mAudioConfig.subdevice = "default";
      if (hasAudio) {
        stop |= !mPlayerAudio.Open (&mClock, mAudioConfig, &mReader);
        mPlayerAudio.SetVolume (pow (10, m_Volume / 2000.0));
        }
      m_threshold = mAudioConfig.is_live ? 0.7f : 0.2f;

      if (stop)
         cLog::log (LOGERROR, "unable to open streams");
      else {
        thread ([=]() { player (hasVideo, hasAudio); } ).detach();
        cRaspWindow::run();
        }
      }

    cLog::log (LOGERROR, "cAppWindow::run - exit");
    }
  //}}}

protected:
  //{{{
  void pollKeyboard() {

    auto event = mKeyboard.getEvent();
    switch (event) {
      case cKeyConfig::ACTION_PLAYPAUSE: m_Pause = !m_Pause; break;

      case cKeyConfig::ACTION_STEP: mClock.step(); break;
      case cKeyConfig::ACTION_SEEK_BACK_SMALL: if (mReader.CanSeek()) m_incr = -30.0; break;
      case cKeyConfig::ACTION_SEEK_FORWARD_SMALL: if (mReader.CanSeek()) m_incr = 30.0; break;
      case cKeyConfig::ACTION_SEEK_FORWARD_LARGE: if (mReader.CanSeek()) m_incr = 600.0; break;
      case cKeyConfig::ACTION_SEEK_BACK_LARGE: if (mReader.CanSeek()) m_incr = -600.0; break;

      //{{{
      case cKeyConfig::ACTION_DECREASE_VOLUME:
        m_Volume -= 300;
        mPlayerAudio.SetVolume (pow (10, m_Volume / 2000.0));
        break;
      //}}}
      //{{{
      case cKeyConfig::ACTION_INCREASE_VOLUME:
        m_Volume += 300;
        mPlayerAudio.SetVolume (pow (10, m_Volume / 2000.0));
        break;
      //}}}

      //{{{
      case cKeyConfig::ACTION_PREVIOUS_AUDIO:
        if (mReader.GetAudioIndex() > 0)
          mReader.SetActiveStream (OMXSTREAM_AUDIO, mReader.GetAudioIndex()-1);
        break;
      //}}}
      //{{{
      case cKeyConfig::ACTION_NEXT_AUDIO:
        mReader.SetActiveStream (OMXSTREAM_AUDIO, mReader.GetAudioIndex()+1);
        break;
      //}}}

      //{{{
      case cKeyConfig::ACTION_PREVIOUS_VIDEO:
        if (mReader.GetVideoIndex() > 0)
          mReader.SetActiveStream (OMXSTREAM_VIDEO, mReader.GetVideoIndex()-1);
        break;
      //}}}
      //{{{
      case cKeyConfig::ACTION_NEXT_VIDEO:
        mReader.SetActiveStream (OMXSTREAM_VIDEO, mReader.GetVideoIndex()+1);
        break;
      //}}}

      case cKeyConfig::KEY_TOGGLE_VSYNC: toggleVsync(); break; // v
      case cKeyConfig::KEY_TOGGLE_PERF:  togglePerf();  break; // p
      case cKeyConfig::KEY_TOGGLE_STATS: toggleStats(); break; // s
      case cKeyConfig::KEY_TOGGLE_TESTS: toggleTests(); break; // y

      case cKeyConfig::KEY_TOGGLE_SOLID: toggleSolid(); break; // i
      case cKeyConfig::KEY_TOGGLE_EDGES: toggleEdges(); break; // a
      case cKeyConfig::KEY_LESS_FRINGE:  fringeWidth (getFringeWidth() - 0.25f); break; // q
      case cKeyConfig::KEY_MORE_FRINGE:  fringeWidth (getFringeWidth() + 0.25f); break; // w

      case cKeyConfig::KEY_LOG1: cLog::setLogLevel (LOGNOTICE); break;
      case cKeyConfig::KEY_LOG2: cLog::setLogLevel (LOGERROR); break;
      case cKeyConfig::KEY_LOG3: cLog::setLogLevel (LOGINFO); break;
      case cKeyConfig::KEY_LOG4: cLog::setLogLevel (LOGINFO1); break;
      case cKeyConfig::KEY_LOG5: cLog::setLogLevel (LOGINFO2); break;
      case cKeyConfig::KEY_LOG6: cLog::setLogLevel (LOGINFO3); break;

      case cKeyConfig::ACTION_NONE: break;
      case cKeyConfig::ACTION_EXIT: gAbort = true; mExit = true; break;
      default: cLog::log (LOGNOTICE, "pollKeyboard - unused event %d", event); break;
      }
    }
  //}}}

private:
  //{{{
  class cKeyConfig {
  public:
    //{{{  key defines
    #define KEY_ESC   27
    #define KEY_UP    0x5b41
    #define KEY_DOWN  0x5b42
    #define KEY_LEFT  0x5b44
    #define KEY_RIGHT 0x5b43
    //}}}
    //{{{  enum eKeyAction
    enum eKeyAction { ACTION_NONE,
                      ACTION_EXIT,
                      ACTION_PLAYPAUSE, ACTION_STEP,
                      ACTION_SEEK_BACK_SMALL, ACTION_SEEK_FORWARD_SMALL,
                      ACTION_SEEK_BACK_LARGE, ACTION_SEEK_FORWARD_LARGE,
                      ACTION_PREVIOUS_VIDEO, ACTION_NEXT_VIDEO,
                      ACTION_PREVIOUS_AUDIO, ACTION_NEXT_AUDIO,
                      ACTION_DECREASE_VOLUME, ACTION_INCREASE_VOLUME,
                      KEY_TOGGLE_VSYNC, KEY_TOGGLE_PERF, KEY_TOGGLE_STATS, KEY_TOGGLE_TESTS,
                      KEY_TOGGLE_SOLID, KEY_TOGGLE_EDGES,
                      KEY_LESS_FRINGE, KEY_MORE_FRINGE,
                      KEY_LOG1, KEY_LOG2,KEY_LOG3, KEY_LOG4, KEY_LOG5, KEY_LOG6,
                      };
    //}}}

    //{{{
    static map<int,int> buildDefaultKeymap() {
      map<int,int> keymap;
      keymap['q'] = ACTION_EXIT;
      keymap['Q'] = ACTION_EXIT;
      keymap[KEY_ESC] = ACTION_EXIT;

      keymap[' '] = ACTION_PLAYPAUSE;
      keymap['>'] = ACTION_STEP;
      keymap['.'] = ACTION_STEP;

      keymap[KEY_LEFT] = ACTION_SEEK_BACK_SMALL;
      keymap[KEY_RIGHT] = ACTION_SEEK_FORWARD_SMALL;
      keymap[KEY_DOWN] = ACTION_SEEK_BACK_LARGE;
      keymap[KEY_UP] = ACTION_SEEK_FORWARD_LARGE;

      keymap['n'] = ACTION_PREVIOUS_VIDEO;
      keymap['N'] = ACTION_PREVIOUS_VIDEO;
      keymap['m'] = ACTION_NEXT_VIDEO;
      keymap['M'] = ACTION_NEXT_VIDEO;

      keymap['j'] = ACTION_PREVIOUS_AUDIO;
      keymap['J'] = ACTION_PREVIOUS_AUDIO;
      keymap['k'] = ACTION_NEXT_AUDIO;
      keymap['K'] = ACTION_NEXT_AUDIO;

      keymap['-'] = ACTION_DECREASE_VOLUME;
      keymap['+'] = ACTION_INCREASE_VOLUME;
      keymap['='] = ACTION_INCREASE_VOLUME;

      keymap['v'] = KEY_TOGGLE_VSYNC;
      keymap['V'] = KEY_TOGGLE_VSYNC;
      keymap['p'] = KEY_TOGGLE_PERF;
      keymap['P'] = KEY_TOGGLE_PERF;
      keymap['s'] = KEY_TOGGLE_STATS;
      keymap['S'] = KEY_TOGGLE_STATS;
      keymap['t'] = KEY_TOGGLE_TESTS;
      keymap['T'] = KEY_TOGGLE_TESTS;
      keymap['i'] = KEY_TOGGLE_SOLID;
      keymap['I'] = KEY_TOGGLE_SOLID;
      keymap['a'] = KEY_TOGGLE_EDGES;
      keymap['A'] = KEY_TOGGLE_EDGES;
      keymap['q'] = KEY_LESS_FRINGE;
      keymap['Q'] = KEY_LESS_FRINGE;
      keymap['w'] = KEY_MORE_FRINGE;
      keymap['W'] = KEY_MORE_FRINGE;

      keymap['1'] = KEY_LOG1;
      keymap['2'] = KEY_LOG2;
      keymap['3'] = KEY_LOG3;
      keymap['4'] = KEY_LOG4;
      keymap['5'] = KEY_LOG5;
      keymap['6'] = KEY_LOG6;

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
  float getDisplayAspectRatio (HDMI_ASPECT_T aspect) {

    switch (aspect) {
      case HDMI_ASPECT_4_3:   return  4.0 / 3.0;  break;
      case HDMI_ASPECT_14_9:  return 14.0 / 9.0;  break;
      case HDMI_ASPECT_5_4:   return  5.0 / 4.0;  break;
      case HDMI_ASPECT_16_10: return 16.0 / 10.0; break;
      case HDMI_ASPECT_15_9:  return 15.0 / 9.0;  break;
      case HDMI_ASPECT_64_27: return 64.0 / 27.0; break;
      case HDMI_ASPECT_16_9:
      default:                return 16.0 / 9.0;  break;
      }
    }
  //}}}
  //{{{
  void blankBackground (uint32_t rgba) {

    // create 1x1 black pixel, added to display just behind video
    int layer = mVideoConfig.layer - 1;
    DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open (mVideoConfig.display);

    VC_IMAGE_TYPE_T type = VC_IMAGE_ARGB8888;
    uint32_t vc_image_ptr;
    DISPMANX_RESOURCE_HANDLE_T resource = vc_dispmanx_resource_create (type, 1, 1, &vc_image_ptr);

    VC_RECT_T dst_rect;
    vc_dispmanx_rect_set (&dst_rect, 0, 0, 1, 1);
    vc_dispmanx_resource_write_data (resource, type, sizeof(rgba), &rgba, &dst_rect);

    VC_RECT_T src_rect;
    vc_dispmanx_rect_set (&src_rect, 0, 0, 1<<16, 1<<16);
    vc_dispmanx_rect_set (&dst_rect, 0, 0, 0, 0);
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start (0);
    vc_dispmanx_element_add (update, display, layer, &dst_rect, resource, &src_rect,
                             DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_STEREOSCOPIC_MONO);

    vc_dispmanx_update_submit_sync (update);
    }
  //}}}

  //{{{
  void player (bool hasVideo, bool hasAudio) {

    cLog::setThreadName ("play");

    mClock.reset (hasVideo, hasAudio);
    mClock.stateExecute();

    OMXPacket* omxPacket = nullptr;
    while (!mExit && !gAbort && !mPlayerAudio.Error()) {
      if (m_incr != 0) {
        //{{{  seek
        double pts = mClock.getMediaTime();

        double seek_pos = (pts ? (pts / 1000000.0) : last_seek_pos) + m_incr;
        last_seek_pos = seek_pos;
        seek_pos *= 1000.0;

        double startpts = 0;
        if (mReader.SeekTime ((int)seek_pos, m_incr < 0.0f, &startpts)) {
          //{{{  flush streams
          mClock.stop();
          mClock.pause();

          if (hasVideo)
            mPlayerVideo.Flush();
          if (hasAudio)
            mPlayerAudio.Flush();

          if (pts != DVD_NOPTS_VALUE)
            mClock.setMediaTime (startpts);

          if (omxPacket) {
            mReader.FreePacket (omxPacket);
            omxPacket = NULL;
            }
          }
          //}}}

        sentStarted = false;
        if (mReader.IsEof() || (hasVideo && !mPlayerVideo.Reset()))
          return;

        cLog::log (LOGINFO, "seekedTo %.0f %.0f %.0f",
                   DVD_MSEC_TO_TIME(seek_pos), startpts, mClock.getMediaTime());

        mClock.pause();
        m_incr = 0;
        }
        //}}}

      //{{{  pts, fifos
      double pts = mClock.getMediaTime();
      float threshold = min (0.1f, (float)mPlayerAudio.GetCacheTotal() * 0.1f);

      // audio
      float audio_fifo = 0.f;
      bool audio_fifo_low = false;
      bool audio_fifo_high = false;
      double audio_pts = mPlayerAudio.GetCurrentPTS();
      if (audio_pts != DVD_NOPTS_VALUE) {
        audio_fifo = (audio_pts - pts) / 1000000.0;
        audio_fifo_low = hasAudio && (audio_fifo < threshold);
        audio_fifo_high = !hasAudio ||
                          ((audio_pts != DVD_NOPTS_VALUE) && (audio_fifo > m_threshold));
        }

      // video
      float video_fifo = 0.f;
      bool video_fifo_low = false;
      bool video_fifo_high = false;
      double video_pts = mPlayerVideo.GetCurrentPTS();
      if (video_pts != DVD_NOPTS_VALUE) {
        video_fifo = (video_pts -pts) / 1000000.0;
        video_fifo_low = hasVideo && (video_fifo < threshold);
        video_fifo_high = !hasVideo ||
                          ((video_pts != DVD_NOPTS_VALUE) && (video_fifo > m_threshold));
        }

      // debug
      mDebugStr = "p:"   + decFrac(pts/1000000.0,6,5,' ') +
                  " a:"  + decFrac(audio_pts/1000000.0,6,5,' ') +
                  " v:"  + decFrac(video_pts/1000000.0,6,5,' ') +
                  " af:" + decFrac(audio_fifo,6,5,' ') +
                  " vf:" + decFrac(video_fifo,6,5,' ') +
                  " al:" + dec(mPlayerAudio.GetLevel()) +
                  " vl:" + dec(mPlayerVideo.GetLevel()) +
                  " ad:" + dec(mPlayerAudio.GetDelay()) +
                  " ac:" + dec(mPlayerAudio.GetCacheTotal());
      //}}}
      if (mAudioConfig.is_live) {
        //{{{  live - latency under control by adjusting clock
        float latency = DVD_NOPTS_VALUE;
        if (hasAudio && (audio_pts != DVD_NOPTS_VALUE))
          latency = audio_fifo;
        else if (!hasAudio && hasVideo && video_pts != DVD_NOPTS_VALUE)
          latency = video_fifo;

        if (!m_Pause && (latency != DVD_NOPTS_VALUE)) {
          if (mClock.isPaused()) {
            if (latency > m_threshold) {
              cLog::log (LOGINFO1, "omxPlayer resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p",
                         audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                         audio_fifo_high, video_fifo_high, mReader.IsEof(), omxPacket);
              mClock.resume();
              m_latency = latency;
              }
            }

          else {
            m_latency = m_latency*0.99f + latency*0.01f;
            float speed = 1.0f;
            if (m_latency < 0.5f*m_threshold)
              speed = 0.990f;
            else if (m_latency < 0.9f*m_threshold)
              speed = 0.999f;
            else if (m_latency > 2.0f*m_threshold)
              speed = 1.010f;
            else if (m_latency > 1.1f*m_threshold)
              speed = 1.001f;

            mClock.setSpeed (DVD_PLAYSPEED_NORMAL * speed, false);
            mClock.setSpeed (DVD_PLAYSPEED_NORMAL * speed, true);
            cLog::log (LOGINFO1, "omxPlayer live: %.2f (%.2f) S:%.3f T:%.2f",
                       m_latency, latency, speed, m_threshold);
            }
          }
        }
        //}}}
      else if (!m_Pause && (mReader.IsEof() || omxPacket || (audio_fifo_high && video_fifo_high))) {
        //{{{  pause
        if (mClock.isPaused()) {
          cLog::log (LOGINFO, "resume %.2f,%.2f (%d,%d,%d,%d) eof:%d pkt:%p",
                     audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                     audio_fifo_high, video_fifo_high, mReader.IsEof(), omxPacket);

          mClock.resume();
          }
        }
        //}}}
      else if (m_Pause || audio_fifo_low || video_fifo_low) {
        //{{{  resume
        if (!mClock.isPaused()) {
          if (!m_Pause)
            m_threshold = min(2.0f*m_threshold, 16.0f);
          cLog::log (LOGINFO, "pause %.2f,%.2f (%d,%d,%d,%d) %.2f",
                     audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                     audio_fifo_high, video_fifo_high, m_threshold);
          mClock.pause();
          }
        }
        //}}}

      if (!sentStarted) {
        //{{{  reset
        cLog::log (LOGINFO, "omxPlayer reset");
        mClock.reset (hasVideo, hasAudio);
        sentStarted = true;
        }
        //}}}

      //{{{  packet reader
      if (!omxPacket)
        omxPacket = mReader.Read();
      if (omxPacket)
        m_send_eos = false;

      if (mReader.IsEof() && !omxPacket) {
        // demuxer EOF, but may have not played out data yet
        if ( (hasVideo && mPlayerVideo.GetCached()) || (hasAudio && mPlayerAudio.GetCached()) ) {
          cOmxClock::sleep (10);
          return;
          }
        if (!m_send_eos && hasVideo)
          mPlayerVideo.SubmitEOS();
        if (!m_send_eos && hasAudio)
          mPlayerAudio.SubmitEOS();

        m_send_eos = true;
        if ((hasVideo && !mPlayerVideo.IsEOS()) || (hasAudio && !mPlayerAudio.IsEOS())) {
          cOmxClock::sleep (10);
          return;
          }
        return;
        }

      if (omxPacket) {
        if (hasVideo && mReader.IsActive (OMXSTREAM_VIDEO, omxPacket->stream_index)) {
          if (mPlayerVideo.AddPacket (omxPacket))
            omxPacket = NULL;
          else
            cOmxClock::sleep (10);
          }
        else if (hasAudio && (omxPacket->codec_type == AVMEDIA_TYPE_AUDIO)) {
          if (mPlayerAudio.AddPacket (omxPacket))
            omxPacket = NULL;
          else
            cOmxClock::sleep (10);
          }
        else {
          mReader.FreePacket (omxPacket);
          omxPacket = NULL;
          }
        }
      else
        cOmxClock::sleep (10);
      //}}}
      }

    mClock.stop();
    mClock.stateIdle();

    if (omxPacket)
      mReader.FreePacket (omxPacket);

    cLog::log (LOGNOTICE, "exit exit:%d abort:%d audioError:%d", mExit, gAbort, mPlayerAudio.Error());
    }
  //}}}

  //{{{  vars
  cKeyboard mKeyboard;

  cBcmHost mBcmHost;
  cOmxClock mClock;
  cOmxReader mReader;
  cOmxPlayerVideo mPlayerVideo;
  cOmxVideoConfig mVideoConfig;
  cOmxPlayerAudio mPlayerAudio;
  cOmxAudioConfig mAudioConfig;

  enum PCMChannels* m_pChannelMap = NULL;

  bool m_send_eos = false;
  double m_incr = 0;
  double last_seek_pos = 0;
  float m_latency = 0.f;
  long m_Volume = 0;
  bool m_Pause = false;
  float m_threshold = 0.f;

  string mDebugStr;
  bool sentStarted = true;
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

  bool logInfo = false;
  bool windowed = true;
  uint32_t alpha = 160;
  float scale = 1.f;
  string fileName;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "w")) windowed = false;
    else if (!strcmp(argv[arg], "a")) alpha = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "s")) scale = atoi (argv[++arg]) / 100.f;
    else fileName = argv[arg];

  cLog::init (logInfo ? LOGINFO1 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "omx " + string(VERSION_DATE) + " " + fileName);

  cAppWindow appWindow;
  appWindow.run (windowed, scale, alpha, fileName);

  return EXIT_SUCCESS;
  }
//}}}
