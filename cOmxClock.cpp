// cOmxClock.cpp
//{{{  includes
#include "cOmxClock.h"

#include "../shared/utils/cLog.h"
#include <IL/OMX_Broadcom.h>

using namespace std;
//}}}

#define OMX_PRE_ROLL 200
#define TP(speed) ((speed) < 0 || (speed) > 4*DVD_PLAYSPEED_NORMAL)

cOmxClock::cOmxClock() { mOmxClock.init ("OMX.broadcom.clock", OMX_IndexParamOtherInit); }
cOmxClock::~cOmxClock() { mOmxClock.deInit(); }

//{{{
void cOmxClock::stateIdle() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mOmxClock.getState() != OMX_StateIdle)
    mOmxClock.setState (OMX_StateIdle);
  mLastMediaTime = 0.f;
  }
//}}}
//{{{
bool cOmxClock::stateExecute() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mOmxClock.getState() != OMX_StateExecuting) {
    stateIdle();
    if (mOmxClock.setState (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxClock::stateExecute setState");
      return false;
      }
      //}}}
    }

  mLastMediaTime = 0.f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::hdmiClockSync() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
  OMX_INIT_STRUCTURE(latencyTarget);
  latencyTarget.nPortIndex = OMX_ALL;
  latencyTarget.bEnabled = OMX_TRUE;
  latencyTarget.nFilter = 10;
  latencyTarget.nTarget = 0;
  latencyTarget.nShift = 3;
  latencyTarget.nSpeedFactor = -60;
  latencyTarget.nInterFactor = 100;
  latencyTarget.nAdjCap = 100;

  if (mOmxClock.setConfig (OMX_IndexConfigLatencyTarget, &latencyTarget)!= OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, "cOmxClock::hdmiClockSync");
    return false;
    }
    //}}}

  mLastMediaTime = 0.f;
  return true;
  }
//}}}

//{{{
int64_t cOmxClock::getAbsoluteClock() {

  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (((int64_t)now.tv_sec * 1000000000L) + now.tv_nsec) / 1000;
  }
//}}}
//{{{
double cOmxClock::getMediaTime() {

  double pts = 0.0;
  double now = getAbsoluteClock();
  if (now - mLastMediaTimeRead > DVD_MSEC_TO_TIME(100) || mLastMediaTime == 0.0) {
    lock_guard<recursive_mutex> lockGuard (mMutex);

    OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
    OMX_INIT_STRUCTURE(timeStamp);
    timeStamp.nPortIndex = mOmxClock.getInputPort();
    OMX_ERRORTYPE omx_err = mOmxClock.getConfig (OMX_IndexConfigTimeCurrentMediaTime, &timeStamp);
    if (omx_err != OMX_ErrorNone) {
      // error, return
      cLog::log (LOGERROR, __func__);
      return 0;
      }

    pts = fromOmxTime (timeStamp.nTimestamp);
    mLastMediaTime = pts;
    mLastMediaTimeRead = now;
    }

  else {
    double speed = mPause ? 0.0 : (double)mOmxSpeed / DVD_PLAYSPEED_NORMAL;
    pts = mLastMediaTime + (now - mLastMediaTimeRead) * speed;
    }

  return pts;
  }
//}}}
//{{{
double cOmxClock::getClockAdjustment() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = mOmxClock.getInputPort();
  if (mOmxClock.getConfig (OMX_IndexConfigClockAdjustment, &timeStamp) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, __func__);
    return 0;
    }

  return (double)fromOmxTime (timeStamp.nTimestamp);
  }
//}}}

//{{{
void cOmxClock::setClockPorts (OMX_TIME_CONFIG_CLOCKSTATETYPE* clock, bool hasVideo, bool hasAudio) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  clock->nWaitMask = 0;
  if (hasAudio)
    clock->nWaitMask |= OMX_CLOCKPORT0;
  if (hasVideo)
    clock->nWaitMask |= OMX_CLOCKPORT1;
  }
//}}}
//{{{
bool cOmxClock::setReferenceClock (bool hasAudio) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  bool ret = true;

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);
  if (hasAudio)
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;

  if (refClock.eClock != mClock) {
    cLog::log (LOGINFO, "cOmxClock::setReferenceClock %s",
                        (refClock.eClock == OMX_TIME_RefClockVideo) ? "video" : "audio");

    if (mOmxClock.setConfig (OMX_IndexConfigTimeActiveRefClock, &refClock) != OMX_ErrorNone) {
      // error, return
      cLog::log (LOGERROR, __func__);
      ret = false;
      }
    mClock = refClock.eClock;
    }

  mLastMediaTime = 0.f;
  return ret;
  }
//}}}
//{{{
bool cOmxClock::setMediaTime (double pts) {
// Set the media time, so calls to get media time use the updated value,
// useful after a seek so mediatime is updated immediately (rather than waiting for first decoded packet)

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_INDEXTYPE index;
  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = mOmxClock.getInputPort();
  if (mClock == OMX_TIME_RefClockAudio)
    index = OMX_IndexConfigTimeCurrentAudioReference;
  else
    index = OMX_IndexConfigTimeCurrentVideoReference;
  timeStamp.nTimestamp = toOmxTime (pts);
  if (mOmxClock.setConfig (index, &timeStamp) != OMX_ErrorNone) {
    // error return
    cLog::log (LOGERROR, "cOmxClock::setMediaTime ref %s",
                          index == OMX_IndexConfigTimeCurrentAudioReference ? "aud":"vid");
    return false;
    }

  cLog::log  (LOGINFO1, "cOmxClock::setMediaTime %s %.2f",
                        index == OMX_IndexConfigTimeCurrentAudioReference ? "aud":"vid", pts);
  mLastMediaTime = 0.f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::setSpeed (int speed, bool pauseResume) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  cLog::log (LOGINFO1, "cOmxClock::setSpeed %.2f pause_resume:%d",
                       (float)speed / (float)DVD_PLAYSPEED_NORMAL, pauseResume);

  if (pauseResume) {
    OMX_TIME_CONFIG_SCALETYPE scale;
    OMX_INIT_STRUCTURE(scale);
    if (TP (speed))
      scale.xScale = 0; // for trickplay we just pause, and single step
    else
      scale.xScale = (speed << 16) / DVD_PLAYSPEED_NORMAL;
    if (mOmxClock.setConfig (OMX_IndexConfigTimeScale, &scale) != OMX_ErrorNone) {
      cLog::log (LOGERROR, __func__);
      return false;
      }
    }

  if (!pauseResume)
    mOmxSpeed = speed;

  mLastMediaTime = 0.f;
  return true;
  }
//}}}

//{{{
bool cOmxClock::stop() {

  cLog::log (LOGINFO1, "cOmxClock::stop");

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);
  clock.eState = OMX_TIME_ClockStateStopped;
  clock.nOffset = toOmxTime (-1000LL * OMX_PRE_ROLL);
  if (mOmxClock.setConfig (OMX_IndexConfigTimeClockState, &clock) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, __func__);
    return false;
    }
  mState = clock.eState;

  mLastMediaTime = 0.f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::step (int steps /* = 1 */) {

  cLog::log (LOGINFO1, "cOmxClock::step");

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);
  param.nPortIndex = OMX_ALL;
  param.nU32 = steps;
  if (mOmxClock.setConfig (OMX_IndexConfigSingleStep, &param) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, __func__);
    return false;
    }

  cLog::log (LOGINFO1, "cOmxClock::step %d", steps);
  mLastMediaTime = 0.f;

  return true;
  }
//}}}
//{{{
bool cOmxClock::reset (bool hasVideo, bool hasAudio) {

  cLog::log (LOGINFO1, "cOmxClock::reset");

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!setReferenceClock (hasAudio))
    return false;

  if (mState == OMX_TIME_ClockStateStopped) {
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
    OMX_INIT_STRUCTURE(clock);
    clock.eState = OMX_TIME_ClockStateWaitingForStartTime;
    clock.nOffset = toOmxTime (-1000LL * OMX_PRE_ROLL);
    setClockPorts (&clock, hasVideo, hasAudio);

    if (clock.nWaitMask) {
      if (mOmxClock.setConfig (OMX_IndexConfigTimeClockState, &clock) != OMX_ErrorNone) {
        // error, return
        cLog::log (LOGERROR, __func__);
        return false;
        }

      cLog::log (LOGINFO1, "cOmxClock::reset av:%d:%d wait mask %d->%d state:%d->%d",
                           hasAudio, hasVideo, mWaitMask, clock.nWaitMask, mState, clock.eState);
      if (mState != OMX_TIME_ClockStateStopped)
        mWaitMask = clock.nWaitMask;
      mState = clock.eState;
      }
    }

  mLastMediaTime = 0.f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::pause() {

  if (!mPause) {
    lock_guard<recursive_mutex> lockGuard (mMutex);

    if (setSpeed (0, true))
      mPause = true;
    mLastMediaTime = 0.f;
    }

  return mPause;
  }
//}}}
//{{{
bool cOmxClock::resume() {

  if (mPause) {
    lock_guard<recursive_mutex> lockGuard (mMutex);

    if (setSpeed (mOmxSpeed, true))
      mPause = false;

    mLastMediaTime = 0.f;
    }

  return !mPause;
  }
//}}}

//{{{
void cOmxClock::sleep (unsigned int mSecs) {

  struct timespec req;
  req.tv_sec = mSecs / 1000;
  req.tv_nsec = (mSecs % 1000) * 1000000;
  while (nanosleep (&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
  }
//}}}
