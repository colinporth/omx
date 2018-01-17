// cOmxClock.h
//{{{  includes
#pragma once

#include <mutex>

#include "cOmxCore.h"
#include "avLibs.h"
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

  cOmxCore* getOmxClock() { return &mOmxClock; }
  int64_t getAbsoluteClock();
  double getClock (bool interpolated = true) { return getAbsoluteClock(); }

  double getMediaTime();
  double getClockAdjustment();
  int getPlaySpeed() { return mOmxSpeed; };
  bool isPaused() { return mPause; };

  bool setReferenceClock (bool hasAudio);
  bool setMediaTime (double pts);
  bool setSpeed (int speed, bool pauseResume);

  bool stop();
  bool step (int steps = 1);
  bool reset (bool has_video, bool hasAudio);
  bool pause();
  bool resume();

  static void msSleep (unsigned int mSecs);

private:
  std::recursive_mutex mMutex;

  cOmxCore mOmxClock;
  cAvFormat mAvFormat;

  bool mPause = false;
  int mOmxSpeed = DVD_PLAYSPEED_NORMAL;

  OMX_U32 mWaitMask = 0;
  OMX_TIME_CLOCKSTATE mState = OMX_TIME_ClockStateStopped;
  OMX_TIME_REFCLOCKTYPE mClock = OMX_TIME_RefClockNone;

  double mLastMediaTime = 0.f;
  double mLastMediaTimeRead = 0.f;
  };
