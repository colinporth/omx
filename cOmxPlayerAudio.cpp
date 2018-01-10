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

double cOmxPlayerAudio::getDelay() { return mDecoder->getDelay(); }
double cOmxPlayerAudio::getCacheTime() { return mDecoder->getCacheTime(); }
double cOmxPlayerAudio::getCacheTotal() { return  mDecoder->getCacheTotal(); }
//{{{
bool cOmxPlayerAudio::isPassthrough (cOmxStreamInfo hints) {

  if (m_config.device == "omx:local")
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
bool cOmxPlayerAudio::isEOS() {
  return mPackets.empty() && mDecoder->isEOS();
  }
//}}}

//{{{
bool cOmxPlayerAudio::open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader) {

  mAvFormat.av_register_all();

  m_config = config;
  m_av_clock = av_clock;
  m_omx_reader = omx_reader;

  mAbort = false;
  mFlush = false;
  mFlush_requested = false;
  mPassthrough = false;
  mHwDecode = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;
  mAudioCodec = NULL;

  mPlayerError = openSwDecoder();
  mPlayerError |= openHwDecoder();
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
      ((mCachedSize + packet->size) < m_config.queue_size * 1024 * 1024)) {
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

  mDecoder->submitEOS();
  }
//}}}
//{{{
void cOmxPlayerAudio::flush() {

  mFlush_requested = true;

  lock();
  lockDecoder();

  if (mAudioCodec)
    mAudioCodec->reset();

  mFlush_requested = false;
  mFlush = true;
  while (!mPackets.empty()) {
    auto packet = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (packet);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;
  mDecoder->flush();

  unLockDecoder();
  unLock();
  }
//}}}

/// private
//{{{
bool cOmxPlayerAudio::openSwDecoder() {

  mAudioCodec = new cSwAudio();
  if (!mAudioCodec->open (m_config.hints, m_config.layout)) {
    delete mAudioCodec;
    mAudioCodec = NULL;
    return false;
    }

  return true;
  }
//}}}
//{{{
bool cOmxPlayerAudio::openHwDecoder() {

  mDecoder = new cOmxAudio();
  if (m_config.passthrough)
    mPassthrough = isPassthrough (m_config.hints);
  if (!mPassthrough && m_config.hwdecode)
    mHwDecode = cOmxAudio::hwDecode (m_config.hints.codec);
  if (mPassthrough)
    mHwDecode = false;

  bool render = mDecoder->initialize (
    m_av_clock, m_config, mAudioCodec->getChannelMap(), mAudioCodec->getBitsPerSample());
  m_codec_name = m_omx_reader->getCodecName (OMXSTREAM_AUDIO);

  if (!render) {
    delete mDecoder;
    mDecoder = NULL;
    return false;
    }
  else if (mPassthrough)
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s passthrough ch:%d rate:%d bps:%d",
               m_codec_name.c_str(),
               m_config.hints.channels,
               m_config.hints.samplerate, m_config.hints.bitspersample);
  else
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s ch:%d rate:%d bps:%d",
               m_codec_name.c_str(),
               m_config.hints.channels,
               m_config.hints.samplerate, m_config.hints.bitspersample);

  // setup current volume settings
  mDecoder->setVolume (m_CurrentVolume);
  mDecoder->setMute (mMute);
  mDecoder->setDynamicRangeCompression (m_amplification);

  return true;
  }
//}}}

//{{{
bool cOmxPlayerAudio::decode (OMXPacket* packet) {

  if (!m_omx_reader->isActive (OMXSTREAM_AUDIO, packet->stream_index))
    return true;

  int channels = packet->hints.channels;
  unsigned int old_bitrate = m_config.hints.bitrate;
  unsigned int new_bitrate = packet->hints.bitrate;

  // only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3
  if (m_config.hints.codec != AV_CODEC_ID_DTS &&
      m_config.hints.codec != AV_CODEC_ID_AC3 && m_config.hints.codec != AV_CODEC_ID_EAC3)
    new_bitrate = old_bitrate = 0;

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = channels != m_config.hints.channels ||
                      packet->hints.bitspersample != m_config.hints.bitspersample ||
                      old_bitrate != new_bitrate;

  if (packet->hints.codec != m_config.hints.codec ||
      packet->hints.samplerate!= m_config.hints.samplerate || (!mPassthrough && minor_change)) {
    cLog::log (LOGINFO, "Decode C : %d %d %d %d %d",
                        m_config.hints.codec, m_config.hints.channels, m_config.hints.samplerate,
                        m_config.hints.bitrate, m_config.hints.bitspersample);
    cLog::log (LOGINFO, "Decode N : %d %d %d %d %d",
                        packet->hints.codec, channels, packet->hints.samplerate,
                        packet->hints.bitrate, packet->hints.bitspersample);
    closeSwDecoder();
    closeHwDecoder();

    m_config.hints = packet->hints;
    mPlayerError = openSwDecoder();
    if (!mPlayerError)
      return false;

    mPlayerError = openHwDecoder();
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
    double dts = packet->dts;
    double pts = packet->pts;
    while (data_len > 0) {
      int len = mAudioCodec->decode((BYTE*)data_dec, data_len, dts, pts);
      if ((len < 0) || (len >  data_len)) {
        mAudioCodec->reset();
        break;
        }

      data_dec += len;
      data_len -= len;
      uint8_t* decoded;
      int decoded_size = mAudioCodec->getData (&decoded, dts, pts);

      if (decoded_size <=0)
        continue;

      while ((int)mDecoder->getSpace() < decoded_size) {
        cOmxClock::sleep (10);
        if (mFlush_requested)
          return true;
        }

      int ret = mDecoder->addPackets (decoded, decoded_size, dts, pts, mAudioCodec->getFrameSize());
      if (ret != decoded_size)
        cLog::log (LOGERROR, "error ret %d decoded_size %d", ret, decoded_size);
      }
    }
  else {
    while ((int) mDecoder->getSpace() < packet->size) {
      cOmxClock::sleep (10);
      if (mFlush_requested)
        return true;
      }

    mDecoder->addPackets (packet->data, packet->size, packet->dts, packet->pts, 0);
    }

  return true;
  }
//}}}

//{{{
void cOmxPlayerAudio::closeSwDecoder() {

  if (mAudioCodec)
    delete mAudioCodec;
  mAudioCodec = NULL;
  }
//}}}
//{{{
void cOmxPlayerAudio::closeHwDecoder() {

  delete mDecoder;
  mDecoder = NULL;
  }
//}}}
//{{{
bool cOmxPlayerAudio::close() {

  mAbort = true;
  flush();

  lock();
  pthread_cond_broadcast (&m_packet_cond);
  unLock();

  closeHwDecoder();
  closeSwDecoder();

  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  mStream = NULL;

  return true;
  }
//}}}
