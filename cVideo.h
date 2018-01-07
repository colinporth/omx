#pragma once
//{{{  includes
#include <deque>
#include <string>
#include <atomic>
#include <sys/types.h>

#include "cSingleLock.h"
#include <IL/OMX_Video.h>

#include "avLibs.h"

#include "cOmxThread.h"
#include "cOmxCoreComponent.h"
#include "cOmxCoreTunnel.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"
//}}}
#define VIDEO_BUFFERS 60
enum EDEINTERLACEMODE { VS_DEINTERLACEMODE_OFF=0, VS_DEINTERLACEMODE_AUTO=1, VS_DEINTERLACEMODE_FORCE=2 };

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
  CPoint operator+(const CPoint &point) const {
    CPoint ans;
    ans.x = x + point.x;
    ans.y = y + point.y;
    return ans;
    };
  //}}}
  //{{{
  const CPoint &operator+=(const CPoint &point) {
    x += point.x;
    y += point.y;
    return *this;
    };
  //}}}
  //{{{
  CPoint operator-(const CPoint &point) const {
    CPoint ans;
    ans.x = x - point.x;
    ans.y = y - point.y;
    return ans;
    };
  //}}}
  //{{{
  const CPoint &operator-=(const CPoint &point) {
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
  void SetRect(float left, float top, float right, float bottom) {
    x1 = left; y1 = top; x2 = right; y2 = bottom;
    };
  //}}}
  //{{{
  bool PtInRect(const CPoint &point) const {
    if (x1 <= point.x && point.x <= x2 && y1 <= point.y && point.y <= y2)
      return true;
    return false;
  };
  //}}}

  //{{{
  inline const CRect &operator -=(const CPoint &point)  {
    x1 -= point.x;
    y1 -= point.y;
    x2 -= point.x;
    y2 -= point.y;
    return *this;
  };
  //}}}
  //{{{
  inline const CRect &operator +=(const CPoint &point)  {
    x1 += point.x;
    y1 += point.y;
    x2 += point.x;
    y2 += point.y;
    return *this;
  };
  //}}}

  //{{{
  const CRect &Intersect(const CRect &rect) {
    x1 = clamp_range(x1, rect.x1, rect.x2);
    x2 = clamp_range(x2, rect.x1, rect.x2);
    y1 = clamp_range(y1, rect.y1, rect.y2);
    y2 = clamp_range(y2, rect.y1, rect.y2);
    return *this;
  };
  //}}}
  //{{{
  const CRect &Union(const CRect &rect) {
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
  bool operator !=(const CRect &rect) const {
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
  inline static float clamp_range(float x, float l, float h)  {
    return (x > h) ? h : ((x < l) ? l : x);
  }
  //}}}
};
//}}}
//{{{
class cOmxVideoConfig {
public:
  cOmxVideoConfig() {
    dst_rect.SetRect (0, 0, 0, 0);
    src_rect.SetRect (0, 0, 0, 0);
    display_aspect = 0.0f;
    deinterlace = VS_DEINTERLACEMODE_AUTO;
    advanced_hd_deinterlace = true;
    hdmi_clock_sync = false;
    allow_mvc = false;
    alpha = 255;
    aspectMode = 0;
    display = 0;
    layer = 0;
    queue_size = 10.0f;
    fifo_size = (float)80*1024*60 / (1024*1024);
    }

  cOmxStreamInfo hints;

  CRect dst_rect;
  CRect src_rect;
  float display_aspect;
  EDEINTERLACEMODE deinterlace;
  bool advanced_hd_deinterlace;

  bool hdmi_clock_sync;
  bool allow_mvc;
  int alpha;
  int aspectMode;
  int display;
  int layer;
  float queue_size;
  float fifo_size;
  };
//}}}
//{{{
class cOmxVideo {
public:
  cOmxVideo();
  ~cOmxVideo();

  // Required overrides
  bool SendDecoderConfig();
  bool NaluFormatStartCodes (enum AVCodecID codec, uint8_t *in_extradata, int in_extrasize);

  bool Open (cOmxClock* clock, const cOmxVideoConfig& config);

  bool PortSettingsChanged();
  void PortSettingsChangedLogger (OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode);

  unsigned int GetSize();
  int GetInputBufferSize();
  unsigned int GetFreeSpace();

  std::string GetDecoderName() { return m_video_codec_name; };
  bool BadState() { return m_omx_decoder.BadState(); };

  void SetDropState (bool bDrop);
  void SetVideoRect (const CRect& SrcRect, const CRect& DestRect);
  void SetVideoRect (int aspectMode);
  void SetVideoRect();
  void SetAlpha (int alpha);

  bool Decode (uint8_t* data, int size, double dts, double pts);
  void Reset();

  bool IsEOS();
  void SubmitEOS();
  bool SubmittedEOS() { return m_submitted_eos; }

protected:
  cCriticalSection  m_critSection;

  bool               m_drop_state;
  OMX_VIDEO_CODINGTYPE m_codingType;

  cOmxCoreComponent  m_omx_decoder;
  cOmxCoreComponent  m_omx_render;
  cOmxCoreComponent  m_omx_sched;
  cOmxCoreComponent  m_omx_image_fx;
  cOmxCoreComponent* m_omx_clock;
  cOmxClock*         m_av_clock;

  cOmxCoreTunnel     m_omx_tunnel_decoder;
  cOmxCoreTunnel     m_omx_tunnel_clock;
  cOmxCoreTunnel     m_omx_tunnel_sched;
  cOmxCoreTunnel     m_omx_tunnel_image_fx;
  bool               m_is_open;

  bool               m_setStartTime;
  std::string        m_video_codec_name;
  bool               m_deinterlace;

  cOmxVideoConfig    m_config;
  float              m_pixel_aspect;
  bool               m_submitted_eos;
  bool               m_failed_eos;
  OMX_DISPLAYTRANSFORMTYPE m_transform;
  bool               m_settings_changed;

private:
  void Close();
  };
//}}}

class cOmxPlayerVideo : public cOmxThread {
public:
  cOmxPlayerVideo();
  ~cOmxPlayerVideo();

  bool Reset();
  bool Open (cOmxClock* av_clock, const cOmxVideoConfig& config);

  int GetDecoderBufferSize();
  int GetDecoderFreeSpace();
  double GetCurrentPTS() { return m_iCurrentPts; };
  double GetFPS() { return m_fps; };
  //{{{
  unsigned int GetLevel() {
    return m_config.queue_size ? 100.0f * m_cached_size / (m_config.queue_size * 1024.0f * 1024.0f) : 0;
    };
  //}}}
  unsigned int GetCached() { return m_cached_size; };
  //{{{
  unsigned int GetMaxCached() {
    return m_config.queue_size * 1024 * 1024;
    };
  //}}}
  double GetDelay() { return m_iVideoDelay; }

  void SetDelay (double delay) { m_iVideoDelay = delay; }
  void SetAlpha (int alpha);
  void SetVideoRect (const CRect& SrcRect, const CRect& DestRect);
  void SetVideoRect (int aspectMode);

  bool AddPacket (OMXPacket* pkt);
  void Process();
  void Flush();

  bool IsEOS();
  void SubmitEOS();

protected:
  //{{{  vars
  pthread_cond_t         m_packet_cond;
  pthread_cond_t         m_picture_cond;

  pthread_mutex_t        m_lock;
  pthread_mutex_t        m_lock_decoder;

  cOmxClock*             m_av_clock;
  cOmxVideo*             m_decoder;
  cOmxVideoConfig        m_config;

  AVStream*              m_pStream;
  int                    m_stream_id;
  std::deque<OMXPacket*> m_packets;
  cAvUtil                mAvUtil;
  cAvCodec               mAvCodec;
  cAvFormat              mAvFormat;
  bool                   m_open;
  double                 m_iCurrentPts;

  float                  m_fps;
  double                 m_frametime;
  float                  m_display_aspect;
  bool                   m_bAbort;
  bool                   m_flush;
  std::atomic<bool>      m_flush_requested;
  unsigned int           m_cached_size;
  double                 m_iVideoDelay;
  //}}}

private:
  bool Close();

  void Lock() { pthread_mutex_lock (&m_lock); }
  void UnLock() { pthread_mutex_unlock (&m_lock); }
  void LockDecoder() { pthread_mutex_lock  (&m_lock_decoder); }
  void UnLockDecoder() { pthread_mutex_unlock (&m_lock_decoder); }

  bool OpenDecoder();
  void CloseDecoder();

  bool Decode (OMXPacket* pkt);
  };
