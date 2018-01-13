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

  pthread_cond_init (&mPacketCond, NULL);
  pthread_cond_init (&mPictureCond, NULL);

  mFlushRequested = false;
  }
//}}}
//{{{
cOmxPlayerVideo::~cOmxPlayerVideo() {

  close();

  pthread_cond_destroy (&mPacketCond);
  pthread_cond_destroy (&mPictureCond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerVideo::open (cOmxClock* av_clock, const cOmxVideoConfig& config) {

  mAvFormat.av_register_all();

  mConfig = config;
  mAvClock = av_clock;

  mFps = 25.f;
  mFrametime = 0;

  mICurrentPts = DVD_NOPTS_VALUE;

  mAbort = false;
  mFlush = false;
  mCachedSize = 0;
  mIVideoDelay = 0;

  // open decoder
  if (mConfig.hints.fpsrate && mConfig.hints.fpsscale)
    mFps = 1000000.f /
            cOmxReader::normalizeFrameDuration (1000000.0 * mConfig.hints.fpsscale / mConfig.hints.fpsrate);
  else
    mFps = 25.f;

  if (mFps > 100.f || mFps < 5.f) {
    cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate %d, using 25fps", (int)mFps);
    mFps = 25.f;
    }
  mFrametime = 1000000.0 / mFps;

  mDecoder = new cOmxVideo();
  if (mDecoder->open (mAvClock, mConfig)) {
    cLog::log (LOGINFO, "cOmxPlayerVideo::OpenDecoder %s profile:%d %dx%d %ffps",
               mDecoder->getDecoderName().c_str(), mConfig.hints.profile,
               mConfig.hints.width, mConfig.hints.height, mFps);
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
      pthread_cond_wait (&mPacketCond, &mLock);

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
      ((mCachedSize + pkt->size) < (mConfig.queue_size * 1024 * 1024))) {

    lock();
    mCachedSize += pkt->size;
    mPackets.push_back (pkt);
    unLock();

    pthread_cond_broadcast (&mPacketCond);
    return true;
    }

  return false;
  }
//}}}
//{{{
void cOmxPlayerVideo::submitEOS() {

  mDecoder->submitEOS();
  }
//}}}
//{{{
void cOmxPlayerVideo::flush() {

  mFlushRequested = true;

  lock();
  lockDecoder();

  mFlushRequested = false;
  mFlush = true;
  while (!mPackets.empty()) {
    auto pkt = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (pkt);
    }

  mICurrentPts = DVD_NOPTS_VALUE;
  mCachedSize = 0;

  mDecoder->reset();

  unLockDecoder();
  unLock();
  }
//}}}
//{{{
void cOmxPlayerVideo::reset() {

  flush();

  mStreamId = -1;
  mStream = NULL;
  mICurrentPts = DVD_NOPTS_VALUE;
  mFrametime = 0;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;

  mCachedSize = 0;
  mIVideoDelay = 0;
  }
//}}}
//{{{
bool cOmxPlayerVideo::close() {

  mAbort  = true;

  flush();

  lock();
  pthread_cond_broadcast (&mPacketCond);
  unLock();

  delete mDecoder;
  mDecoder = nullptr;

  mStreamId = -1;
  mICurrentPts = DVD_NOPTS_VALUE;
  mStream = nullptr;

  return true;
  }
//}}}

// private
//{{{
bool cOmxPlayerVideo::decode (OMXPacket* pkt) {

  double dts = pkt->dts;
  if (dts != DVD_NOPTS_VALUE)
    dts += mIVideoDelay;

  double pts = pkt->pts;
  if (pts != DVD_NOPTS_VALUE) {
    pts += mIVideoDelay;
    mICurrentPts = pts;
    }
  else
    cLog::log (LOGINFO, "decode - DVD_NOPTS_VALUE");

  cLog::log (LOGINFO1, "Decode pts:%6.2f curPts:%6.2f size:%d",
                       pkt->pts / 1000000.f, mICurrentPts / 1000000.f, pkt->size);

  while ((int)mDecoder->getFreeSpace() < pkt->size) {
    cOmxClock::sleep (10);
    if (mFlushRequested)
      return true;
    }

  return mDecoder->decode (pkt->data, pkt->size, dts, pts);
  }
//}}}
