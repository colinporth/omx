//{{{  includes
#pragma once

#include <sys/types.h>
#include <atomic>
#include <string>
#include <mutex>
#include <deque>

#include "avLibs.h"
#include "cOmxCoreComponent.h"
#include "cOmxCoreTunnel.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"
//}}}
//{{{
enum EDEINTERLACEMODE {
  VS_DEINTERLACEMODE_OFF = 0,
  VS_DEINTERLACEMODE_AUTO = 1,
  VS_DEINTERLACEMODE_FORCE = 2 };
//}}}

//{{{
class CPoint {
public:
  //{{{
  CPoint() {
    x = 0;
    y = 0;
    };
  //}}}
  //{{{
  CPoint(float a, float b) {
    x = a;
    y = b;
    };
  //}}}

  //{{{
  CPoint operator + (const CPoint &point) const {
    CPoint ans;
    ans.x = x + point.x;
    ans.y = y + point.y;
    return ans;
    };
  //}}}
  //{{{
  const CPoint &operator += (const CPoint &point) {
    x += point.x;
    y += point.y;
    return *this;
    };
  //}}}
  //{{{
  CPoint operator - (const CPoint &point) const {
    CPoint ans;
    ans.x = x - point.x;
    ans.y = y - point.y;
    return ans;
    };
  //}}}
  //{{{
  const CPoint &operator -= (const CPoint &point) {
    x -= point.x;
    y -= point.y;
    return *this;
    };
  //}}}

  float x, y;
  };
//}}}
//{{{
class CRect {
public:
  //{{{
  CRect() {
    x1 = y1 = x2 = y2 = 0;
    };
  //}}}
  //{{{
  CRect (float left, float top, float right, float bottom) {
    x1 = left; y1 = top; x2 = right; y2 = bottom;
    };
  //}}}

  //{{{
  void SetRect (float left, float top, float right, float bottom) {
    x1 = left; y1 = top; x2 = right; y2 = bottom;
    };
  //}}}
  //{{{
  bool PtInRect (const CPoint &point) const {
    if (x1 <= point.x && point.x <= x2 && y1 <= point.y && point.y <= y2)
      return true;
    return false;
  };
  //}}}

  //{{{
  inline const CRect& operator -= (const CPoint &point)  {
    x1 -= point.x;
    y1 -= point.y;
    x2 -= point.x;
    y2 -= point.y;
    return *this;
  };
  //}}}
  //{{{
  inline const CRect& operator += (const CPoint &point)  {
    x1 += point.x;
    y1 += point.y;
    x2 += point.x;
    y2 += point.y;
    return *this;
  };
  //}}}

  //{{{
  const CRect& Intersect (const CRect &rect) {
    x1 = clamp_range(x1, rect.x1, rect.x2);
    x2 = clamp_range(x2, rect.x1, rect.x2);
    y1 = clamp_range(y1, rect.y1, rect.y2);
    y2 = clamp_range(y2, rect.y1, rect.y2);
    return *this;
  };
  //}}}
  //{{{
  const CRect& Union (const CRect &rect) {
    if (IsEmpty())
      *this = rect;
    else if (!rect.IsEmpty())
    {
      x1 = std::min(x1,rect.x1);
      y1 = std::min(y1,rect.y1);

      x2 = std::max(x2,rect.x2);
      y2 = std::max(y2,rect.y2);
    }

    return *this;
  };
  //}}}

  //{{{
  inline bool IsEmpty() const  {
    return (x2 - x1) * (y2 - y1) == 0;
  };
  //}}}

  //{{{
  inline float Width() const  {
    return x2 - x1;
  };
  //}}}
  //{{{
  inline float Height() const  {
    return y2 - y1;
  };
  //}}}
  //{{{
  inline float Area() const  {
    return Width() * Height();
    };
  //}}}

  //{{{
  bool operator != (const CRect &rect) const {
    if (x1 != rect.x1) return true;
    if (x2 != rect.x2) return true;
    if (y1 != rect.y1) return true;
    if (y2 != rect.y2) return true;
    return false;
  };
  //}}}

  float x1, y1, x2, y2;

private:
  //{{{
  inline static float clamp_range (float x, float l, float h)  {
    return (x > h) ? h : ((x < l) ? l : x);
  }
  //}}}
  };
//}}}

//{{{
class cOmxVideoConfig {
public:
  cOmxStreamInfo mHints;

  float mQueueSize = 5.f;
  float mFifoSize = (float)80*1024*60 / (1024*1024);

  CRect mDstRect = {0, 0, 0, 0};
  CRect mSrcRect = {0, 0, 0, 0};

  float mDisplayAspect = 0.f;
  int mAspectMode = 0;
  int mAlpha = 255;
  int mDisplay = 0;
  int mLayer = 0;

  bool mHdmiClockSync = false;

  EDEINTERLACEMODE mDeinterlace = VS_DEINTERLACEMODE_AUTO;
  bool mAdvancedHdDeinterlace = true;
  };
//}}}
//{{{
class cOmxVideo {
public:
  ~cOmxVideo() { close(); }

  bool sendDecoderConfig();
  bool naluFormatStartCodes (enum AVCodecID codec, uint8_t* in_extradata, int in_extrasize);

  std::string getDecoderName() { return mVideoCodecName; };

  bool isEOS();
  int getInputBufferSize();
  unsigned int GetInputBufferSpace();

  void setAlpha (int alpha);
  void setVideoRect();
  void setVideoRect (int aspectMode);
  void setVideoRect (const CRect& srcRect, const CRect& dstRect);
  void setDropState (bool drop) { mDropState = drop; }

  bool open (cOmxClock* clock, const cOmxVideoConfig& config);
  bool portSettingsChanged();
  bool decode (uint8_t* data, int size, double dts, double pts);
  void submitEOS();
  void reset();
  void close();

private:
  void portSettingsChangedLog (OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode);

  //{{{  vars
  std::recursive_mutex mMutex;

  cOmxVideoConfig mConfig;
  OMX_VIDEO_CODINGTYPE mCodingType;

  cOmxClock* mAvClock = nullptr;
  cOmxCoreComponent* mOmxClock = nullptr;
  cOmxCoreComponent mOmxDecoder;
  cOmxCoreComponent mOmxRender;
  cOmxCoreComponent mOmxSched;
  cOmxCoreComponent mOmxImageFx;

  cOmxCoreTunnel mOmxTunnelDecoder;
  cOmxCoreTunnel mOmxTunnelClock;
  cOmxCoreTunnel mOmxTunnelSched;
  cOmxCoreTunnel mOmxTunnelImageFx;

  std::string mVideoCodecName;

  bool mFailedEos = false;
  bool mSettingsChanged = false;
  bool mSetStartTime = false;
  bool mDeinterlace = false;
  bool mDropState = false;
  bool mSubmittedEos = false;

  float mPixelAspect = 1.f;
  OMX_DISPLAYTRANSFORMTYPE mTransform = OMX_DISPLAY_ROT0;
  //}}}
  };
//}}}

class cOmxPlayerVideo {
public:
  cOmxPlayerVideo();
  ~cOmxPlayerVideo();

  bool isEOS() { return mPackets.empty() && mDecoder->isEOS(); }
  double getCurrentPTS() { return mCurrentPts; };
  double getFPS() { return mFps; };
  //{{{
  unsigned int getLevel() {
    return mConfig.mQueueSize ? 100.f * mCachedSize / (mConfig.mQueueSize * 1024.f * 1024.f) : 0;
    };
  //}}}
  unsigned int getCached() { return mCachedSize; };
  unsigned int getMaxCached() { return mConfig.mQueueSize * 1024 * 1024; };
  double getDelay() { return mVideoDelay; }

  void setDelay (double delay) { mVideoDelay = delay; }
  void setAlpha (int alpha) { mDecoder->setAlpha (alpha); }
  void setVideoRect (int aspectMode) { mDecoder->setVideoRect (aspectMode); }
  void setVideoRect (const CRect& SrcRect, const CRect& DestRect) { mDecoder->setVideoRect (SrcRect, DestRect); }

  bool open (cOmxClock* avClock, const cOmxVideoConfig& config);
  void run();
  bool addPacket (OMXPacket* packet);
  void submitEOS();
  void flush();
  void reset();
  bool close();

private:
  void lock() { pthread_mutex_lock (&mLock); }
  void unLock() { pthread_mutex_unlock (&mLock); }
  void lockDecoder() { pthread_mutex_lock  (&mLockDecoder); }
  void unLockDecoder() { pthread_mutex_unlock (&mLockDecoder); }

  bool decode (OMXPacket* packet);

  //{{{  vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t mPacketCond;
  pthread_cond_t mVideoCond;

  cOmxClock* mAvClock = nullptr;
  cOmxVideo* mDecoder = nullptr;
  cOmxVideoConfig mConfig;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  bool mAbort = false;
  bool mFlush = false;
  std::atomic<bool>  mFlushRequested;
  unsigned int mCachedSize = 0;
  std::deque<OMXPacket*> mPackets;

  int mStreamId = -1;
  AVStream* mStream = nullptr;

  double mVideoDelay = 0.0;
  double mCurrentPts = 0.0;

  float mFps = 25.f;
  double mFrametime = 0.0;
  float mDisplayAspect = false;
  //}}}
  };
