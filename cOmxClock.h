// cOmxClock.h
//{{{  includes
#pragma once

#include <mutex>

#include "cOmxCore.h"
#include "avLibs.h"
//}}}
const double kNoPts = -1LL << 52;
const double kPtsScale = 1000000.0;

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

  cOmxCore* getOmxCore() { return &mOmxCore; }
  double getAbsoluteClock();
  double getMediaTime();
  double getClockAdjustment();
  double getPlaySpeed() { return mSpeed; };
  bool isPaused() { return mPause; };

  bool setReferenceClock (bool hasAudio);
  bool setMediaTime (double pts);
  bool setSpeed (double speed, bool pauseResume);

  void stateIdle();
  bool stateExecute();
  bool hdmiClockSync();

  bool stop();
  bool step (int steps);
  bool reset (bool has_video, bool hasAudio);
  bool pause();
  bool resume();

  void msSleep (unsigned int mSecs);

private:
  std::recursive_mutex mMutex;

  cOmxCore mOmxCore;
  cAvFormat mAvFormat;

  bool mPause = false;
  double mSpeed = 1.0;

  OMX_U32 mWaitMask = 0;
  OMX_TIME_CLOCKSTATE mState = OMX_TIME_ClockStateStopped;
  OMX_TIME_REFCLOCKTYPE mClock = OMX_TIME_RefClockNone;

  double mLastMediaTime = 0.f;
  double mLastMediaTimeRead = 0.f;
  };
