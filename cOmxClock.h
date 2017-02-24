#pragma once
//{{{  includes
#include "cAvLibs.h"
#include "cSingleLock.h"
#include "cOmxCoreComponent.h"
//}}}
//{{{  defines
#define DVD_TIME_BASE 1000000
#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64

#define DVD_TIME_TO_SEC(x)  ((int)((double)(x) / DVD_TIME_BASE))
#define DVD_TIME_TO_MSEC(x) ((int)((double)(x) * 1000 / DVD_TIME_BASE))
#define DVD_SEC_TO_TIME(x)  ((double)(x) * DVD_TIME_BASE)
#define DVD_MSEC_TO_TIME(x) ((double)(x) * DVD_TIME_BASE / 1000)

#define DVD_PLAYSPEED_PAUSE       0       // frame stepping
#define DVD_PLAYSPEED_NORMAL      1000

//}}}

//{{{
static inline OMX_TICKS toOmxTime (int64_t pts) {
  OMX_TICKS ticks;
  ticks.nLowPart = pts;
  ticks.nHighPart = pts >> 32;
  return ticks;
  }
//}}}
//{{{
static inline int64_t fromOmxTime (OMX_TICKS ticks) {
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

protected:
  bool              m_pause;
  int               m_omx_speed;
  OMX_U32           m_WaitMask;
  OMX_TIME_CLOCKSTATE   m_eState;
  OMX_TIME_REFCLOCKTYPE m_eClock;

private:
  cCriticalSection  m_critSection;
  cAvFormat         mAvFormat;
  cOmxCoreComponent m_omx_clock;
  double            m_last_media_time;
  double            m_last_media_time_read;
  };
