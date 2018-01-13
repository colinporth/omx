// cOmxPlayerAudio.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "cAudio.h"
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"

using namespace std;
//}}}

//{{{
cOmxPlayerAudio::cOmxPlayerAudio() {

  pthread_mutex_init (&mLock, nullptr);
  pthread_mutex_init (&mLockDecoder, nullptr);

  pthread_cond_init (&mPacketCond, nullptr);
  pthread_cond_init (&mAudioCond, nullptr);

  mFlushRequested = false;
  }
//}}}
//{{{
cOmxPlayerAudio::~cOmxPlayerAudio() {

  close();

  pthread_cond_destroy (&mAudioCond);
  pthread_cond_destroy (&mPacketCond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerAudio::isPassthrough (cOmxStreamInfo hints) {

  if (mConfig.device == "omx:local")
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

  mAvClock = avClock;
  mConfig = config;
  mOmxReader = omxReader;

  mAvFormat.av_register_all();

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;
  mPassthrough = false;
  mHwDecode = false;
  mICurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;

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
void cOmxPlayerAudio::run() {

  cLog::setThreadName ("aud ");

  OMXPacket* packet = nullptr;
  while (true) {
    lock();
    if (!mAbort && mPackets.empty())
      pthread_cond_wait (&mPacketCond, &mLock);

    if (mAbort) {
      unLock();
      break;
      }

    if (mFlush && packet) {
      cOmxReader::freePacket (packet);
      mFlush = false;
      }
    else if (!packet && !mPackets.empty()) {
      packet = mPackets.front();
      mCachedSize -= packet->size;
      mPackets.pop_front();
      }
    unLock();

    lockDecoder();
    if (mFlush && packet) {
      cOmxReader::freePacket (packet);
      mFlush = false;
      }
    else if (packet && decode (packet))
      cOmxReader::freePacket (packet);
    unLockDecoder();
    }

  cOmxReader::freePacket (packet);

  cLog::log (LOGNOTICE, "exit");
  }
//}}}
//{{{
bool cOmxPlayerAudio::addPacket (OMXPacket* packet) {

  if (!mAbort &&
      ((mCachedSize + packet->size) < mConfig.queue_size * 1024 * 1024)) {
    lock();
    mCachedSize += packet->size;
    mPackets.push_back (packet);
    unLock();

    pthread_cond_broadcast (&mPacketCond);
    return true;
    }

  return false;
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
  mFlush = true;
  while (!mPackets.empty()) {
    auto packet = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (packet);
    }

  mICurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;
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
  mICurrentPts = DVD_NOPTS_VALUE;
  mStream = nullptr;

  return true;
  }
//}}}

// private
//{{{
bool cOmxPlayerAudio::openSwAudio() {

  mSwAudio = new cSwAudio();
  if (!mSwAudio->open (mConfig.hints, mConfig.layout)) {
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

  if (mConfig.passthrough)
    mPassthrough = isPassthrough (mConfig.hints);
  if (!mPassthrough && mConfig.hwdecode)
    mHwDecode = cOmxAudio::hwDecode (mConfig.hints.codec);
  if (mPassthrough)
    mHwDecode = false;

  if (mOmxAudio->initialize (mAvClock, mConfig,
                              mSwAudio->getChannelMap(), mSwAudio->getBitsPerSample())) {
    cLog::log (LOGINFO, "cOmxPlayerAudio::openOmxAudio " +
               string(mPassthrough ? " passThru" : "") +
               " chan:" + dec(mConfig.hints.channels) +
               " rate:" + dec(mConfig.hints.samplerate) +
               " bps:" + dec(mConfig.hints.bitspersample));

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
  auto old_bitrate = mConfig.hints.bitrate;
  auto new_bitrate = packet->hints.bitrate;

  // only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3
  if (mConfig.hints.codec != AV_CODEC_ID_DTS &&
      mConfig.hints.codec != AV_CODEC_ID_AC3 && mConfig.hints.codec != AV_CODEC_ID_EAC3)
    new_bitrate = old_bitrate = 0;

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = (channels != mConfig.hints.channels) ||
                      (packet->hints.bitspersample != mConfig.hints.bitspersample) ||
                      (old_bitrate != new_bitrate);

  if ((packet->hints.codec != mConfig.hints.codec) ||
      (packet->hints.samplerate != mConfig.hints.samplerate) ||
      (!mPassthrough && minor_change)) {
    cLog::log (LOGINFO, "Decode C : %d %d %d %d %d",
                        mConfig.hints.codec, mConfig.hints.channels, mConfig.hints.samplerate,
                        mConfig.hints.bitrate, mConfig.hints.bitspersample);
    cLog::log (LOGINFO, "Decode N : %d %d %d %d %d",
                        packet->hints.codec, channels, packet->hints.samplerate,
                        packet->hints.bitrate, packet->hints.bitspersample);
    mConfig.hints = packet->hints;

    delete mSwAudio;
    mSwAudio = nullptr;
    delete mOmxAudio;
    mOmxAudio = nullptr;
    if (!(openSwAudio() && openOmxAudio()))
      return false;
    }

  cLog::log (LOGINFO1, "Decode pts:%6.2f size:%d", packet->pts / 1000000.f, packet->size);

  if (packet->pts != DVD_NOPTS_VALUE)
    mICurrentPts = packet->pts;
  else if (packet->dts != DVD_NOPTS_VALUE)
    mICurrentPts = packet->dts;

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
    while ((int) mOmxAudio->getSpace() < packet->size) {
      cOmxClock::sleep (10);
      if (mFlushRequested)
        return true;
      }

    mOmxAudio->addPackets (packet->data, packet->size, packet->dts, packet->pts, 0);
    }

  return true;
  }
//}}}
