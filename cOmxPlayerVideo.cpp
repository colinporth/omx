// cOmxPlayerVideo.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "cVideo.h"
#include "../shared/utils/cLog.h"

using namespace std;
//}}}

//{{{
cOmxPlayerVideo::cOmxPlayerVideo() {

  pthread_mutex_init (&mLock, NULL);
  pthread_mutex_init (&mLockDecoder, NULL);

  pthread_cond_init (&m_packet_cond, NULL);
  pthread_cond_init (&m_picture_cond, NULL);

  mFlush_requested = false;
  }
//}}}
//{{{
cOmxPlayerVideo::~cOmxPlayerVideo() {

  Close();

  pthread_cond_destroy (&m_packet_cond);
  pthread_cond_destroy (&m_picture_cond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerVideo::Open (cOmxClock* av_clock, const cOmxVideoConfig& config) {

  mAvFormat.av_register_all();

  m_config = config;
  m_av_clock = av_clock;

  m_fps = 25.f;
  m_frametime = 0;

  m_iCurrentPts = DVD_NOPTS_VALUE;

  mAbort = false;
  mFlush = false;
  m_cached_size = 0;
  m_iVideoDelay = 0;

  if (!OpenDecoder()) {
    Close();
    return false;
    }

  return true;
  }
//}}}
//{{{
void cOmxPlayerVideo::Reset() {

  Flush();

  m_stream_id = -1;
  m_pStream = NULL;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_frametime = 0;

  mAbort = false;
  mFlush = false;
  mFlush_requested = false;

  m_cached_size = 0;
  m_iVideoDelay = 0;
  }
//}}}

int cOmxPlayerVideo::GetDecoderBufferSize() { return m_decoder ? m_decoder->GetInputBufferSize() : 0; }
int cOmxPlayerVideo::GetDecoderFreeSpace() { return m_decoder ? m_decoder->GetFreeSpace() : 0; }

void cOmxPlayerVideo::SetAlpha (int alpha) { m_decoder->SetAlpha (alpha); }
void cOmxPlayerVideo::SetVideoRect (int aspectMode) { m_decoder->SetVideoRect (aspectMode); }
void cOmxPlayerVideo::SetVideoRect (const CRect& SrcRect, const CRect& DestRect) { m_decoder->SetVideoRect (SrcRect, DestRect); }

//{{{
bool cOmxPlayerVideo::AddPacket (OMXPacket* pkt) {

  if (!mAbort &&
      ((m_cached_size + pkt->size) < (m_config.queue_size * 1024 * 1024))) {
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
void cOmxPlayerVideo::Process() {

  cLog::setThreadName ("vid ");

  OMXPacket* omx_pkt = NULL;
  while (true) {
    Lock();
    if (!mAbort && m_packets.empty())
      pthread_cond_wait (&m_packet_cond, &mLock);

    if (mAbort) {
      UnLock();
      break;
      }

    if (mFlush && omx_pkt) {
      cOmxReader::FreePacket (omx_pkt);
      mFlush = false;
      }
    else if (!omx_pkt && !m_packets.empty()) {
      omx_pkt = m_packets.front();
      m_cached_size -= omx_pkt->size;
      m_packets.pop_front();
      }
    UnLock();

    LockDecoder();
    if (omx_pkt) {
      if (mFlush) {
        cOmxReader::FreePacket (omx_pkt);
        mFlush = false;
        }
      else if (Decode (omx_pkt))
        cOmxReader::FreePacket (omx_pkt);
      }
    UnLockDecoder();
    }

  if (omx_pkt)
    cOmxReader::FreePacket (omx_pkt);

  cLog::log (LOGNOTICE, "Process - exit");
  }
//}}}
//{{{
void cOmxPlayerVideo::Flush() {

  mFlush_requested = true;

  Lock();
  LockDecoder();

  mFlush_requested = false;
  mFlush = true;
  while (!m_packets.empty()) {
    OMXPacket *pkt = m_packets.front();
    m_packets.pop_front();
    cOmxReader::FreePacket (pkt);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_cached_size = 0;

  if (m_decoder)
    m_decoder->Reset();

  UnLockDecoder();
  UnLock();
  }
//}}}

//{{{
void cOmxPlayerVideo::SubmitEOS() {

  if (m_decoder)
    m_decoder->SubmitEOS();
  }
//}}}
//{{{
bool cOmxPlayerVideo::IsEOS() {

  if (!m_decoder)
    return false;
  return m_packets.empty() && (!m_decoder || m_decoder->IsEOS());
  }
//}}}

// private
//{{{
bool cOmxPlayerVideo::Close() {

  mAbort  = true;

  Flush();

  Lock();
  pthread_cond_broadcast (&m_packet_cond);
  UnLock();

  CloseDecoder();

  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_pStream = NULL;

  return true;
  }
//}}}

//{{{
bool cOmxPlayerVideo::OpenDecoder() {

  if (m_config.hints.fpsrate && m_config.hints.fpsscale)
    m_fps = 1000000.f /
            cOmxReader::NormalizeFrameDuration (1000000.0 * m_config.hints.fpsscale / m_config.hints.fpsrate);
  else
    m_fps = 25.f;

  if (m_fps > 100.f || m_fps < 5.f) {
    cLog::log (LOGERROR, "cOmxPlayerVideo::OpenDecoder = invalid framerate %d, using forced 25fps, trust timestamps", (int)m_fps);
    m_fps = 25.f;
    }
  m_frametime = 1000000.0 / m_fps;

  m_decoder = new cOmxVideo();
  if (m_decoder->Open (m_av_clock, m_config)) {
    cLog::log (LOGINFO, "cOmxPlayerVideo::OpenDecoder %s profile:%d %dx%d %ffps",
               m_decoder->GetDecoderName().c_str(), m_config.hints.profile,
               m_config.hints.width, m_config.hints.height,
               m_fps);
    return true;
    }
  else {
    CloseDecoder();
    return false;
    }
  }
//}}}
//{{{
void cOmxPlayerVideo::CloseDecoder() {

  if (m_decoder)
    delete m_decoder;
  m_decoder = NULL;
  }
//}}}

//{{{
bool cOmxPlayerVideo::Decode (OMXPacket* pkt) {

  double dts = pkt->dts;
  if (dts != DVD_NOPTS_VALUE)
    dts += m_iVideoDelay;

  double pts = pkt->pts;
  if (pts != DVD_NOPTS_VALUE) {
    pts += m_iVideoDelay;
    m_iCurrentPts = pts;
    }
  else
    cLog::log (LOGINFO, "decode - DVD_NOPTS_VALUE");

  cLog::log (LOGINFO1, "Decode pts:%6.2f curPts:%6.2f size:%d",
                       pkt->pts / 1000000.f, m_iCurrentPts / 1000000.f, pkt->size);

  while ((int)m_decoder->GetFreeSpace() < pkt->size) {
    cOmxClock::sleep (10);
    if (mFlush_requested)
      return true;
    }

  return m_decoder->Decode (pkt->data, pkt->size, dts, pts);
  }
//}}}
