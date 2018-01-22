// cOmxAudioPlayer.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}

//{{{
bool cOmxAudioPlayer::open (cOmxClock* clock, const cOmxAudioConfig& config) {

  mClock = clock;
  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;
  mPacketCacheSize = 0;
  mCurPts = kNoPts;

  mAvFormat.av_register_all();

  mOmxAudio = nullptr;
  if (openOmxAudio())
    return true;
  else {
    close();
    return false;
    }
  }
//}}}

// private
//{{{
bool cOmxAudioPlayer::openOmxAudio() {

  mOmxAudio = new cOmxAudio();

  if (mOmxAudio->open (mClock, mConfig)) {
    cLog::log (LOGINFO, "cOmxAudioPlayer::openOmxAudio - chan:" + dec(mConfig.mHints.channels) +
                        " rate:" + dec(mConfig.mHints.samplerate) +
                        " bps:" + dec(mConfig.mHints.bitspersample));
    return true;
    }
  else {
    delete mOmxAudio;
    mOmxAudio = nullptr;
    return false;
    }
  }
//}}}
