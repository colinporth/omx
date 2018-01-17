// cOmxVideoPlayer.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}

//{{{
double normDur (double frameDuration) {
// if the duration is within 20 microseconds of a common duration, use that

  const double durations[] = {
    DVD_TIME_BASE * 1.001 / 24.0, DVD_TIME_BASE / 24.0, DVD_TIME_BASE / 25.0,
    DVD_TIME_BASE * 1.001 / 30.0, DVD_TIME_BASE / 30.0, DVD_TIME_BASE / 50.0,
    DVD_TIME_BASE * 1.001 / 60.0, DVD_TIME_BASE / 60.0};

  double lowestdiff = DVD_TIME_BASE;
  int selected = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
    double diff = fabs (frameDuration - durations[i]);
    if (diff < DVD_MSEC_TO_TIME(0.02) && diff < lowestdiff) {
      selected = i;
      lowestdiff = diff;
      }
    }

  if (selected != -1)
    return durations[selected];
  else
    return frameDuration;
  }
//}}}

//{{{
bool cOmxVideoPlayer::open (cOmxClock* avClock, const cOmxVideoConfig& config) {

  mAvClock = avClock;

  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAvFormat.av_register_all();

  mFrametime = 0;
  mCurrentPts = DVD_NOPTS_VALUE;
  mVideoDelay = 0;

  mAbort = false;
  mFlush = false;
  mPacketCacheSize = 0;

  if (mConfig.mHints.fpsrate && mConfig.mHints.fpsscale)
    mFps = 1000000.f / normDur (1000000.0 * mConfig.mHints.fpsscale / mConfig.mHints.fpsrate);
  else
    mFps = 25.f;

  if (mFps > 100.f || mFps < 5.f) {
    cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate %d", (int)mFps);
    mFps = 25.f;
    }
  mFrametime = 1000000.0 / mFps;

  // open decoder
  mDecoder = new cOmxVideo();
  if (mDecoder->open (mAvClock, mConfig)) {
    cLog::log (LOGINFO, "cOmxPlayerVideo::open %s profile:%d %dx%d %ffps",
               mDecoder->getDecoderName().c_str(), mConfig.mHints.profile,
               mConfig.mHints.width, mConfig.mHints.height, mFps);
    return true;
    }
  else {
    close();
    return false;
    }
  }
//}}}
//{{{
void cOmxVideoPlayer::reset() {

  flush();

  mStreamId = -1;
  mStream = NULL;
  mCurrentPts = DVD_NOPTS_VALUE;
  mFrametime = 0;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;

  mPacketCacheSize = 0;
  mVideoDelay = 0;
  }
//}}}

// protected
//{{{
bool cOmxVideoPlayer::decode (cOmxPacket* packet) {

  double dts = packet->mDts;
  if (dts != DVD_NOPTS_VALUE)
    dts += mVideoDelay;

  double pts = packet->mPts;
  if (pts != DVD_NOPTS_VALUE) {
    pts += mVideoDelay;
    mCurrentPts = pts;
    }

  cLog::log (LOGINFO1, "decode pts" +
                       (packet->mPts == DVD_NOPTS_VALUE) ? "none" : decFrac(packet->mPts / 1000000.f, 6,2,' ') +
                       " curPts" + decFrac(mCurrentPts / 1000000.f, 6,2,' ') +
                       " size" + dec(packet->mSize));

  while ((int)mDecoder->getInputBufferSpace() < packet->mSize) {
    cOmxClock::msSleep (10);
    if (mFlushRequested)
      return true;
    }

  return mDecoder->decode (packet->mData, packet->mSize, dts, pts);
  }
//}}}
