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
  mCurrentPts = DVD_NOPTS_VALUE;

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

  if (mOmxAudio->open (mConfig)) {
    if (mOmxAudio->init (mClock, mConfig)) {
      cLog::log (LOGINFO, "cOmxAudioPlayer::openOmxAudio - chan:" + dec(mConfig.mHints.channels) +
                          " rate:" + dec(mConfig.mHints.samplerate) +
                          " bps:" + dec(mConfig.mHints.bitspersample));

      // setup current volume settings
      mOmxAudio->setVolume (mCurrentVolume);
      mOmxAudio->setMute (mMute);
      return true;
      }
    }

  delete mOmxAudio;
  mOmxAudio = nullptr;
  return false;
  }
//}}}

//{{{
bool cOmxAudioPlayer::decode (cOmxPacket* packet) {

  if ((packet->mHints.channels != mConfig.mHints.channels)  ||
      (packet->mHints.codec != mConfig.mHints.codec) ||
      (packet->mHints.bitrate != mConfig.mHints.bitrate) ||
      (packet->mHints.samplerate != mConfig.mHints.samplerate) ||
      (packet->mHints.bitspersample != mConfig.mHints.bitspersample)) {
    //{{{  change decoders
    cLog::log (LOGINFO, "Decode C : %d %d %d %d %d",
                        mConfig.mHints.codec, mConfig.mHints.channels, mConfig.mHints.samplerate,
                        mConfig.mHints.bitrate, mConfig.mHints.bitspersample);
    cLog::log (LOGINFO, "Decode N : %d %d %d %d %d",
                        packet->mHints.codec, packet->mHints.channels, packet->mHints.samplerate,
                        packet->mHints.bitrate, packet->mHints.bitspersample);
    mConfig.mHints = packet->mHints;

    //delete mOmxAudio;
    //mOmxAudio = nullptr;
    //if (!openOmxAudio())
    //  return false;
    }
    //}}}

  cLog::log (LOGINFO1, "cOmxAudioPlayer::decode - pts:%6.2f size:%d",
                       packet->mPts/1000000.f, packet->mSize);

  if (packet->mPts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->mPts;
  else if (packet->mDts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->mDts;

  return mOmxAudio->decode (packet->mData, packet->mSize, packet->mDts, packet->mPts, mFlushRequested);
  }
//}}}
