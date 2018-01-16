// cOmxPlayerAudio.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}

//{{{
bool cOmxPlayerAudio::isPassThru (cOmxStreamInfo hints) {

  if (mConfig.mDevice == "omx:local")
    return false;
  if (hints.codec == AV_CODEC_ID_AC3)
    return true;
  if (hints.codec == AV_CODEC_ID_EAC3)
    return true;
  if (hints.codec == AV_CODEC_ID_DTS)
    return true;

  return false;
  }
//}}}

//{{{
bool cOmxPlayerAudio::open (cOmxClock* avClock, const cOmxAudioConfig& config) {

  mAvClock = avClock;

  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAvFormat.av_register_all();

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;
  mPassThru = false;
  mHwDecode = false;
  mCurrentPts = DVD_NOPTS_VALUE;
  mPacketCacheSize = 0;

  mSwAudio = nullptr;
  mOmxAudio = nullptr;
  if (openSwAudio() && openOmxAudio())
    return true;
  else {
    close();
    return false;
    }
  }
//}}}

// private
//{{{
bool cOmxPlayerAudio::openSwAudio() {

  mSwAudio = new cSwAudio();
  if (!mSwAudio->open (mConfig.mHints, mConfig.mLayout)) {
    delete mSwAudio;
    mSwAudio = nullptr;
    return false;
    }

  return true;
  }
//}}}
//{{{
bool cOmxPlayerAudio::openOmxAudio() {

  mOmxAudio = new cOmxAudio();

  if (mConfig.mPassThru)
    mPassThru = isPassThru (mConfig.mHints);
  if (!mPassThru && mConfig.mHwDecode)
    mHwDecode = cOmxAudio::hwDecode (mConfig.mHints.codec);
  if (mPassThru)
    mHwDecode = false;

  if (mOmxAudio->init (mAvClock, mConfig, mSwAudio->getChannelMap(), mSwAudio->getBitsPerSample())) {
    cLog::log (LOGINFO, "cOmxPlayerAudio::openOmxAudio " +
               string(mPassThru ? " passThru" : "") +
               " chan:" + dec(mConfig.mHints.channels) +
               " rate:" + dec(mConfig.mHints.samplerate) +
               " bps:" + dec(mConfig.mHints.bitspersample));

    // setup current volume settings
    mOmxAudio->setVolume (mCurrentVolume);
    mOmxAudio->setMute (mMute);
    mOmxAudio->setDynamicRangeCompression (mDrc);
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
bool cOmxPlayerAudio::decode (OMXPacket* packet) {

  auto channels = packet->hints.channels;
  auto oldBitrate = mConfig.mHints.bitrate;
  auto newBitrate = packet->hints.bitrate;

  // only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3
  if (mConfig.mHints.codec != AV_CODEC_ID_DTS &&
      mConfig.mHints.codec != AV_CODEC_ID_AC3 &&
      mConfig.mHints.codec != AV_CODEC_ID_EAC3)
    newBitrate = oldBitrate = 0;

  // for passThru we only care about the codec and the samplerate
  bool minorChange = (channels != mConfig.mHints.channels) ||
                     (packet->hints.bitspersample != mConfig.mHints.bitspersample) ||
                     (oldBitrate != newBitrate);
  if ((!mPassThru && minorChange) ||
      (packet->hints.codec != mConfig.mHints.codec) ||
      (packet->hints.samplerate != mConfig.mHints.samplerate)) {
    //{{{  change decoders
    cLog::log (LOGINFO, "Decode C : %d %d %d %d %d",
                        mConfig.mHints.codec, mConfig.mHints.channels, mConfig.mHints.samplerate,
                        mConfig.mHints.bitrate, mConfig.mHints.bitspersample);
    cLog::log (LOGINFO, "Decode N : %d %d %d %d %d",
                        packet->hints.codec, channels, packet->hints.samplerate,
                        packet->hints.bitrate, packet->hints.bitspersample);
    mConfig.mHints = packet->hints;

    delete mSwAudio;
    mSwAudio = nullptr;
    delete mOmxAudio;
    mOmxAudio = nullptr;
    if (!(openSwAudio() && openOmxAudio()))
      return false;
    }
    //}}}

  cLog::log (LOGINFO1, "decode - pts:%6.2f size:%d", packet->pts / 1000000.f, packet->size);

  if (packet->pts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->pts;
  else if (packet->dts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->dts;

  uint8_t* data = packet->data;
  int size = packet->size;

  if (mPassThru || mHwDecode) {
    //{{{  hw action
    while (mOmxAudio->getSpace() < packet->size) {
      cOmxClock::sleep (10);
      if (mFlushRequested)
        return true;
      }
    mOmxAudio->addPacket (packet->data, packet->size, packet->dts, packet->pts, 0);
    }
    //}}}
  else {
    // sw decode
    auto dts = packet->dts;
    auto pts = packet->pts;
    while (size > 0) {
      // decode packet
      int len = mSwAudio->decode (data, size, dts, pts);
      if ((len < 0) || (len > size)) {
        mSwAudio->reset();
        break;
        }
      data += len;
      size -= len;

      // add decoded data to hw
      uint8_t* decodedData;
      auto decodedSize = mSwAudio->getData (&decodedData, dts, pts);
      if (decodedSize > 0) {
        while (mOmxAudio->getSpace() < decodedSize) {
          cOmxClock::sleep (10);
          if (mFlushRequested)
            return true;
          }
        mOmxAudio->addPacket (decodedData, decodedSize, dts, pts, mSwAudio->getFrameSize());
        }
      }
    }

  return true;
  }
//}}}
