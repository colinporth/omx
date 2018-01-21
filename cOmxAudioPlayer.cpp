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
  mCurPts = DVD_NOPTS_VALUE;

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

    // setup current volume settings
    mOmxAudio->setVolume (mCurVolume);
    mOmxAudio->setMute (mMute);
    return true;
    }
  else {
    delete mOmxAudio;
    mOmxAudio = nullptr;
    return false;
    }
  }
//}}}

//{{{
bool cOmxAudioPlayer::decode (cOmxPacket* packet) {

  cLog::log (LOGINFO1, "cOmxAudioPlayer::decode - pts:%6.2f size:%d",
                       packet->mPts/1000000.f, packet->mSize);

  if (packet->mPts != DVD_NOPTS_VALUE)
    mCurPts = packet->mPts;
  else if (packet->mDts != DVD_NOPTS_VALUE)
    mCurPts = packet->mDts;

  return mOmxAudio->decode (packet->mData, packet->mSize, packet->mDts, packet->mPts, mFlushRequested);
  }
//}}}
