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
cOmxPlayerAudio::cOmxPlayerAudio() : cOmxPlayer(){

  pthread_mutex_init (&mLock, nullptr);
  pthread_mutex_init (&mLockDecoder, nullptr);
  pthread_cond_init (&mPacketCond, nullptr);

  mFlushRequested = false;
  }
//}}}
//{{{
cOmxPlayerAudio::~cOmxPlayerAudio() {

  close();

  pthread_cond_destroy (&mPacketCond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerAudio::isPassthrough (cOmxStreamInfo hints) {

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
bool cOmxPlayerAudio::open (cOmxClock* avClock, const cOmxAudioConfig& config, cOmxReader* omxReader) {

  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAvClock = avClock;
  mOmxReader = omxReader;

  mAvFormat.av_register_all();

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;
  mPassthrough = false;
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
//{{{
void cOmxPlayerAudio::submitEOS() {

  mOmxAudio->submitEOS();
  }
//}}}
//{{{
void cOmxPlayerAudio::flush() {

  mFlushRequested = true;

  lock();
  lockDecoder();

  if (mSwAudio)
    mSwAudio->reset();

  mFlushRequested = false;
  flushPackets();
  mOmxAudio->flush();

  unLockDecoder();
  unLock();
  }
//}}}
//{{{
bool cOmxPlayerAudio::close() {

  mAbort = true;
  flush();

  lock();
  pthread_cond_broadcast (&mPacketCond);
  unLock();

  delete mSwAudio;
  mSwAudio = nullptr;
  delete mOmxAudio;
  mOmxAudio = nullptr;

  mStreamId = -1;
  mCurrentPts = DVD_NOPTS_VALUE;
  mStream = nullptr;

  return true;
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

  if (mConfig.mPassthrough)
    mPassthrough = isPassthrough (mConfig.mHints);
  if (!mPassthrough && mConfig.mHwDecode)
    mHwDecode = cOmxAudio::hwDecode (mConfig.mHints.codec);
  if (mPassthrough)
    mHwDecode = false;

  if (mOmxAudio->init (mAvClock, mConfig, mSwAudio->getChannelMap(), mSwAudio->getBitsPerSample())) {
    cLog::log (LOGINFO, "cOmxPlayerAudio::openOmxAudio " +
               string(mPassthrough ? " passThru" : "") +
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

  if (!mOmxReader->isActive (OMXSTREAM_AUDIO, packet->stream_index))
    return true;

  auto channels = packet->hints.channels;
  auto old_bitrate = mConfig.mHints.bitrate;
  auto new_bitrate = packet->hints.bitrate;

  // only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3
  if (mConfig.mHints.codec != AV_CODEC_ID_DTS &&
      mConfig.mHints.codec != AV_CODEC_ID_AC3 && mConfig.mHints.codec != AV_CODEC_ID_EAC3)
    new_bitrate = old_bitrate = 0;

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = (channels != mConfig.mHints.channels) ||
                      (packet->hints.bitspersample != mConfig.mHints.bitspersample) ||
                      (old_bitrate != new_bitrate);

  if ((packet->hints.codec != mConfig.mHints.codec) ||
      (packet->hints.samplerate != mConfig.mHints.samplerate) ||
      (!mPassthrough && minor_change)) {
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

  cLog::log (LOGINFO1, "decode - pts:%6.2f size:%d", packet->pts / 1000000.f, packet->size);

  if (packet->pts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->pts;
  else if (packet->dts != DVD_NOPTS_VALUE)
    mCurrentPts = packet->dts;

  const uint8_t* data_dec = packet->data;
  int data_len = packet->size;

  if (!mPassthrough && !mHwDecode) {
    auto dts = packet->dts;
    auto pts = packet->pts;
    while (data_len > 0) {
      int len = mSwAudio->decode((unsigned char*)data_dec, data_len, dts, pts);
      if ((len < 0) || (len > data_len)) {
        mSwAudio->reset();
        break;
        }

      data_dec += len;
      data_len -= len;
      uint8_t* decoded;
      int decoded_size = mSwAudio->getData (&decoded, dts, pts);
      if (decoded_size <= 0)
        continue;

      while ((int)mOmxAudio->getSpace() < decoded_size) {
        cOmxClock::sleep (10);
        if (mFlushRequested)
          return true;
        }

      int ret = mOmxAudio->addPackets (decoded, decoded_size, dts, pts, mSwAudio->getFrameSize());
      if (ret != decoded_size)
        cLog::log (LOGERROR, "error ret %d decoded_size %d", ret, decoded_size);
      }
    }
  else {
    while (mOmxAudio->getSpace() < packet->size) {
      cOmxClock::sleep (10);
      if (mFlushRequested)
        return true;
      }

    mOmxAudio->addPackets (packet->data, packet->size, packet->dts, packet->pts, 0);
    }

  return true;
  }
//}}}
