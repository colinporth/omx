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
double normaliseDuration (double frameDuration) {
// if the duration is within 20 microseconds of a common duration, use that

  const double durations[] = {
    1.001/24.0, 1.0/24.0, 1.0/25.0, 1.001/30.0, 1.0/30.0, 1.0/50.0, 1.001/60.0, 1.0/60.0 };

  double lowestdiff = kPtsScale;
  int selected = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
    double diff = fabs (frameDuration - (durations[i] * kPtsScale));
    if ((diff < 20.0) && (diff < lowestdiff)) {
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
bool cOmxVideoPlayer::open (cOmxClock* clock, const cOmxVideoConfig& config) {

  mClock = clock;
  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;
  mPacketCacheSize = 0;
  mCurPts = kNoPts;

  mAvFormat.av_register_all();

  mDelay = 0;

  if (mConfig.mHints.fpsrate && mConfig.mHints.fpsscale) {
    mFps = 1000000.f / normaliseDuration (1000000.0 * mConfig.mHints.fpsscale / mConfig.mHints.fpsrate);
    if (mFps > 100.f || mFps < 5.f) {
      cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate %d", (int)mFps);
      mFps = 25.f;
      }
    }
  else
    mFps = 25.f;

  if (mConfig.mHints.codec == AV_CODEC_ID_MPEG2VIDEO) {
    cLog::log (LOGNOTICE, "cOmxPlayerVideo::open - no hw mpeg2 decoder - implement swDecoder");
    return false;
    }
  else {
    // open hw decoder
    mOmxVideo = new cOmxVideo();
    if (mOmxVideo->open (mClock, mConfig)) {
      cLog::log (LOGINFO, "cOmxPlayerVideo::open %s profile:%d %dx%d %ffps",
                 mOmxVideo->getDecoderName().c_str(), mConfig.mHints.profile,
                 mConfig.mHints.width, mConfig.mHints.height, mFps);
      return true;
      }
    else {
      close();
      return false;
      }
    }
  }
//}}}
//{{{
void cOmxVideoPlayer::reset() {

  flush();

  mStreamId = -1;
  mStream = NULL;
  mCurPts = kNoPts;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;

  mPacketCacheSize = 0;
  mDelay = 0;
  }
//}}}

// protected
//{{{
bool cOmxVideoPlayer::decodeDecoder (uint8_t* data, int size, double dts, double pts, std::atomic<bool>& flushRequested) {
  return mOmxVideo->decode (data, size, dts, pts, mFlushRequested);
  }
//}}}
