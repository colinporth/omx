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
double normaliseFps (int scale, int rate) {
// if the frameDuration is within 20 microseconds of a common duration, use that

  const double durations[] = {
    1.001/24.0, 1.0/24.0, 1.0/25.0, 1.001/30.0, 1.0/30.0, 1.0/50.0, 1.001/60.0, 1.0/60.0 };

  double frameDuration = double(scale) / double(rate);
  double lowestdiff = kPtsScale;
  int selected = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
    double diff = fabs (frameDuration - durations[i]);
    if ((diff < 0.000020) && (diff < lowestdiff)) {
      selected = i;
      lowestdiff = diff;
      }
    }

  if (selected != -1)
    return 1.0 / durations[selected];
  else
    return 1.0 / frameDuration;
  }
//}}}

//{{{
string cOmxVideoPlayer::getDebugString() {
  return dec(mConfig.mHints.width) + "x" + dec(mConfig.mHints.height) + "@" + frac (mFps, 4,2,' ');
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
    mFps = normaliseFps (mConfig.mHints.fpsscale, mConfig.mHints.fpsrate);
    if (mFps > 100.f || mFps < 5.f) {
      cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate " + frac (mFps,6,4,' '));
      mFps = 25.0;
      }
    }
  else
    mFps = 25.0;

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
