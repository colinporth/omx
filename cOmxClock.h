//{{{  includes
#pragma once

#include <mutex>
#include "avLibs.h"
#include "cOmxCoreComponent.h"
//}}}
//{{{  defines
#define DVD_TIME_BASE        1000000
#define DVD_NOPTS_VALUE      (-1LL<<52) // should be possible to represent in both double and __int64

#define DVD_TIME_TO_SEC(x)   ((int)((double)(x) / DVD_TIME_BASE))
#define DVD_TIME_TO_MSEC(x)  ((int)((double)(x) * 1000 / DVD_TIME_BASE))
#define DVD_SEC_TO_TIME(x)   ((double)(x) * DVD_TIME_BASE)
#define DVD_MSEC_TO_TIME(x)  ((double)(x) * DVD_TIME_BASE / 1000)

#define DVD_PLAYSPEED_PAUSE  0       // frame stepping
#define DVD_PLAYSPEED_NORMAL 1000
//}}}

//{{{
inline OMX_TICKS toOmxTime (int64_t pts) {

  OMX_TICKS ticks;
  ticks.nLowPart = pts;
  ticks.nHighPart = pts >> 32;
  return ticks;
  }
//}}}
//{{{
inline int64_t fromOmxTime (OMX_TICKS ticks) {

  int64_t pts = ticks.nLowPart | ((uint64_t)(ticks.nHighPart) << 32);
  return pts;
  }
//}}}

class cOmxClock {
public:
  cOmxClock();
  ~cOmxClock();

  void stateIdle();
  bool stateExecute();
  bool hdmiClockSync();

  cOmxCoreComponent* getOmxClock() { return &m_omx_clock; }
  int64_t getAbsoluteClock();
  double getClock (bool interpolated = true) { return getAbsoluteClock(); }

  double getMediaTime();
  double getClockAdjustment();
  int getPlaySpeed() { return m_omx_speed; };
  bool isPaused() { return m_pause; };

  void setClockPorts (OMX_TIME_CONFIG_CLOCKSTATETYPE* clock, bool has_video, bool has_audio);
  bool setReferenceClock (bool has_audio);
  bool setMediaTime (double pts);
  bool setSpeed (int speed, bool pause_resume);

  bool stop();
  bool step (int steps = 1);
  bool reset (bool has_video, bool has_audio);
  bool pause();
  bool resume();

  static void sleep (unsigned int dwMilliSeconds);

private:
  std::recursive_mutex mMutex;

  cAvFormat mAvFormat;
  cOmxCoreComponent m_omx_clock;

  bool m_pause = false;
  int m_omx_speed = DVD_PLAYSPEED_NORMAL;

  OMX_U32 m_WaitMask = 0;
  OMX_TIME_CLOCKSTATE m_eState = OMX_TIME_ClockStateStopped;
  OMX_TIME_REFCLOCKTYPE m_eClock = OMX_TIME_RefClockNone;

  double m_last_media_time = 0.f;
  double m_last_media_time_read = 0.f;
  };
