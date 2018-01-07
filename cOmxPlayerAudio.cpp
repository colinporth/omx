// cOmxPlayerAudio.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>

#include "cAudio.h"
#include "../shared/utils/cLog.h"
//}}}

//{{{
cOmxPlayerAudio::cOmxPlayerAudio() {

  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_omx_reader    = NULL;
  m_decoder       = NULL;
  m_flush         = false;
  m_flush_requested = false;
  m_cached_size   = 0;
  m_pAudioCodec   = NULL;
  m_player_error  = true;
  m_CurrentVolume = 0.0f;
  m_amplification = 0;
  m_mute          = false;

  pthread_cond_init (&m_packet_cond, NULL);
  pthread_cond_init (&m_audio_cond, NULL);
  pthread_mutex_init (&m_lock, NULL);
  pthread_mutex_init (&m_lock_decoder, NULL);
  }
//}}}
//{{{
cOmxPlayerAudio::~cOmxPlayerAudio() {

  Close();

  pthread_cond_destroy (&m_audio_cond);
  pthread_cond_destroy (&m_packet_cond);
  pthread_mutex_destroy (&m_lock);
  pthread_mutex_destroy (&m_lock_decoder);
  }
//}}}

//{{{
bool cOmxPlayerAudio::Open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader) {

  if (ThreadHandle())
    Close();

  if (!av_clock)
    return false;

  mAvFormat.av_register_all();

  m_config = config;
  m_av_clock = av_clock;
  m_omx_reader = omx_reader;
  m_passthrough = false;
  m_hw_decode = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort = false;
  m_flush = false;
  m_flush_requested = false;
  m_cached_size = 0;
  m_pAudioCodec = NULL;

  m_player_error = OpenAudioCodec();
  if (!m_player_error) {
    Close();
    return false;
    }

  m_player_error = OpenDecoder();
  if (!m_player_error) {
    Close();
    return false;
    }
  Create();

  m_open = true;
  return true;
  }
//}}}

double cOmxPlayerAudio::GetDelay() { return m_decoder ? m_decoder->GetDelay() : 0; }
double cOmxPlayerAudio::GetCacheTime() { return m_decoder ? m_decoder->GetCacheTime() : 0; }
double cOmxPlayerAudio::GetCacheTotal() { return m_decoder ? m_decoder->GetCacheTotal() : 0; }
//{{{
bool cOmxPlayerAudio::IsPassthrough (cOmxStreamInfo hints) {

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
bool cOmxPlayerAudio::AddPacket (OMXPacket *pkt) {

  if (m_bStop || m_bAbort)
    return false;

  if ((m_cached_size + pkt->size) < m_config.queue_size * 1024 * 1024) {
    Lock();
    m_cached_size += pkt->size;
    m_packets.push_back (pkt);
    UnLock();

    pthread_cond_broadcast (&m_packet_cond);
    return true;
    }

  return false;
  }
//}}}
//{{{
void cOmxPlayerAudio::Process() {

  cLog::setThreadName (" aud");

  OMXPacket* omx_pkt = NULL;
  while (true) {
    Lock();
    if (!(m_bStop || m_bAbort) && m_packets.empty())
      pthread_cond_wait (&m_packet_cond, &m_lock);

    if (m_bStop || m_bAbort) {
      UnLock();
      break;
      }

    if (m_flush && omx_pkt) {
      cOmxReader::FreePacket (omx_pkt);
      omx_pkt = NULL;
      m_flush = false;
      }
    else if (!omx_pkt && !m_packets.empty()) {
      omx_pkt = m_packets.front();
      m_cached_size -= omx_pkt->size;
      m_packets.pop_front();
      }
    UnLock();

    LockDecoder();
    if (m_flush && omx_pkt) {
      cOmxReader::FreePacket (omx_pkt);
      omx_pkt = NULL;
      m_flush = false;
      }
    else if (omx_pkt && Decode (omx_pkt)) {
      cOmxReader::FreePacket (omx_pkt);
      omx_pkt = NULL;
      }
    UnLockDecoder();
    }

  if (omx_pkt)
    cOmxReader::FreePacket (omx_pkt);
  }
//}}}
//{{{
void cOmxPlayerAudio::Flush() {

  m_flush_requested = true;

  Lock();
  LockDecoder();

  if (m_pAudioCodec)
    m_pAudioCodec->Reset();

  m_flush_requested = false;
  m_flush = true;
  while (!m_packets.empty()) {
    auto pkt = m_packets.front();
    m_packets.pop_front();
    cOmxReader::FreePacket (pkt);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_cached_size = 0;
  if (m_decoder)
    m_decoder->Flush();

  UnLockDecoder();
  UnLock();
  }
//}}}

//{{{
void cOmxPlayerAudio::SubmitEOS() {
  if (m_decoder)
    m_decoder->SubmitEOS();
  }
//}}}
//{{{
bool cOmxPlayerAudio::IsEOS() {
  return m_packets.empty() && (!m_decoder || m_decoder->IsEOS());
  }
//}}}

/// private
//{{{
bool cOmxPlayerAudio::Close() {

  m_bAbort  = true;
  Flush();

  if (ThreadHandle()) {
    Lock();
    pthread_cond_broadcast (&m_packet_cond);
    UnLock();
    StopThread();
    }

  CloseDecoder();
  CloseAudioCodec();

  m_open = false;
  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_pStream = NULL;

  return true;
  }
//}}}

//{{{
bool cOmxPlayerAudio::OpenDecoder() {

  bool bAudioRenderOpen = false;

  m_decoder = new cOmxAudio();
  if (m_config.passthrough)
    m_passthrough = IsPassthrough (m_config.hints);
  if (!m_passthrough && m_config.hwdecode)
    m_hw_decode = cOmxAudio::HWDecode (m_config.hints.codec);
  if (m_passthrough)
    m_hw_decode = false;

  bAudioRenderOpen = m_decoder->Initialize (m_av_clock, m_config, m_pAudioCodec->GetChannelMap(),
                                            m_pAudioCodec->GetBitsPerSample());
  m_codec_name = m_omx_reader->GetCodecName (OMXSTREAM_AUDIO);

  if (!bAudioRenderOpen) {
    delete m_decoder;
    m_decoder = NULL;
    return false;
    }
  else if (m_passthrough)
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s passthrough ch:%d rate:%d bps:%d",
               m_codec_name.c_str(), m_config.hints.channels,
               m_config.hints.samplerate, m_config.hints.bitspersample);
  else
    cLog::log (LOGINFO, "cOmxPlayerAudio::OpenDecoder %s ch:%d rate:%d bps:%d",
               m_codec_name.c_str(), m_config.hints.channels,
               m_config.hints.samplerate, m_config.hints.bitspersample);

  // setup current volume settings
  m_decoder->SetVolume (m_CurrentVolume);
  m_decoder->SetMute (m_mute);
  m_decoder->SetDynamicRangeCompression (m_amplification);

  return true;
  }
//}}}
//{{{
void cOmxPlayerAudio::CloseDecoder() {
  if (m_decoder)
    delete m_decoder;
  m_decoder = NULL;
  }
//}}}

//{{{
bool cOmxPlayerAudio::OpenAudioCodec() {

  m_pAudioCodec = new cSwAudio();
  if (!m_pAudioCodec->Open (m_config.hints, m_config.layout)) {
    delete m_pAudioCodec;
    m_pAudioCodec = NULL;
    return false;
   }

  return true;
  }
//}}}
//{{{
void cOmxPlayerAudio::CloseAudioCodec() {
  if (m_pAudioCodec)
    delete m_pAudioCodec;
  m_pAudioCodec = NULL;
  }
//}}}

//{{{
bool cOmxPlayerAudio::Decode (OMXPacket *pkt) {

  if (!m_decoder || !m_pAudioCodec)
    return true;

  if (!m_omx_reader->IsActive (OMXSTREAM_AUDIO, pkt->stream_index))
    return true;

  int channels = pkt->hints.channels;
  unsigned int old_bitrate = m_config.hints.bitrate;
  unsigned int new_bitrate = pkt->hints.bitrate;

  /* only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3 */
  if (m_config.hints.codec != AV_CODEC_ID_DTS &&
      m_config.hints.codec != AV_CODEC_ID_AC3 && m_config.hints.codec != AV_CODEC_ID_EAC3)
    new_bitrate = old_bitrate = 0;

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = channels != m_config.hints.channels ||
                      pkt->hints.bitspersample != m_config.hints.bitspersample ||
                      old_bitrate != new_bitrate;

  if (pkt->hints.codec != m_config.hints.codec ||
      pkt->hints.samplerate!= m_config.hints.samplerate || (!m_passthrough && minor_change)) {
    printf ("C : %d %d %d %d %d\n",
            m_config.hints.codec, m_config.hints.channels, m_config.hints.samplerate, m_config.hints.bitrate, m_config.hints.bitspersample);
    printf ("N : %d %d %d %d %d\n",
            pkt->hints.codec, channels, pkt->hints.samplerate, pkt->hints.bitrate, pkt->hints.bitspersample);
    CloseDecoder();
    CloseAudioCodec();

    m_config.hints = pkt->hints;
    m_player_error = OpenAudioCodec();
    if (!m_player_error)
      return false;

    m_player_error = OpenDecoder();
    if (!m_player_error)
      return false;
    }

  cLog::log (LOGINFO, "Decode pts:%.0f size:%d", pkt->pts, pkt->size);

  if (pkt->pts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->pts;
  else if (pkt->dts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->dts;

  const uint8_t* data_dec = pkt->data;
  int data_len = pkt->size;

  if (!m_passthrough && !m_hw_decode) {
    double dts = pkt->dts, pts=pkt->pts;
    while (data_len > 0) {
      int len = m_pAudioCodec->Decode((BYTE*)data_dec, data_len, dts, pts);
      if ((len < 0) || (len >  data_len)) {
        m_pAudioCodec->Reset();
        break;
        }

      data_dec += len;
      data_len -= len;
      uint8_t* decoded;
      int decoded_size = m_pAudioCodec->GetData (&decoded, dts, pts);

      if (decoded_size <=0)
        continue;

      while ((int)m_decoder->GetSpace() < decoded_size) {
        cOmxClock::sleep (10);
        if (m_flush_requested)
          return true;
        }

      int ret = m_decoder->AddPackets (decoded, decoded_size, dts, pts, m_pAudioCodec->GetFrameSize());
      if (ret != decoded_size)
        printf ("error ret %d decoded_size %d\n", ret, decoded_size);
      }
    }
  else {
    while ((int) m_decoder->GetSpace() < pkt->size) {
      cOmxClock::sleep (10);
      if (m_flush_requested)
        return true;
      }

    m_decoder->AddPackets (pkt->data, pkt->size, pkt->dts, pkt->pts, 0);
    }

  return true;
  }
//}}}
