// omx.cpp - simplified omxPlayer
//{{{  includes
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <string>
#include <utility>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "cKeyboard.h"
#include "cBcmHost.h"

#include "../shared/utils/cLog.h"
#include "cPcmRemap.h"

#define AV_NOWARN_DEPRECATED
#include "avLibs.h"

#include "cOmx.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cAudio.h"
#include "cVideo.h"

#include "version.h"

using namespace std;
//}}}
//{{{  vars
volatile sig_atomic_t g_abort = false;

cKeyboard mKeyboard;

cBcmHost mBcmHost;
cOmxClock mClock;
cOmxReader mReader;
cOmxPlayerVideo mPlayerVideo;
cOmxVideoConfig mVideoConfig;
cOmxPlayerAudio mPlayerAudio;
cOmxAudioConfig mAudioConfig;

bool mStop = false;
bool mHasVideo = false;
bool mHasAudio = false;

enum PCMChannels* m_pChannelMap = NULL;
//}}}

//{{{
class cKeyConfig {
public:
  enum { ACTION_EXIT,
         ACTION_PLAYPAUSE,
         ACTION_DECREASE_VOLUME, ACTION_INCREASE_VOLUME,
         ACTION_SEEK_BACK_SMALL, ACTION_SEEK_FORWARD_SMALL,
         ACTION_SEEK_BACK_LARGE, ACTION_SEEK_FORWARD_LARGE,
         ACTION_STEP,
         ACTION_PREVIOUS_AUDIO, ACTION_NEXT_AUDIO,
         ACTION_PREVIOUS_VIDEO, ACTION_NEXT_VIDEO,
         };

  #define KEY_LEFT 0x5b44
  #define KEY_RIGHT 0x5b43
  #define KEY_UP 0x5b41
  #define KEY_DOWN 0x5b42
  #define KEY_ESC 27

  static map<int, int> buildDefaultKeymap() {
    map<int,int> keymap;
    keymap['j'] = ACTION_PREVIOUS_AUDIO;
    keymap['k'] = ACTION_NEXT_AUDIO;
    keymap['n'] = ACTION_PREVIOUS_VIDEO;
    keymap['m'] = ACTION_NEXT_VIDEO;

    keymap['q'] = ACTION_EXIT;
    keymap[KEY_ESC] = ACTION_EXIT;

    keymap[' '] = ACTION_PLAYPAUSE;

    keymap['-'] = ACTION_DECREASE_VOLUME;
    keymap['+'] = ACTION_INCREASE_VOLUME;
    keymap['='] = ACTION_INCREASE_VOLUME;

    keymap[KEY_LEFT] = ACTION_SEEK_BACK_SMALL;
    keymap[KEY_RIGHT] = ACTION_SEEK_FORWARD_SMALL;
    keymap[KEY_DOWN] = ACTION_SEEK_BACK_LARGE;
    keymap[KEY_UP] = ACTION_SEEK_FORWARD_LARGE;

    return keymap;
    }
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
void sigHandler (int s) {

  if (s == SIGINT && !g_abort) {
    signal (SIGINT, SIG_DFL);
    g_abort = true;
    return;
    }

  signal (SIGABRT, SIG_DFL);
  signal (SIGSEGV, SIG_DFL);
  signal (SIGFPE, SIG_DFL);
  abort();
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
int main (int argc, char* argv[]) {

  //{{{  signals
  signal (SIGSEGV, sigHandler);
  signal (SIGABRT, sigHandler);
  signal (SIGFPE, sigHandler);
  signal (SIGINT, sigHandler);
  //}}}
  bool logInfo = false;
  //bool windowed = true;
  //uint32_t alpha = 255;
  //float scale = 0.8f;
  string fileName;
  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    //else if (!strcmp(argv[arg], "w")) windowed = false;
    //else if (!strcmp(argv[arg], "a")) alpha = atoi (argv[++arg]);
    //else if (!strcmp(argv[arg], "s")) scale = atoi (argv[++arg]) / 100.f;
    else fileName = argv[arg];

  cLog::init (logInfo ? LOGINFO : LOGINFO3, false, "");
  cLog::log (LOGNOTICE, "omx " + string(VERSION_DATE) + " " + fileName);

  mKeyboard.setKeymap (cKeyConfig::buildDefaultKeymap());

  // vars
  bool m_send_eos = false;
  double m_incr = 0;
  double last_seek_pos = 0;
  float m_latency = 0.f;
  long m_Volume = 0;
  bool m_Pause = false;

  if ((isURL (fileName) || isPipe (fileName) || exists (fileName)) &&
      mReader.Open (fileName.c_str(), true, mAudioConfig.is_live, 10.f)) {
    mClock.stateIdle();
    mClock.stop();
    mClock.pause();

    mHasAudio = mReader.AudioStreamCount();
    mHasVideo = mReader.VideoStreamCount();
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
    mStop = mHasVideo && !mPlayerVideo.Open (&mClock, mVideoConfig);

    mAudioConfig.device = "omx:local";
    if (mAudioConfig.device == "omx:alsa" && mAudioConfig.subdevice.empty())
      mAudioConfig.subdevice = "default";
    if (mHasAudio) {
      mStop |= !mPlayerAudio.Open (&mClock, mAudioConfig, &mReader);
      mPlayerAudio.SetVolume (pow (10, m_Volume / 2000.0));
      }
    auto m_threshold = mAudioConfig.is_live ? 0.7f : 0.2f;

    //mPlayerVideo.SetAlpha (128);
    mClock.reset (mHasVideo, mHasAudio);
    mClock.stateExecute();

    OMXPacket* mOmxPacket = NULL;
    bool sentStarted = true;
    double m_last_check_time = 0.0;
    while (!mStop && !g_abort && !mPlayerAudio.Error()) {
      auto now = mClock.getAbsoluteClock();
      bool update = (m_last_check_time == 0.0) || (m_last_check_time + DVD_MSEC_TO_TIME(20) <= now);
      if (update) {
        //{{{  keyboard update
        m_last_check_time = now;

        switch (mKeyboard.getEvent()) {
          case cKeyConfig::ACTION_EXIT: g_abort = true; mStop = true; break;
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
            if (mHasAudio) {
              int new_index = mReader.GetAudioIndex() - 1;
              if (new_index >= 0)
                mReader.SetActiveStream (OMXSTREAM_AUDIO, new_index);
              }
            break;
          //}}}
          //{{{
          case cKeyConfig::ACTION_NEXT_AUDIO:
            if (mHasAudio)
              mReader.SetActiveStream (OMXSTREAM_AUDIO, mReader.GetAudioIndex() + 1);
            break;
          //}}}
          //{{{
          case cKeyConfig::ACTION_PREVIOUS_VIDEO:
            if (mHasVideo) {
              int new_index = mReader.GetVideoIndex() - 1;
              if (new_index >= 0)
                mReader.SetActiveStream (OMXSTREAM_VIDEO, new_index);
              }
            break;
          //}}}
          //{{{
          case cKeyConfig::ACTION_NEXT_VIDEO:
            if (mHasVideo)
              mReader.SetActiveStream (OMXSTREAM_VIDEO, mReader.GetVideoIndex() + 1);
            break;
          //}}}
          default: break;
          }
        }
        //}}}
      if (m_incr != 0) {
        //{{{  seek
        double pts = mClock.getMediaTime();
        double seek_pos = (pts ? pts / DVD_TIME_BASE : last_seek_pos) + m_incr;

        last_seek_pos = seek_pos;
        seek_pos *= 1000.0;

        double startpts = 0;
        if (mReader.SeekTime ((int)seek_pos, m_incr < 0.0f, &startpts)) {
          //{{{  flush streams
          mClock.stop();
          mClock.pause();

          if (mHasVideo)
            mPlayerVideo.Flush();
          if (mHasAudio)
            mPlayerAudio.Flush();

          if (pts != DVD_NOPTS_VALUE)
            mClock.setMediaTime (startpts);

          if (mOmxPacket) {
            mReader.FreePacket (mOmxPacket);
            mOmxPacket = NULL;
            }
          }
          //}}}

        sentStarted = false;
        if (mReader.IsEof() || (mHasVideo && !mPlayerVideo.Reset()))
          break;

        cLog::log (LOGINFO1, "omxPlayer seeked to %.0f %.0f %.0f",
                   DVD_MSEC_TO_TIME(seek_pos), startpts, mClock.getMediaTime());

        mClock.pause();
        m_incr = 0;
        }
        //}}}
      if (update) {
        //{{{  player update
        /* when the video/audio fifos are low, we pause clock, when high we resume */
        double stamp = mClock.getMediaTime();

        double audio_pts = mPlayerAudio.GetCurrentPTS();
        float audio_fifo = audio_pts == DVD_NOPTS_VALUE ? 0.0f : audio_pts / DVD_TIME_BASE - stamp * 1e-6;

        double video_pts = mPlayerVideo.GetCurrentPTS();
        float video_fifo = video_pts == DVD_NOPTS_VALUE ? 0.0f : video_pts / DVD_TIME_BASE - stamp * 1e-6;

        float threshold = min (0.1f, (float)mPlayerAudio.GetCacheTotal() * 0.1f);

        bool audio_fifo_low = false;
        bool audio_fifo_high = false;
        if (audio_pts != DVD_NOPTS_VALUE) {
          audio_fifo_low = mHasAudio && audio_fifo < threshold;
          audio_fifo_high = !mHasAudio || (audio_pts != DVD_NOPTS_VALUE && audio_fifo > m_threshold);
          }

        bool video_fifo_low = false;
        bool video_fifo_high = false;
        if (video_pts != DVD_NOPTS_VALUE) {
          video_fifo_low = mHasVideo && video_fifo < threshold;
          video_fifo_high = !mHasVideo || (video_pts != DVD_NOPTS_VALUE && video_fifo > m_threshold);
          }

        if (!mClock.isPaused()) 
          cLog::log (LOGINFO, "%.0f av:%.0f:%.0f av:%.2f:%.2f th:%.2f %d%d%d%d av:%d:%d d%.2f c%.2f",
                     stamp, 
                     audio_pts, video_pts,
                     (audio_pts == DVD_NOPTS_VALUE) ? 0.0 : audio_fifo,
                     (video_pts == DVD_NOPTS_VALUE) ? 0.0 : video_fifo,
                     m_threshold,
                     audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high,
                     mPlayerAudio.GetLevel(), mPlayerVideo.GetLevel(),
                     mPlayerAudio.GetDelay(), (float)mPlayerAudio.GetCacheTotal());

        if (mAudioConfig.is_live) {
          //{{{  live - latency under control by adjusting clock
          float latency = DVD_NOPTS_VALUE;

          if (mHasAudio && audio_pts != DVD_NOPTS_VALUE)
            latency = audio_fifo;

          else if (!mHasAudio && mHasVideo && video_pts != DVD_NOPTS_VALUE)
            latency = video_fifo;

          if (!m_Pause && latency != DVD_NOPTS_VALUE) {
            if (mClock.isPaused()) {
              if (latency > m_threshold) {
                cLog::log (LOGINFO1, "omxPlayer resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p",
                           audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                           audio_fifo_high, video_fifo_high, mReader.IsEof(), mOmxPacket);
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
        else if (!m_Pause && 
                 (mReader.IsEof() || mOmxPacket || (audio_fifo_high && video_fifo_high))) {
          //{{{  pause
          if (mClock.isPaused()) {
            cLog::log (LOGINFO1, "omxPlayer resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p",
                       audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                       audio_fifo_high, video_fifo_high, mReader.IsEof(), mOmxPacket);

            mClock.resume();
            }
          }
          //}}}
        else if (m_Pause || audio_fifo_low || video_fifo_low) {
          //{{{  resume
          if (!mClock.isPaused()) {
            if (!m_Pause)
              m_threshold = min(2.0f*m_threshold, 16.0f);

            cLog::log (LOGINFO1, "omxPlayer pause %.2f,%.2f (%d,%d,%d,%d) %.2f",
                       audio_fifo, video_fifo, audio_fifo_low, video_fifo_low,
                       audio_fifo_high, video_fifo_high, m_threshold);

            mClock.pause();
            }
          }
          //}}}
        }
        //}}}
      if (!sentStarted) {
        //{{{  reset
        cLog::log (LOGINFO1, "omxPlayer reset");
        mClock.reset (mHasVideo, mHasAudio);
        sentStarted = true;
        }
        //}}}
      //{{{  packet reader
      if (!mOmxPacket)
        mOmxPacket = mReader.Read();
      if (mOmxPacket)
        m_send_eos = false;

      if (mReader.IsEof() && !mOmxPacket) {
        // demuxer EOF, but may have not played out data yet
        if ( (mHasVideo && mPlayerVideo.GetCached()) || (mHasAudio && mPlayerAudio.GetCached()) ) {
          cOmxClock::sleep (10);
          continue;
          }
        if (!m_send_eos && mHasVideo)
          mPlayerVideo.SubmitEOS();
        if (!m_send_eos && mHasAudio)
          mPlayerAudio.SubmitEOS();

        m_send_eos = true;
        if ((mHasVideo && !mPlayerVideo.IsEOS()) || (mHasAudio && !mPlayerAudio.IsEOS())) {
          cOmxClock::sleep (10);
          continue;
          }
        break;
        }

      if (mOmxPacket) {
        if (mHasVideo && mReader.IsActive (OMXSTREAM_VIDEO, mOmxPacket->stream_index)) {
          if (mPlayerVideo.AddPacket (mOmxPacket))
            mOmxPacket = NULL;
          else
            cOmxClock::sleep (10);
          }

        else if (mHasAudio && (mOmxPacket->codec_type == AVMEDIA_TYPE_AUDIO)) {
          if (mPlayerAudio.AddPacket (mOmxPacket))
            mOmxPacket = NULL;
          else
            cOmxClock::sleep (10);
          }

        else {
          mReader.FreePacket (mOmxPacket);
          mOmxPacket = NULL;
          }
        }

      else
        cOmxClock::sleep (10);
      //}}}
      }

    if (mOmxPacket) {
      //{{{  free omxPacket
      mReader.FreePacket (mOmxPacket);
      mOmxPacket = NULL;
      }
      //}}}
    }

  // exit
  mClock.stop();
  mClock.stateIdle();

  return EXIT_SUCCESS;
  }
//}}}
