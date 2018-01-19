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

  if (mOmxAudio->open (mConfig, mConfig.mHints, mConfig.mLayout)) {
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

    delete mOmxAudio;
    mOmxAudio = nullptr;
    if (!openOmxAudio())
      return false;
    }
    //}}}

  cLog::log (LOGINFO1, "cOmxAudioPlayer::decode - pts:%6.2f size:%d",
                       packet->mPts/1000000.f, packet->mSize);

  if (packet->mPts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->mPts;
  else if (packet->mDts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->mDts;

  auto data = packet->mData;
  auto size = packet->mSize;
  auto dts = packet->mDts;
  auto pts = packet->mPts;
  while (size > 0) {
    int len = mOmxAudio->swDecode (data, size, dts, pts);
    if ((len < 0) || (len > size)) {
      mOmxAudio->reset();
      break;
      }
    data += len;
    size -= len;

    uint8_t* decodedData;
    auto decodedSize = mOmxAudio->getData (&decodedData, dts, pts);
    if (decodedSize > 0) {
      while (mOmxAudio->getSpace() < decodedSize) {
        mClock->msSleep (10);
        if (mFlushRequested)
          return true;
        }
      mOmxAudio->addDecodedData (decodedData, decodedSize, dts, pts, mOmxAudio->getFrameSize());
      }
    }

  return true;
  }
//}}}
