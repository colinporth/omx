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
#include "cOmxPlayer.h"
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

  int mPacketMaxCacheSize = 2 * 1024 * 1024; // 1m
  int mFifoSize = 2 * 1024 * 1024; // 2m

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

  std::string getDecoderName() { return mVideoCodecName; };

  bool isEOS();
  int getInputBufferSize();
  unsigned int getInputBufferSpace();

  void setAlpha (int alpha);
  void setVideoRect();
  void setVideoRect (int aspectMode);
  void setVideoRect (const CRect& srcRect, const CRect& dstRect);
  void setDropState (bool drop) { mDropState = drop; }

  bool open (cOmxClock* clock, const cOmxVideoConfig& config);
  bool portChanged();
  bool decode (uint8_t* data, int size, double dts, double pts);
  void submitEOS();
  void reset();
  void close();

private:
  bool sendDecoderExtraConfig();
  bool naluFormatStartCodes (enum AVCodecID codec, uint8_t* in_extradata, int in_extrasize);

  void logPortChanged (OMX_PARAM_PORTDEFINITIONTYPE port, int interlaceMode);

  //{{{  vars
  std::recursive_mutex mMutex;

  cOmxVideoConfig mConfig;
  OMX_VIDEO_CODINGTYPE mCodingType;

  cOmxClock* mAvClock = nullptr;
  cOmxCoreComponent* mClock = nullptr;
  cOmxCoreComponent mDecoder;
  cOmxCoreComponent mRender;
  cOmxCoreComponent mScheduler;
  cOmxCoreComponent mImageFx;

  cOmxCoreTunnel mTunnelDecoder;
  cOmxCoreTunnel mTunnelClock;
  cOmxCoreTunnel mTunnelSched;
  cOmxCoreTunnel mTunnelImageFx;

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

class cOmxPlayerVideo : public cOmxPlayer {
public:
  cOmxPlayerVideo();
  ~cOmxPlayerVideo();

  bool isEOS() { return mPackets.empty() && mDecoder->isEOS(); }
  double getDelay() { return mVideoDelay; }
  double getFPS() { return mFps; };

  void setDelay (double delay) { mVideoDelay = delay; }
  void setAlpha (int alpha) { mDecoder->setAlpha (alpha); }
  void setVideoRect (int aspectMode) { mDecoder->setVideoRect (aspectMode); }
  void setVideoRect (const CRect& SrcRect, const CRect& DestRect) { mDecoder->setVideoRect (SrcRect, DestRect); }

  bool open (cOmxClock* avClock, const cOmxVideoConfig& config);
  void submitEOS();
  void flush();
  void reset();
  bool close();

private:
  bool decode (OMXPacket* packet);

  //{{{  vars
  cOmxVideoConfig mConfig;
  cOmxVideo* mDecoder = nullptr;

  double mVideoDelay = 0.0;
  float mFps = 25.f;
  double mFrametime = 0.0;
  float mDisplayAspect = false;
  //}}}
  };
