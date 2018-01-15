// cOmxPlayerVideo.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "cVideo.h"
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"

using namespace std;
//}}}

//{{{
cOmxPlayerVideo::cOmxPlayerVideo() : cOmxPlayer() {

  pthread_mutex_init (&mLock, NULL);
  pthread_mutex_init (&mLockDecoder, NULL);
  pthread_cond_init (&mPacketCond, NULL);

  mFlushRequested = false;
  }
//}}}
//{{{
cOmxPlayerVideo::~cOmxPlayerVideo() {

  close();

  pthread_cond_destroy (&mPacketCond);
  pthread_mutex_destroy (&mLock);
  pthread_mutex_destroy (&mLockDecoder);
  }
//}}}

//{{{
bool cOmxPlayerVideo::open (cOmxClock* avClock, const cOmxVideoConfig& config) {

  mConfig = config;
  mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

  mAvFormat.av_register_all();
  mAvClock = avClock;

  mFrametime = 0;
  mCurrentPts = DVD_NOPTS_VALUE;
  mVideoDelay = 0;

  mAbort = false;
  mFlush = false;
  mPacketCacheSize = 0;

  if (mConfig.mHints.fpsrate && mConfig.mHints.fpsscale)
    mFps = 1000000.f / cOmxReader::normDur (1000000.0 * mConfig.mHints.fpsscale / mConfig.mHints.fpsrate);
  else
    mFps = 25.f;

  if (mFps > 100.f || mFps < 5.f) {
    cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate %d", (int)mFps);
    mFps = 25.f;
    }
  mFrametime = 1000000.0 / mFps;

  // open decoder
  mDecoder = new cOmxVideo();
  if (mDecoder->open (mAvClock, mConfig)) {
    cLog::log (LOGINFO, "cOmxPlayerVideo::open %s profile:%d %dx%d %ffps",
               mDecoder->getDecoderName().c_str(), mConfig.mHints.profile,
               mConfig.mHints.width, mConfig.mHints.height, mFps);
    return true;
    }
  else {
    close();
    return false;
    }
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
    auto packet = mPackets.front();
    mPackets.pop_front();
    cOmxReader::freePacket (packet);
    }

  mCurrentPts = DVD_NOPTS_VALUE;
  mPacketCacheSize = 0;

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
  mCurrentPts = DVD_NOPTS_VALUE;
  mFrametime = 0;

  mAbort = false;
  mFlush = false;
  mFlushRequested = false;

  mPacketCacheSize = 0;
  mVideoDelay = 0;
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
  mCurrentPts = DVD_NOPTS_VALUE;
  mStream = nullptr;

  return true;
  }
//}}}

// private
//{{{
bool cOmxPlayerVideo::decode (OMXPacket* packet) {

  double dts = packet->dts;
  if (dts != DVD_NOPTS_VALUE)
    dts += mVideoDelay;

  double pts = packet->pts;
  if (pts != DVD_NOPTS_VALUE) {
    pts += mVideoDelay;
    mCurrentPts = pts;
    }

  cLog::log (LOGINFO1, "decode pts" +
                       (packet->pts == DVD_NOPTS_VALUE) ? "none" : decFrac(packet->pts / 1000000.f, 6,2,' ') +
                       " curPts" + decFrac(mCurrentPts / 1000000.f, 6,2,' ') +
                       " size" + dec(packet->size));

  while ((int)mDecoder->getInputBufferSpace() < packet->size) {
    cOmxClock::sleep (10);
    if (mFlushRequested)
      return true;
    }

  return mDecoder->decode (packet->data, packet->size, dts, pts);
  }
//}}}
