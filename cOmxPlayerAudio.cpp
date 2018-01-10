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

double cOmxPlayerAudio::getDelay() { return m_decoder ? m_decoder->getDelay() : 0; }
double cOmxPlayerAudio::getCacheTime() { return m_decoder ? m_decoder->getCacheTime() : 0; }
double cOmxPlayerAudio::getCacheTotal() { return m_decoder ? m_decoder->getCacheTotal() : 0; }
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
  return mPackets.empty() && (!m_decoder || m_decoder->isEOS());
  }
//}}}

//{{{
bool cOmxPlayerAudio::open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader) {

  mAvFormat.av_register_all();

  m_config = config;
  m_av_clock = av_clock;
  m_omx_reader = omx_reader;
  m_passthrough = false;
  m_hw_decode = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  mAbort = false;
  mFlush = false;
  mFlush_requested = false;
  mCachedSize = 0;
  mAudioCodec = NULL;

  m_player_error = openSwDecoder();
  if (!m_player_error) {
    close();
    return false;
    }

  m_player_error = openHwDecoder();
  if (!m_player_error) {
    close();
    return false;
    }

  return true;
  }
//}}}
//{{{
void cOmxPlayerAudio::run() {

  cLog::setThreadName ("aud ");

  OMXPacket* omx_pkt = NULL;
  while (true) {
    lock();
    if (!mAbort && mPackets.empty())
      pthread_cond_wait (&m_packet_cond, &mLock);

    if (mAbort) {
      unLock();
      break;
      }

    if (mFlush && omx_pkt) {
      cOmxReader::freePacket (omx_pkt);
      mFlush = false;
      }
    else if (!omx_pkt && !mPackets.empty()) {
      omx_pkt = mPackets.front();
      mCachedSize -= omx_pkt->size;
      mPackets.pop_front();
      }
    unLock();

    lockDecoder();
    if (mFlush && omx_pkt) {
      cOmxReader::freePacket (omx_pkt);
      mFlush = false;
      }
    else if (omx_pkt && decode (omx_pkt))
      cOmxReader::freePacket (omx_pkt);
    unLockDecoder();
    }

  cOmxReader::freePacket (omx_pkt);

  cLog::log (LOGNOTICE, "exit");
  }
//}}}
//{{{
bool cOmxPlayerAudio::addPacket (OMXPacket *pkt) {

  if (!mAbort &&
      ((mCachedSize + pkt->size) < m_config.queue_size * 1024 * 1024)) {
    lock();
    mCachedSize += pkt->size;
    mPackets.push_back (pkt);
    unLock();

    pthread_cond_broadcast (&m_packet_cond);
    return true;
    }

  return false;
  }
//}}}
//{{{
void cOmxPlayerAudio::submitEOS() {
  if (m_decoder)
    m_decoder->submitEOS();
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
    auto pkt = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (pkt);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;
  if (m_decoder)
    m_decoder->flush();

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
void cOmxPlayerAudio::closeSwDecoder() {

  if (mAudioCodec)
    delete mAudioCodec;

  mAudioCodec = NULL;
  }
//}}}

//{{{
bool cOmxPlayerAudio::openHwDecoder() {

  m_decoder = new cOmxAudio();
  if (m_config.passthrough)
    m_passthrough = isPassthrough (m_config.hints);
  if (!m_passthrough && m_config.hwdecode)
    m_hw_decode = cOmxAudio::hwDecode (m_config.hints.codec);
  if (m_passthrough)
    m_hw_decode = false;

  bool audioRenderOpen = m_decoder->initialize (
    m_av_clock, m_config, mAudioCodec->getChannelMap(), mAudioCodec->getBitsPerSample());
  m_codec_name = m_omx_reader->getCodecName (OMXSTREAM_AUDIO);

  if (!audioRenderOpen) {
    delete m_decoder;
    m_decoder = NULL;
    return false;
    }
  else if (m_passthrough)
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
  m_decoder->setVolume (m_CurrentVolume);
  m_decoder->setMute (m_mute);
  m_decoder->setDynamicRangeCompression (m_amplification);

  return true;
  }
//}}}
//{{{
void cOmxPlayerAudio::closeHwDecoder() {
  if (m_decoder)
    delete m_decoder;
  m_decoder = NULL;
  }
//}}}

//{{{
bool cOmxPlayerAudio::decode (OMXPacket *pkt) {

  if (!m_decoder || !mAudioCodec)
    return true;

  if (!m_omx_reader->isActive (OMXSTREAM_AUDIO, pkt->stream_index))
    return true;

  int channels = pkt->hints.channels;
  unsigned int old_bitrate = m_config.hints.bitrate;
  unsigned int new_bitrate = pkt->hints.bitrate;

  // only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3
  if (m_config.hints.codec != AV_CODEC_ID_DTS &&
      m_config.hints.codec != AV_CODEC_ID_AC3 && m_config.hints.codec != AV_CODEC_ID_EAC3)
    new_bitrate = old_bitrate = 0;

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = channels != m_config.hints.channels ||
                      pkt->hints.bitspersample != m_config.hints.bitspersample ||
                      old_bitrate != new_bitrate;

  if (pkt->hints.codec != m_config.hints.codec ||
      pkt->hints.samplerate!= m_config.hints.samplerate || (!m_passthrough && minor_change)) {
    cLog::log (LOGINFO, "Decode C : %d %d %d %d %d",
                        m_config.hints.codec, m_config.hints.channels, m_config.hints.samplerate,
                        m_config.hints.bitrate, m_config.hints.bitspersample);
    cLog::log (LOGINFO, "Decode N : %d %d %d %d %d",
                        pkt->hints.codec, channels, pkt->hints.samplerate,
                        pkt->hints.bitrate, pkt->hints.bitspersample);
    closeSwDecoder();
    closeHwDecoder();

    m_config.hints = pkt->hints;
    m_player_error = openSwDecoder();
    if (!m_player_error)
      return false;

    m_player_error = openHwDecoder();
    if (!m_player_error)
      return false;
    }

  cLog::log (LOGINFO1, "Decode pts:%6.2f size:%d", pkt->pts / 1000000.f, pkt->size);

  if (pkt->pts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->pts;
  else if (pkt->dts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->dts;

  const uint8_t* data_dec = pkt->data;
  int data_len = pkt->size;

  if (!m_passthrough && !m_hw_decode) {
    double dts = pkt->dts;
    double pts = pkt->pts;
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

      while ((int)m_decoder->getSpace() < decoded_size) {
        cOmxClock::sleep (10);
        if (mFlush_requested)
          return true;
        }

      int ret = m_decoder->addPackets (decoded, decoded_size, dts, pts, mAudioCodec->getFrameSize());
      if (ret != decoded_size)
        cLog::log (LOGERROR, "error ret %d decoded_size %d", ret, decoded_size);
      }
    }
  else {
    while ((int) m_decoder->getSpace() < pkt->size) {
      cOmxClock::sleep (10);
      if (mFlush_requested)
        return true;
      }

    m_decoder->addPackets (pkt->data, pkt->size, pkt->dts, pkt->pts, 0);
    }

  return true;
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
