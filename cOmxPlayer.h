//{{{  includes
#pragma once

#include <sys/types.h>
#include <atomic>
#include <string>
#include <mutex>
#include <deque>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"

#include "avLibs.h"
#include "cOmxCoreComponent.h"
#include "cOmxCoreTunnel.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"
//}}}

class cOmxPlayer {
public:
  cOmxPlayer() {}
  virtual ~cOmxPlayer() {}

  int getNumPackets() { return mPackets.size(); };
  int getPacketCacheSize() { return mPacketCacheSize; };
  double getCurrentPTS() { return mCurrentPts; };

  //{{{
  bool addPacket (OMXPacket* packet) {

    if (mAbort || ((mPacketCacheSize + packet->size) > mPacketMaxCacheSize))
      return false;

    lock();
    mPacketCacheSize += packet->size;
    mPackets.push_back (packet);
    unLock();

    pthread_cond_broadcast (&mPacketCond);
    return true;
    }
  //}}}
  //{{{
  void run (const std::string& name) {

    cLog::setThreadName (name);

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
        mPacketCacheSize -= packet->size;
        mPackets.pop_front();
        }
      unLock();

      lockDecoder();
      if (packet) {
        if (mFlush) {
          cOmxReader::freePacket (packet);
          mFlush = false;
          }
        else if (decode (packet))
          cOmxReader::freePacket (packet);
        }
      unLockDecoder();
      }

    cOmxReader::freePacket (packet);

    cLog::log (LOGNOTICE, "exit");
    }
  //}}}
  virtual void submitEOS() = 0;
  virtual void flush() = 0;
  virtual void reset() = 0;
  virtual bool close() = 0;

protected:
  void lock() { pthread_mutex_lock (&mLock); }
  void unLock() { pthread_mutex_unlock (&mLock); }
  void lockDecoder() { pthread_mutex_lock (&mLockDecoder); }
  void unLockDecoder() { pthread_mutex_unlock (&mLockDecoder); }

  virtual bool decode (OMXPacket* packet) = 0;

  // vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t mPacketCond;

  cOmxClock* mAvClock = nullptr;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  int mStreamId = -1;
  AVStream* mStream = nullptr;

  double mCurrentPts = 0.0;

  bool mAbort;
  bool mFlush = false;
  std::atomic<bool> mFlushRequested;
  std::deque<OMXPacket*> mPackets;
  int mPacketCacheSize = 0;
  int mPacketMaxCacheSize = 0;
  };
