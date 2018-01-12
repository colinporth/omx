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

  close();

  pthread_cond_destroy (&m_packet_cond);
  pthread_cond_destroy (&m_picture_cond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerVideo::open (cOmxClock* av_clock, const cOmxVideoConfig& config) {

  mAvFormat.av_register_all();

  m_config = config;
  m_av_clock = av_clock;

  m_fps = 25.f;
  m_frametime = 0;

  m_iCurrentPts = DVD_NOPTS_VALUE;

  mAbort = false;
  mFlush = false;
  mCachedSize = 0;
  m_iVideoDelay = 0;

  // open decoder
  if (m_config.hints.fpsrate && m_config.hints.fpsscale)
    m_fps = 1000000.f /
            cOmxReader::normalizeFrameDuration (1000000.0 * m_config.hints.fpsscale / m_config.hints.fpsrate);
  else
    m_fps = 25.f;

  if (m_fps > 100.f || m_fps < 5.f) {
    cLog::log (LOGERROR, "cOmxPlayerVideo::OpenDecoder = invalid framerate %d, using forced 25fps, trust timestamps", (int)m_fps);
    m_fps = 25.f;
    }
  m_frametime = 1000000.0 / m_fps;

  mDecoder = new cOmxVideo();
  if (mDecoder->Open (m_av_clock, m_config)) {
    cLog::log (LOGINFO, "cOmxPlayerVideo::OpenDecoder %s profile:%d %dx%d %ffps",
               mDecoder->GetDecoderName().c_str(), m_config.hints.profile,
               m_config.hints.width, m_config.hints.height, m_fps);
    return true;
    }
  else {
    close();
    return false;
    }
  }
//}}}
//{{{
void cOmxPlayerVideo::run() {

  cLog::setThreadName ("vid ");

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
    if (omx_pkt) {
      if (mFlush) {
        cOmxReader::freePacket (omx_pkt);
        mFlush = false;
        }
      else if (decode (omx_pkt))
        cOmxReader::freePacket (omx_pkt);
      }
    unLockDecoder();
    }

  cOmxReader::freePacket (omx_pkt);

  cLog::log (LOGNOTICE, "exit");
  }
//}}}
//{{{
bool cOmxPlayerVideo::addPacket (OMXPacket* pkt) {

  if (!mAbort &&
      ((mCachedSize + pkt->size) < (m_config.queue_size * 1024 * 1024))) {

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
void cOmxPlayerVideo::submitEOS() {

  mDecoder->SubmitEOS();
  }
//}}}
//{{{
void cOmxPlayerVideo::flush() {

  mFlush_requested = true;

  lock();
  lockDecoder();

  mFlush_requested = false;
  mFlush = true;
  while (!mPackets.empty()) {
    auto pkt = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (pkt);
    }

  m_iCurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;

  mDecoder->Reset();

  unLockDecoder();
  unLock();
  }
//}}}
//{{{
void cOmxPlayerVideo::reset() {

  flush();

  m_stream_id = -1;
  m_pStream = NULL;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_frametime = 0;

  mAbort = false;
  mFlush = false;
  mFlush_requested = false;

  mCachedSize = 0;
  m_iVideoDelay = 0;
  }
//}}}
//{{{
bool cOmxPlayerVideo::close() {

  mAbort  = true;

  flush();

  lock();
  pthread_cond_broadcast (&m_packet_cond);
  unLock();

  delete mDecoder;
  mDecoder = nullptr;

  m_stream_id = -1;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_pStream = nullptr;

  return true;
  }
//}}}

// private
//{{{
bool cOmxPlayerVideo::decode (OMXPacket* pkt) {

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

  while ((int)mDecoder->GetFreeSpace() < pkt->size) {
    cOmxClock::sleep (10);
    if (mFlush_requested)
      return true;
    }

  return mDecoder->Decode (pkt->data, pkt->size, dts, pts);
  }
//}}}
