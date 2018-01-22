// cOmxAv.h
//{{{  includes
#pragma once

#include <sys/types.h>
#include <atomic>
#include <string>
#include <mutex>
#include <deque>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"

#include <IL/OMX_Image.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

#include "avLibs.h"
#include "cOmxCore.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"
#include "cPcmMap.h"

//{{{  WAVE_FORMAT defines
#define WAVE_FORMAT_UNKNOWN           0x0000
#define WAVE_FORMAT_PCM               0x0001
#define WAVE_FORMAT_ADPCM             0x0002
#define WAVE_FORMAT_IEEE_FLOAT        0x0003
#define WAVE_FORMAT_EXTENSIBLE        0xFFFE

#define SPEAKER_FRONT_LEFT            0x00001
#define SPEAKER_FRONT_RIGHT           0x00002
#define SPEAKER_FRONT_CENTER          0x00004
#define SPEAKER_LOW_FREQUENCY         0x00008
#define SPEAKER_BACK_LEFT             0x00010
#define SPEAKER_BACK_RIGHT            0x00020
#define SPEAKER_FRONT_LEFT_OF_CENTER  0x00040
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x00080
#define SPEAKER_BACK_CENTER           0x00100
#define SPEAKER_SIDE_LEFT             0x00200
#define SPEAKER_SIDE_RIGHT            0x00400
#define SPEAKER_TOP_CENTER            0x00800
#define SPEAKER_TOP_FRONT_LEFT        0x01000
#define SPEAKER_TOP_FRONT_CENTER      0x02000
#define SPEAKER_TOP_FRONT_RIGHT       0x04000
#define SPEAKER_TOP_BACK_LEFT         0x08000
#define SPEAKER_TOP_BACK_CENTER       0x10000
#define SPEAKER_TOP_BACK_RIGHT        0x20000
//}}}
//{{{
typedef struct tGUID {
  unsigned int Data1;
  unsigned short  Data2, Data3;
  uint8_t  Data4[8];
  } __attribute__((__packed__)) GUID;
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_UNKNOWN = {
  WAVE_FORMAT_UNKNOWN,
  0x0000, 0x0000,
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
  };
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_PCM = {
  WAVE_FORMAT_PCM,
  0x0000, 0x0010,
  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
  };
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
  WAVE_FORMAT_IEEE_FLOAT,
  0x0000, 0x0010,
  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
  };
//}}}
//{{{
typedef struct tWAVEFORMATEX {
  unsigned short wFormatTag;
  unsigned short nChannels;
  unsigned int   nSamplesPerSec;
  unsigned int   nAvgBytesPerSec;
  unsigned short nBlockAlign;
  unsigned short wBitsPerSample;
  unsigned short cbSize;
  } __attribute__((__packed__)) WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
//}}}
//{{{
typedef struct tWAVEFORMATEXTENSIBLE {
  WAVEFORMATEX Format;
  union {
    unsigned short wValidBitsPerSample;
    unsigned short wSamplesPerBlock;
    unsigned short wReserved;
    } Samples;
  unsigned int dwChannelMask;
  GUID SubFormat;
  } __attribute__((__packed__)) WAVEFORMATEXTENSIBLE;
//}}}
//}}}

//{{{
class cPoint {
public:
  cPoint() {}
  cPoint (float a, float b) { x = a; y = b; }

  //{{{
  cPoint operator + (const cPoint& point) const {
    cPoint ret;
    ret.x = x + point.x;
    ret.y = y + point.y;
    return ret;
    };
  //}}}
  //{{{
  const cPoint &operator += (const cPoint& point) {
    x += point.x;
    y += point.y;
    return *this;
    };
  //}}}
  //{{{
  cPoint operator - (const cPoint& point) const {
    cPoint ret;
    ret.x = x - point.x;
    ret.y = y - point.y;
    return ret;
    };
  //}}}
  //{{{
  const cPoint &operator -= (const cPoint& point) {
    x -= point.x;
    y -= point.y;
    return *this;
    };
  //}}}

  float x = 0.f;
  float y = 0.f;
  };
//}}}
//{{{
class cRect {
public:
  //{{{
  cRect() {
    x1 = y1 = x2 = y2 = 0;
    }
  //}}}
  //{{{
  cRect (float left, float top, float right, float bottom) {
    x1 = left;
    y1 = top;
    x2 = right;
    y2 = bottom;
    }
  //}}}

  //{{{
  void setRect (float left, float top, float right, float bottom) {
    x1 = left;
    y1 = top;
    x2 = right;
    y2 = bottom;
    }
  //}}}
  //{{{
  bool pointInRect (const cPoint &point) const {
    return x1 <= point.x && point.x <= x2 && y1 <= point.y && point.y <= y2;
    }
  //}}}

  //{{{
  inline const cRect& operator -= (const cPoint &point)  {
    x1 -= point.x;
    y1 -= point.y;
    x2 -= point.x;
    y2 -= point.y;
    return *this;
    }
  //}}}
  //{{{
  inline const cRect& operator += (const cPoint &point)  {
    x1 += point.x;
    y1 += point.y;
    x2 += point.x;
    y2 += point.y;
    return *this;
    }
  //}}}

  //{{{
  const cRect& intersect (const cRect &rect) {

    x1 = clampRange (x1, rect.x1, rect.x2);
    x2 = clampRange (x2, rect.x1, rect.x2);
    y1 = clampRange (y1, rect.y1, rect.y2);
    y2 = clampRange (y2, rect.y1, rect.y2);
    return *this;
    }
  //}}}
  //{{{
  const cRect& getUnion (const cRect& rect) {

    if (isEmpty())
      *this = rect;
    else if (!rect.isEmpty()) {
      x1 = std::min (x1,rect.x1);
      y1 = std::min (y1,rect.y1);

      x2 = std::max (x2,rect.x2);
      y2 = std::max (y2,rect.y2);
      }

    return *this;
    };
  //}}}

  //{{{
  inline bool isEmpty() const  {
    return (x2 - x1) * (y2 - y1) == 0;
    };
  //}}}

  //{{{
  inline float getWidth() const  {
    return x2 - x1;
    };
  //}}}
  //{{{
  inline float getHeight() const  {
    return y2 - y1;
    };
  //}}}
  //{{{
  inline float area() const  {
    return getWidth() * getHeight();
    };
  //}}}

  //{{{
  bool operator != (const cRect &rect) const {

    if (x1 != rect.x1)
      return true;
    if (x2 != rect.x2)
      return true;
    if (y1 != rect.y1)
      return true;
    if (y2 != rect.y2)
      return true;
    return false;
    };
  //}}}

  float x1, y1, x2, y2;

private:
  //{{{
  inline static float clampRange (float x, float l, float h)  {
    return (x > h) ? h : ((x < l) ? l : x);
  }
    //}}}
  };
//}}}

//{{{
enum eDeInterlaceMode {
  eDeInterlaceOff = 0,
  eDeInterlaceAuto = 1,
  eDeInterlaceForce = 2,
  eDeInterlaceAutoAdv = 3,
  eDeInterlaceForceAdv = 4,
  };
//}}}
//{{{
class cOmxVideoConfig {
public:
  cOmxStreamInfo mHints;
  int mPacketMaxCacheSize = 2 * 1024 * 1024; // 2m
  int mFifoSize = 2 * 1024 * 1024; // 2m

  cRect mDstRect = {0, 0, 0, 0};
  cRect mSrcRect = {0, 0, 0, 0};

  int mDisplay = 0;
  int mAspectMode = 0;
  float mDisplayAspect = 0.f;
  bool mHdmiClockSync = false;

  eDeInterlaceMode mDeInterlaceMode = eDeInterlaceAuto;
  };
//}}}
//{{{
class cOmxAudioConfig {
public:
  cOmxStreamInfo mHints;
  int mPacketMaxCacheSize = 512 * 1024; // 0.5m

  std::string mDevice = "omx:local";
  enum PCMLayout mLayout = PCM_LAYOUT_2_0;
  bool mBoostOnDownmix = true;
  };
//}}}

//{{{
class cOmxVideo {
public:
  ~cOmxVideo();

  std::string getDecoderName() { return mVideoCodecName; };

  bool isEOS();
  int getInputBufferSize();
  unsigned int getInputBufferSpace();

  void setAlpha (int alpha);
  void setVideoRect();
  void setVideoRect (int aspectMode);
  void setVideoRect (const cRect& srcRect, const cRect& dstRect);

  bool open (cOmxClock* clock, const cOmxVideoConfig& config);
  bool decode (uint8_t* data, int size, double dts, double pts, std::atomic<bool>& flushRequested);
  void submitEOS();
  void reset();
  void close();

private:
  std::string getInterlaceModeString (enum OMX_INTERLACETYPE mode);
  std::string getDeInterlaceModeString (eDeInterlaceMode deInterlaceMode);

  bool setNaluFormat (enum AVCodecID codec, uint8_t* in_extradata, int in_extrasize);
  bool sendDecoderExtraConfig();

  bool srcChanged();
  void logSrcChanged (OMX_PARAM_PORTDEFINITIONTYPE port, enum OMX_INTERLACETYPE interlaceMode);

  //{{{  vars
  std::recursive_mutex mMutex;

  cOmxVideoConfig mConfig;
  OMX_VIDEO_CODINGTYPE mCodingType;

  cOmxClock* mClock = nullptr;
  cOmxCore mDecoder;
  cOmxCore mScheduler;
  cOmxCore mImageFx;
  cOmxCore mRender;

  cOmxTunnel mTunnelClock;
  cOmxTunnel mTunnelDecoder;
  cOmxTunnel mTunnelImageFx;
  cOmxTunnel mTunnelSched;

  std::string mVideoCodecName;

  bool mSrcChanged = false;
  bool mSetStartTime = false;

  bool mSubmittedEos = false;
  bool mFailedEos = false;

  bool mDeInterlace = false;
  bool mDeInterlaceAdv = false;

  float mPixelAspect = 1.f;
  OMX_DISPLAYTRANSFORMTYPE mTransform = OMX_DISPLAY_ROT0;
  //}}}
  };
//}}}
//{{{
class cOmxAudio {
public:
  ~cOmxAudio();

  bool isEOS();
  double getDelay();
  float getCacheTotal();
  unsigned int getAudioRenderingLatency();
  int getChunkLen (int chans);

  int getChans() { return mCodecContext->channels; }
  int getSampleRate() { return mCodecContext->sample_rate; }
  int getBitRate() { return mCodecContext->bit_rate; }
  uint64_t getChanLayout (enum PCMLayout layout);
  std::string getDebugString();

  float getMute() { return mMute; }
  float getVolume() { return mMute ? 0.f : mCurVolume; }

  void setMute (bool mute);
  void setVolume (float volume);

  bool open (cOmxClock* clock, const cOmxAudioConfig& config);
  bool decode (uint8_t* data, int size, double dts, double pts, std::atomic<bool>& flushRequested);
  void submitEOS();
  void flush();
  void reset();

private:
  int getBitsPerSample() { return mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32; }
  uint64_t getChanMap();

  void buildChanMap (enum PCMChannels* chanMap, uint64_t layout);
  int buildChanMapCEA (enum PCMChannels* chanMap, uint64_t layout);
  void buildChanMapOMX (enum OMX_AUDIO_CHANNELTYPE* chanMap, uint64_t layout);

  bool srcChanged();
  void applyVolume();
  void addBuffer (uint8_t* data, int size, double dts, double pts);

  //{{{  vars
  std::recursive_mutex mMutex;

  cOmxAudioConfig mConfig;
  cOmxClock* mClock = nullptr;

  cOmxCore mDecoder;
  cOmxCore mMixer;
  cOmxCore mSplitter;
  cOmxCore mRenderAnal;
  cOmxCore mRenderHdmi;

  cOmxTunnel mTunnelDecoder;
  cOmxTunnel mTunnelMixer;
  cOmxTunnel mTunnelSplitterAnalog;
  cOmxTunnel mTunnelClockAnalog;
  cOmxTunnel mTunnelSplitterHdmi;
  cOmxTunnel mTunnelClockHdmi;

  OMX_AUDIO_CHANNELTYPE mInputChans[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE mOutputChans[OMX_AUDIO_MAXCHANNELS];

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cSwResample mSwResample;

  AVCodecContext* mCodecContext = nullptr;
  AVFrame* mFrame = nullptr;
  SwrContext* mConvert = nullptr;

  enum AVSampleFormat mSampleFormat = AV_SAMPLE_FMT_NONE;
  enum AVSampleFormat mOutFormat = AV_SAMPLE_FMT_NONE;

  unsigned int mNumInputChans = 0;
  unsigned int mNumOutputChans = 0;
  unsigned int mBitsPerSample = 0;

  unsigned int mBytesPerSec = 0;
  unsigned int mInputBytesPerSec = 0;
  unsigned int mBufferLen = 0;
  unsigned int mChunkLen = 0;

  bool mSetStartTime = false;
  double mLastPts = kNoPts;

  bool mSubmittedEos = false;
  bool mFailedEos = false;

  bool mMute = false;
  float mCurVolume = 1.f;
  float mLastVolume = 0.f;
  float mDownmixMatrix[OMX_AUDIO_MAXCHANNELS*OMX_AUDIO_MAXCHANNELS];

  int mChans = 0;

  bool mGotFirstFrame = true;
  bool mGotFrame = false;
  uint8_t* mOutput = nullptr;
  int mOutputAllocated = 0;
  int mOutputSize = 0;

  double mPts = 0.0;
  double mDts = 0.0;
  //}}}
  };
//}}}

//{{{
class cOmxPlayer {
public:
  //{{{
  cOmxPlayer() {
    pthread_mutex_init (&mLock, nullptr);
    pthread_mutex_init (&mLockDecoder, nullptr);
    pthread_cond_init (&mPacketCond, nullptr);
    mFlushRequested = false;
    }
  //}}}
  //{{{
  virtual ~cOmxPlayer() {
    pthread_cond_destroy (&mPacketCond);
    pthread_mutex_destroy (&mLock);
    pthread_mutex_destroy (&mLockDecoder);
    }
  //}}}

  int getNumPackets() { return mPackets.size(); };
  int getPacketCacheSize() { return mPacketCacheSize; };
  double getCurPTS() { return mCurPts; };
  double getDelay() { return mDelay; }

  void setDelay (double delay) { mDelay = delay; }

  //{{{
  bool addPacket (cOmxPacket* packet) {

    if (mAbort || ((mPacketCacheSize + packet->mSize) > mPacketMaxCacheSize))
      return false;

    lock();
    mPacketCacheSize += packet->mSize;
    mPackets.push_back (packet);
    unLock();

    pthread_cond_broadcast (&mPacketCond);
    return true;
    }
  //}}}
  //{{{
  void run (const std::string& name) {

    cLog::setThreadName (name);

    cOmxPacket* packet = nullptr;
    while (true) {
      lock();
      if (!mAbort && mPackets.empty())
        pthread_cond_wait (&mPacketCond, &mLock);

      if (mAbort) {
        unLock();
        break;
        }

      if (mFlush && packet) {
        delete (packet);
        packet = nullptr;
        mFlush = false;
        }
      else if (!packet && !mPackets.empty()) {
        packet = mPackets.front();
        mPacketCacheSize -= packet->mSize;
        mPackets.pop_front();
        }
      unLock();

      lockDecoder();
      if (packet) {
        if (mFlush) {
          delete (packet);
          packet = nullptr;
          mFlush = false;
          }
        else if (decode (packet)) {
          delete (packet);
          packet = nullptr;
          }
        }
      unLockDecoder();
      }

    delete (packet);
    packet = nullptr;

    cLog::log (LOGNOTICE, "exit");
    }
  //}}}
  //{{{
  bool decode (cOmxPacket* packet) {

    double dts = packet->mDts;
    if (dts != kNoPts)
      dts += mDelay;

    double pts = packet->mPts;
    if (pts != kNoPts) {
      pts += mDelay;
      mCurPts = pts;
      }

    return decodeDecoder (packet->mData, packet->mSize, dts, pts);
    }
  //}}}
  //{{{
  void flush() {

    mFlushRequested = true;

    lock();
    lockDecoder();

    mFlushRequested = false;

    mFlush = true;
    while (!mPackets.empty()) {
      auto packet = mPackets.front();
      mPackets.pop_front();
      delete (packet);
      packet = nullptr;
      }

    mPacketCacheSize = 0;
    mCurPts = kNoPts;

    flushDecoder();

    unLockDecoder();
    unLock();
    }
  //}}}
  //{{{
  bool close() {

    mAbort  = true;
    flush();

    lock();
    pthread_cond_broadcast (&mPacketCond);
    unLock();

    deleteDecoder();

    mStreamId = -1;
    mCurPts = kNoPts;
    mStream = nullptr;

    return true;
    }
  //}}}
  virtual void submitEOS() = 0;
  virtual void reset() = 0;

protected:
  void lock() { pthread_mutex_lock (&mLock); }
  void unLock() { pthread_mutex_unlock (&mLock); }
  void lockDecoder() { pthread_mutex_lock (&mLockDecoder); }
  void unLockDecoder() { pthread_mutex_unlock (&mLockDecoder); }

  // should be a decoder base class here
  virtual bool decodeDecoder (uint8_t* data, int size, double dts, double pts) = 0;
  virtual void flushDecoder() = 0;
  virtual void deleteDecoder() = 0;

  // vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t mPacketCond;

  cOmxClock* mClock = nullptr;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  int mStreamId = -1;
  AVStream* mStream = nullptr;

  double mCurPts = 0.0;
  double mDelay = 0.0;

  bool mAbort;
  bool mFlush = false;
  std::atomic<bool> mFlushRequested;
  std::deque<cOmxPacket*> mPackets;
  int mPacketCacheSize = 0;
  int mPacketMaxCacheSize = 0;
  };
//}}}
//{{{
class cOmxAudioPlayer : public cOmxPlayer {
public:
  cOmxAudioPlayer() : cOmxPlayer() {}
  virtual ~cOmxAudioPlayer() { close(); }

  bool isEOS() { return mPackets.empty() && mOmxAudio->isEOS(); }
  double getDelay() { return mOmxAudio->getDelay(); }
  double getCacheTotal() { return mOmxAudio->getCacheTotal(); }

  bool getMute() { return mOmxAudio->getMute(); }
  float getVolume() { return mOmxAudio->getVolume(); }
  std::string getDebugString() { return mOmxAudio->getDebugString(); }

  void setMute (bool mute) { mOmxAudio->setMute (mute); }
  void setVolume (float volume) { mOmxAudio->setVolume (volume); }

  //{{{
  bool open (cOmxClock* clock, const cOmxAudioConfig& config) {

    mClock = clock;
    mConfig = config;
    mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

    mAbort = false;
    mFlush = false;
    mFlushRequested = false;
    mPacketCacheSize = 0;
    mCurPts = kNoPts;

    mAvFormat.av_register_all();

    mOmxAudio = new cOmxAudio();
    if (mOmxAudio->open (mClock, mConfig)) {
      cLog::log (LOGINFO, "cOmxAudioPlayer::open - " + dec(mConfig.mHints.channels) +
                          "x" + dec(mConfig.mHints.samplerate) +
                          "@:" + dec(mConfig.mHints.bitspersample));
      return true;
      }
    else {
      close();
      return false;
      }
    }
  //}}}
  void submitEOS() { mOmxAudio->submitEOS(); }
  void reset() {}

private:
  bool openOmxAudio();

  //{{{
  bool decodeDecoder (uint8_t* data, int size, double dts, double pts) {
    return mOmxAudio->decode (data, size, dts, pts, mFlushRequested);
    }
  //}}}
  //{{{
  void flushDecoder() {
    mOmxAudio->reset();
    mOmxAudio->flush();
    }
  //}}}
  //{{{
  void deleteDecoder() {
    delete mOmxAudio;
    mOmxAudio = nullptr;
    }
  //}}}

  // vars
  cOmxAudioConfig mConfig;
  cOmxAudio* mOmxAudio = nullptr;
  };
//}}}
//{{{
class cOmxVideoPlayer : public cOmxPlayer {
public:
  cOmxVideoPlayer() : cOmxPlayer() {}
  virtual ~cOmxVideoPlayer() { close(); }

  bool isEOS() { return mPackets.empty() && mOmxVideo->isEOS(); }
  double getFPS() { return mFps; };
  //{{{
  std::string getDebugString() {
    return dec(mConfig.mHints.width) + "x" + dec(mConfig.mHints.height) + "@" + frac (mFps, 4,2,' ');
    }
  //}}}

  void setAlpha (int alpha) { mOmxVideo->setAlpha (alpha); }
  void setVideoRect (int aspectMode) { mOmxVideo->setVideoRect (aspectMode); }
  void setVideoRect (const cRect& SrcRect, const cRect& DestRect) { mOmxVideo->setVideoRect (SrcRect, DestRect); }

  //{{{
  bool open (cOmxClock* clock, const cOmxVideoConfig& config) {

    mClock = clock;
    mConfig = config;
    mPacketMaxCacheSize = mConfig.mPacketMaxCacheSize;

    mAbort = false;
    mFlush = false;
    mFlushRequested = false;
    mPacketCacheSize = 0;
    mCurPts = kNoPts;

    mAvFormat.av_register_all();

    mDelay = 0;

    if (mConfig.mHints.fpsrate && mConfig.mHints.fpsscale) {
      mFps = normaliseFps (mConfig.mHints.fpsscale, mConfig.mHints.fpsrate);
      if (mFps > 100.f || mFps < 5.f) {
        cLog::log (LOGERROR, "cOmxPlayerVideo::open invalid framerate " + frac (mFps,6,4,' '));
        mFps = 25.0;
        }
      }
    else
      mFps = 25.0;

    if (mConfig.mHints.codec == AV_CODEC_ID_MPEG2VIDEO) {
      cLog::log (LOGNOTICE, "cOmxPlayerVideo::open - no hw mpeg2 decoder - implement swDecoder");
      return false;
      }
    else {
      // open hw decoder
      mOmxVideo = new cOmxVideo();
      if (mOmxVideo->open (mClock, mConfig)) {
        cLog::log (LOGINFO, "cOmxPlayerVideo::open %s profile:%d %dx%d %ffps",
                   mOmxVideo->getDecoderName().c_str(), mConfig.mHints.profile,
                   mConfig.mHints.width, mConfig.mHints.height, mFps);
        return true;
        }
      else {
        close();
        return false;
        }
      }
    }
  //}}}
  //{{{
  void reset() {

    flush();

    mStreamId = -1;
    mStream = NULL;
    mCurPts = kNoPts;

    mAbort = false;
    mFlush = false;
    mFlushRequested = false;

    mPacketCacheSize = 0;
    mDelay = 0;
    }
  //}}}
  void submitEOS() { mOmxVideo->submitEOS(); }

private:
  //{{{
  double normaliseFps (int scale, int rate) {
  // if the frameDuration is within 20 microseconds of a common duration, use that

    const double durations[] = {
      1.001/24.0, 1.0/24.0, 1.0/25.0, 1.001/30.0, 1.0/30.0, 1.0/50.0, 1.001/60.0, 1.0/60.0 };

    double frameDuration = double(scale) / double(rate);
    double lowestdiff = kPtsScale;
    int selected = -1;
    for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
      double diff = fabs (frameDuration - durations[i]);
      if ((diff < 0.000020) && (diff < lowestdiff)) {
        selected = i;
        lowestdiff = diff;
        }
      }

    if (selected != -1)
      return 1.0 / durations[selected];
    else
      return 1.0 / frameDuration;
    }
  //}}}

  //{{{
  bool decodeDecoder (uint8_t* data, int size, double dts, double pts) {
    return mOmxVideo->decode (data, size, dts, pts, mFlushRequested);
    }
  //}}}
  void flushDecoder() { mOmxVideo->reset(); }
  void deleteDecoder() { delete mOmxVideo; mOmxVideo = nullptr; }

  // vars
  cOmxVideoConfig mConfig;
  cOmxVideo* mOmxVideo = nullptr;

  double mFps = 25.0;
  };
//}}}
