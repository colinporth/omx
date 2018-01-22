// cOmxClock.cpp - also has main OMX_init, OMX_deinit
//{{{  includes
#include "cOmxClock.h"

#include "../shared/utils/cLog.h"
#include <IL/OMX_Broadcom.h>

using namespace std;
//}}}

#define OMX_PRE_ROLL 200

//{{{
cOmxClock::cOmxClock() {
  OMX_Init();
  mOmxCore.init ("OMX.broadcom.clock", OMX_IndexParamOtherInit);
  }
//}}}
//{{{
cOmxClock::~cOmxClock() {
  mOmxCore.deInit();
  OMX_Deinit();
  }
//}}}

//{{{
double cOmxClock::getAbsoluteClock() {

  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (((int64_t)now.tv_sec * 1000000000L) + now.tv_nsec) / 1000.0;
  }
//}}}
//{{{
double cOmxClock::getMediaTime() {

  double pts = 0.0;
  double now = getAbsoluteClock();
  if ((now - mLastMediaTimeRead > (kPtsScale / 10.0)) || (mLastMediaTime == 0.0)) {
    lock_guard<recursive_mutex> lockGuard (mMutex);

    OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
    OMX_INIT_STRUCTURE(timeStamp);

    timeStamp.nPortIndex = mOmxCore.getInputPort();
    OMX_ERRORTYPE omx_err = mOmxCore.getConfig (OMX_IndexConfigTimeCurrentMediaTime, &timeStamp);
    if (omx_err) {
      // error, return
      cLog::log (LOGERROR, __func__);
      return 0;
      }

    pts = fromOmxTime (timeStamp.nTimestamp);
    mLastMediaTime = pts;
    mLastMediaTimeRead = now;
    }

  else {
    double speed = mPause ? 0.0 : (double)mOmxSpeed / 1000;
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

  timeStamp.nPortIndex = mOmxCore.getInputPort();
  if (mOmxCore.getConfig (OMX_IndexConfigClockAdjustment, &timeStamp)) {
    // error, return
    cLog::log (LOGERROR, __func__);
    return 0;
    }

  return (double)fromOmxTime (timeStamp.nTimestamp);
  }
//}}}

//{{{
bool cOmxClock::setReferenceClock (bool hasAudio) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  bool ret = true;

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);

  refClock.eClock = hasAudio ? OMX_TIME_RefClockAudio : OMX_TIME_RefClockVideo;
  if (refClock.eClock != mClock) {
    cLog::log (LOGINFO, "cOmxClock::setReferenceClock %s",
                        (refClock.eClock == OMX_TIME_RefClockVideo) ? "vid" : "aud");

    if (mOmxCore.setConfig (OMX_IndexConfigTimeActiveRefClock, &refClock)) {
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

  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);

  timeStamp.nPortIndex = mOmxCore.getInputPort();
  OMX_INDEXTYPE index = (mClock == OMX_TIME_RefClockAudio) ?
      OMX_IndexConfigTimeCurrentAudioReference : OMX_IndexConfigTimeCurrentVideoReference;
  timeStamp.nTimestamp = toOmxTime (pts);
  if (mOmxCore.setConfig (index, &timeStamp)) {
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
                       (float)speed / (float)1000, pauseResume);

  if (pauseResume) {
    OMX_TIME_CONFIG_SCALETYPE scale;
    OMX_INIT_STRUCTURE(scale);

    scale.xScale = (speed << 16) / 1000;
    if (mOmxCore.setConfig (OMX_IndexConfigTimeScale, &scale)) {
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
void cOmxClock::stateIdle() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mOmxCore.getState() != OMX_StateIdle)
    mOmxCore.setState (OMX_StateIdle);
  mLastMediaTime = 0.f;
  }
//}}}
//{{{
bool cOmxClock::stateExecute() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mOmxCore.getState() != OMX_StateExecuting) {
    stateIdle();
    if (mOmxCore.setState (OMX_StateExecuting)) {
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

  OMX_CONFIG_LATENCYTARGETTYPE latency;
  OMX_INIT_STRUCTURE(latency);

  latency.nPortIndex = OMX_ALL;
  latency.bEnabled = OMX_TRUE;
  latency.nFilter = 10;
  latency.nTarget = 0;
  latency.nShift = 3;
  latency.nSpeedFactor = -60;
  latency.nInterFactor = 100;
  latency.nAdjCap = 100;
  if (mOmxCore.setConfig (OMX_IndexConfigLatencyTarget, &latency)!= OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, __func__);
    return false;
    }

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
  if (mOmxCore.setConfig (OMX_IndexConfigTimeClockState, &clock)) {
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
bool cOmxClock::step (int steps) {

  cLog::log (LOGINFO1, "cOmxClock::step");

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  param.nPortIndex = OMX_ALL;
  param.nU32 = steps;
  if (mOmxCore.setConfig (OMX_IndexConfigSingleStep, &param)) {
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
    clock.nWaitMask = 0;
    if (hasAudio)
      clock.nWaitMask |= OMX_CLOCKPORT0;
    if (hasVideo)
      clock.nWaitMask |= OMX_CLOCKPORT1;
    if (clock.nWaitMask) {
      if (mOmxCore.setConfig (OMX_IndexConfigTimeClockState, &clock)) {
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
void cOmxClock::msSleep (unsigned int mSecs) {

  struct timespec ts;
  ts.tv_sec = mSecs / 1000;
  ts.tv_nsec = (mSecs % 1000) * 1000000;
  nanosleep (&ts, NULL);
  }
//}}}
