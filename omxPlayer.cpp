// omxPlayer.cpp
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

#define AV_NOWARN_DEPRECATED

#include "cLog.h"
#include "cPcmRemap.h"

#include "cBcmHost.h"
#include "cOmx.h"
#include "avLibs.h"

#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cAudio.h"
#include "cVideo.h"

#include "cKeyboard.h"

#include "version.h"
//}}}
//{{{  vars
volatile sig_atomic_t g_abort = false;

cKeyboard mKeyboard;

cBcmHost m_BcmHost;
cOmx m_OMX;
cOmxClock mClock;
cOmxReader mReader;

cOmxPlayerVideo mPlayerVideo;
cOmxVideoConfig mVideoConfig;
cOmxPlayerAudio mPlayerAudio;
cOmxAudioConfig mAudioConfig;
OMXPacket* mOmxPacket = NULL;

bool m_stop = false;
bool m_has_video = false;
bool m_has_audio = false;

enum PCMChannels* m_pChannelMap = NULL;
//}}}

//{{{
bool exists (const std::string& path) {

  struct stat buf;
  auto error = stat (path.c_str(), &buf);
  return !error || errno != ENOENT;
  }
//}}}
//{{{
bool isURL (const std::string& str) {

  auto result = str.find ("://");
  if (result == std::string::npos || result == 0)
    return false;

  for (size_t i = 0; i < result; ++i)
    if (!isalpha (str[i]))
      return false;

  return true;
  }
//}}}
//{{{
bool isPipe (const std::string& str) {

  if (str.compare (0, 5, "pipe:") == 0)
    return true;
  return false;
  }
//}}}

//{{{
void sig_handler (int s) {

  if (s == SIGINT && !g_abort) {
    signal (SIGINT, SIG_DFL);
    g_abort = true;
    return;
    }

  signal (SIGABRT, SIG_DFL);
  signal (SIGSEGV, SIG_DFL);
  signal (SIGFPE, SIG_DFL);
  mKeyboard.Close();

  abort();
  }
//}}}
//{{{
void flushStreams (double pts) {

  mClock.stop();
  mClock.pause();

  if (m_has_video)
    mPlayerVideo.Flush();
  if (m_has_audio)
    mPlayerAudio.Flush();

  if (pts != DVD_NOPTS_VALUE)
    mClock.setMediaTime (pts);

  if (mOmxPacket) {
    mReader.FreePacket (mOmxPacket);
    mOmxPacket = NULL;
    }
  }
//}}}
//{{{
float get_display_aspect_ratio (HDMI_ASPECT_T aspect) {

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

  printf ("omxplayer %s\n", VERSION_DATE);
  cLog::Init ("./", LOG_LEVEL_DEBUG);
  cLog::Log (LOGNOTICE, "omxPlayer %s", argv[1]);

  mKeyboard.setKeymap (cKeyConfig::buildDefaultKeymap());
  //{{{  signals
  signal (SIGSEGV, sig_handler);
  signal (SIGABRT, sig_handler);
  signal (SIGFPE, sig_handler);
  signal (SIGINT, sig_handler);
  //}}}

  // vars
  bool m_send_eos = false;
  double m_incr = 0;
  double last_seek_pos = 0;
  float m_latency = 0.0f;
  long m_Volume = 0;
  bool m_Pause = false;

  std::string fileName = argv[1];
  if ((isURL (fileName) || isPipe (fileName) || exists (fileName)) &&
      mReader.Open (fileName.c_str(), true, mAudioConfig.is_live, 10.0f)) {
    // ok fileName
    mClock.stateIdle();
    mClock.stop();
    mClock.pause();

    m_has_audio = mReader.AudioStreamCount();
    m_has_video = mReader.VideoStreamCount();
    mReader.GetHints (OMXSTREAM_AUDIO, mAudioConfig.hints);
    mReader.GetHints (OMXSTREAM_VIDEO, mVideoConfig.hints);

    blankBackground (0);
    //{{{  get display aspect
    TV_DISPLAY_STATE_T current_tv_state;
    memset (&current_tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
    m_BcmHost.vc_tv_get_display_state (&current_tv_state);
    mVideoConfig.display_aspect = get_display_aspect_ratio ((HDMI_ASPECT_T)current_tv_state.display.hdmi.aspect_ratio);
    mVideoConfig.display_aspect *= (float)current_tv_state.display.hdmi.height / (float)current_tv_state.display.hdmi.width;
    //}}}
    m_stop = m_has_video && !mPlayerVideo.Open (&mClock, mVideoConfig);

    mAudioConfig.device = "omx:local";
    if (mAudioConfig.device == "omx:alsa" && mAudioConfig.subdevice.empty())
      mAudioConfig.subdevice = "default";

    if (m_has_audio) {
      m_stop |= !mPlayerAudio.Open (&mClock, mAudioConfig, &mReader);
      mPlayerAudio.SetVolume (pow (10, m_Volume / 2000.0));
      }
    float m_threshold = mAudioConfig.is_live ? 0.7f : 0.2f;

    //mPlayerVideo.SetAlpha (128);
    mClock.reset (m_has_video, m_has_audio);
    mClock.stateExecute();

    bool sentStarted = true;
    double m_last_check_time = 0.0;
    while (!m_stop && !g_abort && !mPlayerAudio.Error()) {
      double now = mClock.getAbsoluteClock();
      bool update = (m_last_check_time == 0.0) || (m_last_check_time + DVD_MSEC_TO_TIME(20) <= now);
      if (update) {
        //{{{  update
        m_last_check_time = now;

        // decode keyboard
        switch (mKeyboard.getEvent()) {
          case cKeyConfig::ACTION_STEP: mClock.step(); printf ("Step\n"); break;
          //{{{
          case cKeyConfig::ACTION_PREVIOUS_AUDIO:
            if (m_has_audio) {
              int new_index = mReader.GetAudioIndex() - 1;
              if (new_index >= 0)
                mReader.SetActiveStream (OMXSTREAM_AUDIO, new_index);
              }
            break;
          //}}}
          //{{{
          case cKeyConfig::ACTION_NEXT_AUDIO:
            if (m_has_audio)
              mReader.SetActiveStream (OMXSTREAM_AUDIO, mReader.GetAudioIndex() + 1);
            break;
          //}}}
          case cKeyConfig::ACTION_SEEK_BACK_SMALL: if (mReader.CanSeek()) m_incr = -30.0; break;
          case cKeyConfig::ACTION_SEEK_FORWARD_SMALL: if (mReader.CanSeek()) m_incr = 30.0; break;
          case cKeyConfig::ACTION_SEEK_FORWARD_LARGE: if (mReader.CanSeek()) m_incr = 600.0; break;
          case cKeyConfig::ACTION_SEEK_BACK_LARGE: if (mReader.CanSeek()) m_incr = -600.0; break;
          case cKeyConfig::ACTION_PLAYPAUSE: m_Pause = !m_Pause; break;
          //{{{
          case cKeyConfig::ACTION_DECREASE_VOLUME:
            m_Volume -= 300;
            mPlayerAudio.SetVolume(pow(10, m_Volume / 2000.0));
            printf("Current Volume: %.2fdB\n", m_Volume / 100.0f);
            break;
          //}}}
          //{{{
          case cKeyConfig::ACTION_INCREASE_VOLUME:
            m_Volume += 300;
            mPlayerAudio.SetVolume(pow(10, m_Volume / 2000.0));
            printf("Current Volume: %.2fdB\n", m_Volume / 100.0f);
            break;
          //}}}
          case cKeyConfig::ACTION_EXIT: g_abort = true; m_stop = true; break;
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
          unsigned t = (unsigned)(startpts*1e-6);
          //auto dur = mReader.GetStreamLength() / 1000;
          printf ("Seek to: %02d:%02d:%02d\n", (t/3600), (t/60)%60, t%60);
          flushStreams (startpts);
          }

        sentStarted = false;
        if (mReader.IsEof() || (m_has_video && !mPlayerVideo.Reset()))
          break;

        cLog::Log (LOGDEBUG, "Seeked to %.0f %.0f %.0f\n",
                   DVD_MSEC_TO_TIME(seek_pos), startpts, mClock.getMediaTime());

        mClock.pause();
        m_incr = 0;
        }
        //}}}
      if (update) {
        //{{{  update
        /* when the video/audio fifos are low, we pause clock, when high we resume */
        double stamp = mClock.getMediaTime();
        double audio_pts = mPlayerAudio.GetCurrentPTS();
        double video_pts = mPlayerVideo.GetCurrentPTS();

        if (0 && mClock.isPaused()) {
          //{{{  paused stamp adjust
          double old_stamp = stamp;
          if (audio_pts != DVD_NOPTS_VALUE && (stamp == 0 || audio_pts < stamp))
            stamp = audio_pts;

          if (video_pts != DVD_NOPTS_VALUE && (stamp == 0 || video_pts < stamp))
            stamp = video_pts;

          if (old_stamp != stamp) {
            mClock.setMediaTime(stamp);
            stamp = mClock.getMediaTime();
            }
          }
          //}}}

        float audio_fifo = audio_pts == DVD_NOPTS_VALUE ? 0.0f : audio_pts / DVD_TIME_BASE - stamp * 1e-6;
        float video_fifo = video_pts == DVD_NOPTS_VALUE ? 0.0f : video_pts / DVD_TIME_BASE - stamp * 1e-6;
        float threshold = std::min (0.1f, (float)mPlayerAudio.GetCacheTotal() * 0.1f);

        bool audio_fifo_low = false;
        bool audio_fifo_high = false;
        if (audio_pts != DVD_NOPTS_VALUE) {
          audio_fifo_low = m_has_audio && audio_fifo < threshold;
          audio_fifo_high = !m_has_audio || (audio_pts != DVD_NOPTS_VALUE && audio_fifo > m_threshold);
          }

        bool video_fifo_low = false;
        bool video_fifo_high = false;
        if (video_pts != DVD_NOPTS_VALUE) {
          video_fifo_low = m_has_video && video_fifo < threshold;
          video_fifo_high = !m_has_video || (video_pts != DVD_NOPTS_VALUE && video_fifo > m_threshold);
          }

        cLog::Log (LOGINFO, "p:%d m:%.0f a:%.0f v:%.0f a:%.2f v:%.2f th:%.2f %d%d%d%d A:%d%% V:%d%% d:%.2f c:%.2f\n",
                   mClock.isPaused(),
                   stamp, 
                   audio_pts, video_pts,
                   (audio_pts == DVD_NOPTS_VALUE) ? 0.0 : audio_fifo,
                   (video_pts == DVD_NOPTS_VALUE) ? 0.0 : video_fifo,
                   m_threshold, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high,
                   mPlayerAudio.GetLevel(), mPlayerVideo.GetLevel(),
                   mPlayerAudio.GetDelay(), (float)mPlayerAudio.GetCacheTotal());

        // keep latency under control by adjusting clock (and so resampling audio)
        if (mAudioConfig.is_live) {
          //{{{  live audio
          float latency = DVD_NOPTS_VALUE;
          if (m_has_audio && audio_pts != DVD_NOPTS_VALUE)
            latency = audio_fifo;
          else if (!m_has_audio && m_has_video && video_pts != DVD_NOPTS_VALUE)
            latency = video_fifo;

          if (!m_Pause && latency != DVD_NOPTS_VALUE) {
            if (mClock.isPaused()) {
              if (latency > m_threshold) {
                cLog::Log (LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n",
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
              cLog::Log (LOGDEBUG, "Live: %.2f (%.2f) S:%.3f T:%.2f\n", m_latency, latency, speed, m_threshold);
              }
            }
          }
          //}}}
        else if (!m_Pause && (mReader.IsEof() || mOmxPacket || (audio_fifo_high && video_fifo_high))) {
          //{{{  pause
          if (mClock.isPaused()) {
            cLog::Log (LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n",
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
              m_threshold = std::min(2.0f*m_threshold, 16.0f);
            cLog::Log (LOGDEBUG, "Pause %.2f,%.2f (%d,%d,%d,%d) %.2f\n",
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
        cLog::Log (LOGDEBUG, "omxPlayer - reset");
        mClock.reset (m_has_video, m_has_audio);
        sentStarted = true;
        }
        //}}}
      //{{{  read
      if (!mOmxPacket)
        mOmxPacket = mReader.Read();

      if (mOmxPacket)
        m_send_eos = false;

      if (mReader.IsEof() && !mOmxPacket) {
        // demuxer EOF, but may have not played out data yet
        if ( (m_has_video && mPlayerVideo.GetCached()) || (m_has_audio && mPlayerAudio.GetCached()) ) {
          cOmxClock::sleep (10);
          continue;
          }
        if (!m_send_eos && m_has_video)
          mPlayerVideo.SubmitEOS();
        if (!m_send_eos && m_has_audio)
          mPlayerAudio.SubmitEOS();

        m_send_eos = true;
        if ((m_has_video && !mPlayerVideo.IsEOS()) || (m_has_audio && !mPlayerAudio.IsEOS())) {
          cOmxClock::sleep (10);
          continue;
          }
        break;
        }

      if (m_has_video && mOmxPacket && mReader.IsActive (OMXSTREAM_VIDEO, mOmxPacket->stream_index)) {
        if (mPlayerVideo.AddPacket (mOmxPacket))
          mOmxPacket = NULL;
        else
          cOmxClock::sleep (10);
        }
      else if (m_has_audio && mOmxPacket && mOmxPacket->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (mPlayerAudio.AddPacket (mOmxPacket))
          mOmxPacket = NULL;
        else
          cOmxClock::sleep (10);
        }
      else if (mOmxPacket) {
        mReader.FreePacket (mOmxPacket);
        mOmxPacket = NULL;
        }
      else
        cOmxClock::sleep (10);
      //}}}
      }
    }

  // exit
  unsigned timeSecs = (unsigned)(mClock.getMediaTime() * 1000000);
  printf ("Stopped at: %02d:%02d:%02d\n", (timeSecs / 3600), (timeSecs / 60) % 60, timeSecs % 60);

  mClock.stop();
  mClock.stateIdle();
  mPlayerVideo.Close();
  mPlayerAudio.Close();
  mKeyboard.Close();

  if (mOmxPacket) {
    mReader.FreePacket (mOmxPacket);
    mOmxPacket = NULL;
    }
  mReader.Close();

  return EXIT_SUCCESS;
  }
//}}}
