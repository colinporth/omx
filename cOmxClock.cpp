// cOmxClock.cpp
//{{{  includes
#include "cOmxClock.h"
#include "cLog.h"
//}}}

#define OMX_PRE_ROLL 200
#define TP(speed) ((speed) < 0 || (speed) > 4*DVD_PLAYSPEED_NORMAL)

//{{{
cOmxClock::cOmxClock() {

  m_pause = false;
  m_omx_speed = DVD_PLAYSPEED_NORMAL;
  m_WaitMask = 0;
  m_eState = OMX_TIME_ClockStateStopped;
  m_eClock = OMX_TIME_RefClockNone;
  m_last_media_time = 0.0f;
  m_last_media_time_read = 0.0f;

  m_pause = false;
  m_omx_clock.Initialize ("OMX.broadcom.clock", OMX_IndexParamOtherInit);
  }
//}}}
//{{{
cOmxClock::~cOmxClock() {

  m_omx_clock.Deinitialize();
  m_omx_speed = DVD_PLAYSPEED_NORMAL;
  m_last_media_time = 0.0f;
  }
//}}}

//{{{
void cOmxClock::stateIdle() {

  cSingleLock lock (m_critSection);

  if (m_omx_clock.GetState() != OMX_StateIdle)
    m_omx_clock.SetStateForComponent (OMX_StateIdle);
  m_last_media_time = 0.0f;
  }
//}}}
//{{{
bool cOmxClock::stateExecute() {

  cSingleLock lock (m_critSection);

  if (m_omx_clock.GetState() != OMX_StateExecuting) {
    stateIdle();
    if (m_omx_clock.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxClock::stateExecute");
      return false;
      }
    }

  m_last_media_time = 0.0f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::hdmiClockSync() {

  cSingleLock lock (m_critSection);

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

  if (m_omx_clock.SetConfig (OMX_IndexConfigLatencyTarget, &latencyTarget)!= OMX_ErrorNone) {
    cLog::Log(LOGERROR, "cOmxClock::hdmiClockSync");
    return false;
    }

  m_last_media_time = 0.0f;
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
  if (now - m_last_media_time_read > DVD_MSEC_TO_TIME(100) || m_last_media_time == 0.0) {
    cSingleLock lock (m_critSection);

    OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
    OMX_INIT_STRUCTURE(timeStamp);
    timeStamp.nPortIndex = m_omx_clock.GetInputPort();
    OMX_ERRORTYPE omx_err = m_omx_clock.GetConfig (OMX_IndexConfigTimeCurrentMediaTime, &timeStamp);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxClock::getMediaTime");
      return 0;
      }

    pts = fromOmxTime (timeStamp.nTimestamp);
    m_last_media_time = pts;
    m_last_media_time_read = now;
    }
  else {
    double speed = m_pause ? 0.0 : (double)m_omx_speed / DVD_PLAYSPEED_NORMAL;
    pts = m_last_media_time + (now - m_last_media_time_read) * speed;
    }

  return pts;
  }
//}}}
//{{{
double cOmxClock::getClockAdjustment() {

  cSingleLock lock (m_critSection);

  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = m_omx_clock.GetInputPort();
  if (m_omx_clock.GetConfig (OMX_IndexConfigClockAdjustment, &timeStamp) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxClock::getClockAdjustment");
    return 0;
    }

  double pts = (double)fromOmxTime (timeStamp.nTimestamp);
  return pts;
  }
//}}}

//{{{
void cOmxClock::setClockPorts (OMX_TIME_CONFIG_CLOCKSTATETYPE* clock, bool has_video, bool has_audio) {

  cSingleLock lock (m_critSection);

  clock->nWaitMask = 0;
  if (has_audio)
    clock->nWaitMask |= OMX_CLOCKPORT0;
  if (has_video)
    clock->nWaitMask |= OMX_CLOCKPORT1;
  }
//}}}
//{{{
bool cOmxClock::setReferenceClock (bool has_audio) {

  cSingleLock lock (m_critSection);

  bool ret = true;

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);
  if (has_audio)
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;
  if (refClock.eClock != m_eClock) {
    cLog::Log (LOGNOTICE, "cOmxClock::setReferenceClock %s",
               refClock.eClock == OMX_TIME_RefClockVideo ? "video" : "audio");

    if (m_omx_clock.SetConfig(OMX_IndexConfigTimeActiveRefClock, &refClock) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxClock::setReferenceClock");
      ret = false;
      }
    m_eClock = refClock.eClock;
    }

  m_last_media_time = 0.0f;
  return ret;
  }
//}}}
//{{{
bool cOmxClock::setMediaTime (double pts) {
// Set the media time, so calls to get media time use the updated value,
// useful after a seek so mediatime is updated immediately (rather than waiting for first decoded packet)

  cSingleLock lock (m_critSection);

  OMX_INDEXTYPE index;
  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = m_omx_clock.GetInputPort();
  if (m_eClock == OMX_TIME_RefClockAudio)
    index = OMX_IndexConfigTimeCurrentAudioReference;
  else
    index = OMX_IndexConfigTimeCurrentVideoReference;
  timeStamp.nTimestamp = toOmxTime (pts);
  if (m_omx_clock.SetConfig (index, &timeStamp) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxClock::setMediaTime ref %s",
               index == OMX_IndexConfigTimeCurrentAudioReference ?
                "OMX_IndexConfigTimeCurrentAudioReference":"OMX_IndexConfigTimeCurrentVideoReference");
    return false;
    }

  cLog::Log  (LOGDEBUG, "cOmxClock::setMediaTime %s %.2f",
              index == OMX_IndexConfigTimeCurrentAudioReference ?
                "OMX_IndexConfigTimeCurrentAudioReference":"OMX_IndexConfigTimeCurrentVideoReference", pts);

  m_last_media_time = 0.0f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::setSpeed (int speed, bool pause_resume) {

  cSingleLock lock (m_critSection);

  cLog::Log (LOGDEBUG, "cOmxClock::setSpeed %.2f pause_resume:%d",
             (float)speed / (float)DVD_PLAYSPEED_NORMAL, pause_resume);

  if (pause_resume) {
    OMX_TIME_CONFIG_SCALETYPE scaleType;
    OMX_INIT_STRUCTURE(scaleType);
    if (TP (speed))
      scaleType.xScale = 0; // for trickplay we just pause, and single step
    else
      scaleType.xScale = (speed << 16) / DVD_PLAYSPEED_NORMAL;
    if (m_omx_clock.SetConfig (OMX_IndexConfigTimeScale, &scaleType) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxClock::setSpeed");
      return false;
      }
    }

  if (!pause_resume)
    m_omx_speed = speed;

  m_last_media_time = 0.0f;
  return true;
  }
//}}}

//{{{
bool cOmxClock::stop() {

  cSingleLock lock (m_critSection);

  cLog::Log (LOGDEBUG, "cOmxClock::stop");

  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);
  clock.eState = OMX_TIME_ClockStateStopped;
  clock.nOffset = toOmxTime (-1000LL * OMX_PRE_ROLL);
  if (m_omx_clock.SetConfig (OMX_IndexConfigTimeClockState, &clock) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxClock::stop");
    return false;
    }
  m_eState = clock.eState;

  m_last_media_time = 0.0f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::step (int steps /* = 1 */) {

  cSingleLock lock (m_critSection);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);
  param.nPortIndex = OMX_ALL;
  param.nU32 = steps;
  if (m_omx_clock.SetConfig (OMX_IndexConfigSingleStep, &param) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxClock::step");
    return false;
    }

  cLog::Log (LOGDEBUG, "cOmxClock::step %d", steps);

  m_last_media_time = 0.0f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::reset (bool has_video, bool has_audio) {

  cSingleLock lock (m_critSection);

  if (!setReferenceClock (has_audio))
    return false;

  if (m_eState == OMX_TIME_ClockStateStopped) {
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
    OMX_INIT_STRUCTURE(clock);
    clock.eState = OMX_TIME_ClockStateWaitingForStartTime;
    clock.nOffset = toOmxTime (-1000LL * OMX_PRE_ROLL);
    setClockPorts (&clock, has_video, has_audio);

    if (clock.nWaitMask) {
      if (m_omx_clock.SetConfig (OMX_IndexConfigTimeClockState, &clock) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxClock::reset");
        return false;
        }

      cLog::Log (LOGDEBUG, "cOmxClock::reset av:%d:%d wait mask %d->%d state:%d->%d",
                 has_audio, has_video, m_WaitMask, clock.nWaitMask, m_eState, clock.eState);
      if (m_eState != OMX_TIME_ClockStateStopped)
        m_WaitMask = clock.nWaitMask;
      m_eState = clock.eState;
      }
    }

  m_last_media_time = 0.0f;
  return true;
  }
//}}}
//{{{
bool cOmxClock::pause() {

  if (!m_pause) {
    cSingleLock lock (m_critSection);

    if (setSpeed (0, true))
      m_pause = true;
    m_last_media_time = 0.0f;
    }

  return m_pause == true;
  }
//}}}
//{{{
bool cOmxClock::resume() {

  if (m_pause) {
    cSingleLock lock (m_critSection);

    if (setSpeed (m_omx_speed, true))
      m_pause = false;

    m_last_media_time = 0.0f;
    }

  return m_pause == false;
  }
//}}}

//{{{
void cOmxClock::sleep (unsigned int dwMilliSeconds) {
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;
  while (nanosleep (&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
  }
//}}}
