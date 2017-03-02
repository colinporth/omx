//{{{  includes
#include "cVideo.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "cLog.h"
//}}}

//{{{
cOmxPlayerVideo::cOmxPlayerVideo() {

  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_decoder       = NULL;
  m_fps           = 25.0f;
  m_flush         = false;
  m_flush_requested = false;
  m_cached_size   = 0;
  m_iVideoDelay   = 0;
  m_iCurrentPts   = 0;

  pthread_cond_init (&m_packet_cond, NULL);
  pthread_cond_init (&m_picture_cond, NULL);
  pthread_mutex_init (&m_lock, NULL);
  pthread_mutex_init (&m_lock_decoder, NULL);
  }
//}}}
//{{{
cOmxPlayerVideo::~cOmxPlayerVideo() {

  Close();

  pthread_cond_destroy (&m_packet_cond);
  pthread_cond_destroy (&m_picture_cond);
  pthread_mutex_destroy (&m_lock);
  pthread_mutex_destroy (&m_lock_decoder);
  }
//}}}

//{{{
bool cOmxPlayerVideo::Open (cOmxClock* av_clock, const cOmxVideoConfig& config) {

  if (!av_clock)
    return false;

  if (ThreadHandle())
    Close();

  mAvFormat.av_register_all();

  m_config      = config;
  m_av_clock    = av_clock;
  m_fps         = 25.0f;
  m_frametime   = 0;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort      = false;
  m_flush       = false;
  m_cached_size = 0;
  m_iVideoDelay = 0;

  if (!OpenDecoder()) {
    Close();
    return false;
    }

  Create();

  m_open = true;
  return true;
  }
//}}}
//{{{
bool cOmxPlayerVideo::Close() {

  m_bAbort  = true;

  Flush();

  if (ThreadHandle()) {
    Lock();
    pthread_cond_broadcast(&m_packet_cond);
    UnLock();
    StopThread();
    }

  CloseDecoder();

  m_open = false;
  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_pStream = NULL;

  return true;
  }
//}}}

//{{{
bool cOmxPlayerVideo::OpenDecoder() {

  if (m_config.hints.fpsrate && m_config.hints.fpsscale)
    m_fps = DVD_TIME_BASE / cOmxReader::NormalizeFrameDuration (
      (double)DVD_TIME_BASE * m_config.hints.fpsscale / m_config.hints.fpsrate);
  else
    m_fps = 25;

  if (m_fps > 100 || m_fps < 5 ) {
    printf ("Invalid framerate %d, using forced 25fps and just trust timestamps\n", (int)m_fps);
    m_fps = 25;
    }
  m_frametime = (double)DVD_TIME_BASE / m_fps;

  m_decoder = new cOmxVideo();
  if (m_decoder->Open (m_av_clock, m_config)) {
    cLog::Log (LOGINFO, "cOmxPlayerVideo::OpenDecoder %s w:%d h:%d profile:%d fps %f\n",
               m_decoder->GetDecoderName().c_str(),
               m_config.hints.width, m_config.hints.height, m_config.hints.profile, m_fps);
    return true;
    }
  else {
    CloseDecoder();
    return false;
    }
  }
//}}}
//{{{
bool cOmxPlayerVideo::CloseDecoder() {
  if (m_decoder)
    delete m_decoder;
  m_decoder   = NULL;
  return true;
  }
//}}}

//{{{
bool cOmxPlayerVideo::Reset() {

  // Quick reset of internal state back to a default that is ready to play from
  // the start or a new position.  This replaces a combination of Close and then
  // Open calls but does away with the DLL unloading/loading, decoder reset, and
  // thread reset.
  Flush();
  m_stream_id         = -1;
  m_pStream           = NULL;
  m_iCurrentPts       = DVD_NOPTS_VALUE;
  m_frametime         = 0;
  m_bAbort            = false;
  m_flush             = false;
  m_flush_requested   = false;
  m_cached_size       = 0;
  m_iVideoDelay       = 0;

  // Keep consistency with old Close/Open logic by continuing to return a bool
  // with the success/failure of this call.  Although little can go wrong
  // setting some variables, in the future this could indicate success/failure
  // of the reset.  For now just return success (true).
  return true;
  }
//}}}
//{{{
void cOmxPlayerVideo::Flush() {

  m_flush_requested = true;
  Lock();
  LockDecoder();

  m_flush_requested = false;
  m_flush = true;
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
int  cOmxPlayerVideo::GetDecoderBufferSize() {
  if (m_decoder)
    return m_decoder->GetInputBufferSize();
  else
    return 0;
  }
//}}}
//{{{
int  cOmxPlayerVideo::GetDecoderFreeSpace() {
  if (m_decoder)
    return m_decoder->GetFreeSpace();
  else
    return 0;
  }
//}}}

//{{{
void cOmxPlayerVideo::SetAlpha (int alpha) {
  m_decoder->SetAlpha (alpha);
  }
//}}}
//{{{
void cOmxPlayerVideo::SetVideoRect (const CRect& SrcRect, const CRect& DestRect) {
  m_decoder->SetVideoRect (SrcRect, DestRect);
  }
//}}}
//{{{
void cOmxPlayerVideo::SetVideoRect (int aspectMode) {
  m_decoder->SetVideoRect (aspectMode);
  }
//}}}

//{{{
bool cOmxPlayerVideo::AddPacket (OMXPacket *pkt) {

  bool ret = false;
  if (!pkt)
    return ret;

  if (m_bStop || m_bAbort)
    return ret;

  if ((m_cached_size + pkt->size) < m_config.queue_size * 1024 * 1024) {
    Lock();
    m_cached_size += pkt->size;
    m_packets.push_back (pkt);
    UnLock();
    ret = true;
    pthread_cond_broadcast (&m_packet_cond);
    }

  return ret;
  }
//}}}
//{{{
void cOmxPlayerVideo::Process() {

  OMXPacket *omx_pkt = NULL;
  while(true) {
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
bool cOmxPlayerVideo::Decode (OMXPacket* pkt) {

  double dts = pkt->dts;
  if (dts != DVD_NOPTS_VALUE)
    dts += m_iVideoDelay;

  double pts = pkt->pts;
  if (pts != DVD_NOPTS_VALUE)
    pts += m_iVideoDelay;
  if (pts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pts;

  while ((int)m_decoder->GetFreeSpace() < pkt->size) {
    cOmxClock::sleep (10);
    if (m_flush_requested)
      return true;
    }

  cLog::Log (LOGINFO, "cOmxPlayerVideo::vidDecode dts:%.0f pts:%.0f curPts:%.0f, size:%d",
             pkt->dts, pkt->pts, m_iCurrentPts, pkt->size);
  m_decoder->Decode (pkt->data, pkt->size, dts, pts);

  return true;
  }
//}}}
