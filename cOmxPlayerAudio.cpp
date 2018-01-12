// cOmxPlayerAudio.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "cAudio.h"
#include "../shared/utils/cLog.h"

using namespace std;
//}}}

//{{{
cOmxPlayerAudio::cOmxPlayerAudio() {

  pthread_mutex_init (&mLock, NULL);
  pthread_mutex_init (&mLockDecoder, NULL);

  pthread_cond_init (&m_packet_cond, NULL);
  pthread_cond_init (&m_audio_cond, NULL);

  mFlush_requested = false;
  }
//}}}
//{{{
cOmxPlayerAudio::~cOmxPlayerAudio() {

  close();

  pthread_cond_destroy (&m_audio_cond);
  pthread_cond_destroy (&m_packet_cond);
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
bool cOmxPlayerAudio::open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader) {

  mAvFormat.av_register_all();

  mConfig = config;
  mAvClock = av_clock;
  m_omx_reader = omx_reader;

  mAbort = false;
  mFlush = false;
  mFlush_requested = false;
  mPassthrough = false;
  mHwDecode = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;
  mSwAudio = NULL;

  mPlayerError = openSwAudio();
  mPlayerError |= openOmxAudio();
  if (!mPlayerError) {
    close();
    return false;
    }

  return true;
  }
//}}}
//{{{
void cOmxPlayerAudio::run() {

  cLog::setThreadName ("aud ");

  OMXPacket* packet = NULL;
  while (true) {
    lock();
    if (!mAbort && mPackets.empty())
      pthread_cond_wait (&m_packet_cond, &mLock);

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

    pthread_cond_broadcast (&m_packet_cond);
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

  mFlush_requested = true;

  lock();
  lockDecoder();

  if (mSwAudio)
    mSwAudio->reset();

  mFlush_requested = false;
  mFlush = true;
  while (!mPackets.empty()) {
    auto packet = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (packet);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
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
  pthread_cond_broadcast (&m_packet_cond);
  unLock();

  closeSwAudio();
  closeOmxAudio();

  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  mStream = NULL;

  return true;
  }
//}}}

/// private
//{{{
bool cOmxPlayerAudio::openSwAudio() {

  mSwAudio = new cSwAudio();
  if (!mSwAudio->open (mConfig.hints, mConfig.layout)) {
    delete mSwAudio;
    mSwAudio = NULL;
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

  bool render = mOmxAudio->initialize (
    mAvClock, mConfig, mSwAudio->getChannelMap(), mSwAudio->getBitsPerSample());
  m_codec_name = m_omx_reader->getCodecName (OMXSTREAM_AUDIO);

  if (!render) {
    delete mOmxAudio;
    mOmxAudio = NULL;
    return false;
    }
  else if (mPassthrough)
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s passthrough ch:%d rate:%d bps:%d",
               m_codec_name.c_str(),
               mConfig.hints.channels,
               mConfig.hints.samplerate, mConfig.hints.bitspersample);
  else
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s ch:%d rate:%d bps:%d",
               m_codec_name.c_str(),
               mConfig.hints.channels,
               mConfig.hints.samplerate, mConfig.hints.bitspersample);

  // setup current volume settings
  mOmxAudio->setVolume (m_CurrentVolume);
  mOmxAudio->setMute (mMute);
  mOmxAudio->setDynamicRangeCompression (mDrc);

  return true;
  }
//}}}

//{{{
bool cOmxPlayerAudio::decode (OMXPacket* packet) {

  if (!m_omx_reader->isActive (OMXSTREAM_AUDIO, packet->stream_index))
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
    closeSwAudio();
    closeOmxAudio();

    mConfig.hints = packet->hints;
    mPlayerError = openSwAudio();
    mPlayerError |= openOmxAudio();
    if (!mPlayerError)
      return false;
    }

  cLog::log (LOGINFO1, "Decode pts:%6.2f size:%d", packet->pts / 1000000.f, packet->size);

  if (packet->pts != DVD_NOPTS_VALUE)
    m_iCurrentPts = packet->pts;
  else if (packet->dts != DVD_NOPTS_VALUE)
    m_iCurrentPts = packet->dts;

  const uint8_t* data_dec = packet->data;
  int data_len = packet->size;

  if (!mPassthrough && !mHwDecode) {
    auto dts = packet->dts;
    auto pts = packet->pts;
    while (data_len > 0) {
      int len = mSwAudio->decode((BYTE*)data_dec, data_len, dts, pts);
      if ((len < 0) || (len >  data_len)) {
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
        if (mFlush_requested)
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
      if (mFlush_requested)
        return true;
      }

    mOmxAudio->addPackets (packet->data, packet->size, packet->dts, packet->pts, 0);
    }

  return true;
  }
//}}}

//{{{
void cOmxPlayerAudio::closeSwAudio() {

  if (mSwAudio)
    delete mSwAudio;
  mSwAudio = NULL;
  }
//}}}
//{{{
void cOmxPlayerAudio::closeOmxAudio() {

  delete mOmxAudio;
  mOmxAudio = NULL;
  }
//}}}
