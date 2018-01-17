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
  unsigned short    wFormatTag;
  unsigned short    nChannels;
  unsigned int   nSamplesPerSec;
  unsigned int   nAvgBytesPerSec;
  unsigned short    nBlockAlign;
  unsigned short    wBitsPerSample;
  unsigned short    cbSize;
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

#include "cPcmRemap.h"
//}}}
#define AUDIO_BUFFER_SECONDS 3

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
enum eInterlaceMode {
  eInterlaceOff = 0,
  eInterlaceAuto = 1,
  eInterlaceForce = 2 };
//}}}
//{{{
class cOmxVideoConfig {
public:
  cOmxStreamInfo mHints;

  int mPacketMaxCacheSize = 2 * 1024 * 1024; // 1m
  int mFifoSize = 2 * 1024 * 1024; // 2m

  cRect mDstRect = {0, 0, 0, 0};
  cRect mSrcRect = {0, 0, 0, 0};

  float mDisplayAspect = 0.f;
  int mAspectMode = 0;
  int mAlpha = 255;
  int mDisplay = 0;
  int mLayer = 0;

  bool mHdmiClockSync = false;

  eInterlaceMode mDeinterlace = eInterlaceAuto;
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
  void setVideoRect (const cRect& srcRect, const cRect& dstRect);
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
  cOmxCore* mClock = nullptr;
  cOmxCore mDecoder;
  cOmxCore mRender;
  cOmxCore mScheduler;
  cOmxCore mImageFx;

  cOmxTunnel mTunnelDecoder;
  cOmxTunnel mTunnelClock;
  cOmxTunnel mTunnelSched;
  cOmxTunnel mTunnelImageFx;

  std::string mVideoCodecName;

  bool mFailedEos = false;
  bool mPortChanged = false;
  bool mSetStartTime = false;
  bool mDeinterlace = false;
  bool mDropState = false;
  bool mSubmittedEos = false;

  float mPixelAspect = 1.f;
  OMX_DISPLAYTRANSFORMTYPE mTransform = OMX_DISPLAY_ROT0;
  //}}}
  };
//}}}
//{{{
class cOmxAudioConfig {
public:
  cOmxStreamInfo mHints;

  int mPacketMaxCacheSize = 512 * 1024; // 0.5m

  std::string mDevice;

  enum PCMLayout mLayout = PCM_LAYOUT_2_0;
  bool mBoostOnDownmix = true;

  bool mPassThru = false;
  bool mHwDecode = false;

  bool mIsLive = false;
  };
//}}}

//{{{
class cSwAudio {
public:
  ~cSwAudio();

  uint64_t getChannelMap();
  unsigned int getFrameSize() { return mFrameSize; }
  int getData (uint8_t** dst, double &dts, double &pts);
  int getChannels() { return mCodecContext->channels; }
  int getSampleRate() { return mCodecContext->sample_rate; }
  int getBitRate() { return mCodecContext->bit_rate; }
  int getBitsPerSample() { return mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32; }

  bool open (cOmxStreamInfo& hints, enum PCMLayout layout);
  int decode (uint8_t* data, int size, double dts, double pts);
  void reset();
  void dispose();

protected:
  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cSwResample mSwResample;

  SwrContext* mConvert = nullptr;
  AVCodecContext* mCodecContext = nullptr;
  AVFrame* mFrame = nullptr;

  enum AVSampleFormat mSampleFormat = AV_SAMPLE_FMT_NONE;
  enum AVSampleFormat mDesiredSampleFormat = AV_SAMPLE_FMT_NONE;

  uint8_t* mBufferOutput = nullptr;
  int mBufferOutputUsed = 0;
  int mBufferOutputAlloced = 0;

  int mChannels = 0;

  bool mFirstFrame = true;
  bool mGotFrame = false;
  bool mNoConcatenate = false;
  unsigned int mFrameSize = 0;
  double mPts = 0.0;
  double mDts = 0.0;
  };
//}}}
//{{{
class cOmxAudio {
public:
  //{{{
  enum EEncoded {
    ENCODED_NONE = 0,
    ENCODED_IEC61937_AC3,
    ENCODED_IEC61937_EAC3,
    ENCODED_IEC61937_DTS,
    ENCODED_IEC61937_MPEG,
    ENCODED_IEC61937_UNKNOWN,
    };
  //}}}
  ~cOmxAudio();

  static bool hwDecode (AVCodecID codec);

  int getSpace() { return mDecoder.getInputBufferSpace(); }
  int getChunkLen() { return mChunkLen; }
  float getDelay();
  float getCacheTotal();

  unsigned int getAudioRenderingLatency();
  float getMaxLevel (double &pts);
  uint64_t getChannelLayout (enum PCMLayout layout);
  float getVolume() { return mMute ? 0.f : mCurrentVolume; }
  bool isEOS();

  void setMute (bool mute);
  void setVolume (float volume);
  void setDynamicRangeCompression (float drc);
  void setCodingType (AVCodecID codec);

  bool init (cOmxClock* clock, const cOmxAudioConfig &config, uint64_t channelMap, unsigned int uiBitsPerSample);
  void buildChannelMap (enum PCMChannels* channelMap, uint64_t layout);
  int buildChannelMapCEA (enum PCMChannels* channelMap, uint64_t layout);
  void buildChannelMapOMX (enum OMX_AUDIO_CHANNELTYPE* channelMap, uint64_t layout);
  bool portChanged();
  int addPacket (void* data, int len, double dts, double pts, int frameSize);
  void process();
  void submitEOS();
  void flush();
  bool deInit();

private:
  bool canHwDecode (AVCodecID codec);

  bool applyVolume();
  void updateAttenuation();

  void printChannels (OMX_AUDIO_CHANNELTYPE eChannelMapping[]);
  void printPCM (OMX_AUDIO_PARAM_PCMMODETYPE* pcm, const std::string& direction);

  //{{{  vars
  std::recursive_mutex mMutex;

  cOmxAudioConfig mConfig;
  cAvUtil mAvUtil;

  cOmxClock* mAvClock = nullptr;
  cOmxCore* mClock = nullptr;

  cOmxCore mRenderAnal;
  cOmxCore mRenderHdmi;
  cOmxCore mSplitter;
  cOmxCore mMixer;
  cOmxCore mDecoder;

  cOmxTunnel mTunnelClockAnalog;
  cOmxTunnel mTunnelClockHdmi;
  cOmxTunnel mTunnelMixer;
  cOmxTunnel mTunnelDecoder;
  cOmxTunnel mTunnelSplitterAnalog;
  cOmxTunnel mTunnelSplitterHdmi;

  OMX_AUDIO_CODINGTYPE mEncoding = OMX_AUDIO_CodingPCM;
  OMX_AUDIO_CHANNELTYPE mInputChannels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE mOutputChannels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_PARAM_PCMMODETYPE mPcmInput;
  OMX_AUDIO_PARAM_PCMMODETYPE mPcmOutput;

  unsigned int mNumInputChannels = 0;
  unsigned int mNumOutputChannels = 0;
  unsigned int mBitsPerSample = 0;

  bool mInitialized = false;
  unsigned int mBytesPerSec = 0;
  unsigned int mInputBytesPerSec = 0;
  unsigned int mBufferLen = 0;
  unsigned int mChunkLen = 0;

  float mSubmitted = 0.f;
  bool mPortChanged = false;
  bool  mSetStartTime = false;
  double mLastPts = DVD_NOPTS_VALUE;
  bool mSubmittedEos = false;
  bool mFailedEos = false;

  float mCurrentVolume = 0.f;
  bool mMute = false;
  float mMaxLevel = 0.f;
  float mAttenuation = 1.f;
  float mDrc = 1.f;

  OMX_AUDIO_PARAM_DTSTYPE mDtsParam;
  WAVEFORMATEXTENSIBLE mWaveHeader;

  //{{{  struct amplitudes_t
  typedef struct {
    double pts;
    float level;
    } amplitudes_t;
  //}}}
  std::deque<amplitudes_t> mAmpQueue;
  float mDownmixMatrix[OMX_AUDIO_MAXCHANNELS*OMX_AUDIO_MAXCHANNELS];
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
      cOmxReader::freePacket (packet);
      }

    mPacketCacheSize = 0;
    mCurrentPts = DVD_NOPTS_VALUE;

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
    mCurrentPts = DVD_NOPTS_VALUE;
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

  virtual bool decode (OMXPacket* packet) = 0;
  virtual void flushDecoder() = 0;
  virtual void deleteDecoder() = 0;

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
//}}}
//{{{
class cOmxAudioPlayer : public cOmxPlayer {
public:
  cOmxAudioPlayer() : cOmxPlayer() {}
  virtual ~cOmxAudioPlayer() { close(); }

  bool isEOS() { return mPackets.empty() && mOmxAudio->isEOS(); }
  double getDelay() { return mOmxAudio->getDelay(); }
  double getCacheTotal() { return mOmxAudio->getCacheTotal(); }

  float getVolume() { return mCurrentVolume; }
  bool isPassThru (cOmxStreamInfo hints);

  //{{{
  void setVolume (float volume) {
    mCurrentVolume = volume;
    mOmxAudio->setVolume (volume);
    }
  //}}}
  //{{{
  void setMute (bool mute) {
    mMute = mute;
    mOmxAudio->setMute (mute);
    }
  //}}}
  //{{{
  void setDynamicRangeCompression (float drc) {
    mDrc = drc;
    mOmxAudio->setDynamicRangeCompression (drc);
    }
  //}}}

  bool open (cOmxClock* avClock, const cOmxAudioConfig& config);
  void submitEOS() { mOmxAudio->submitEOS(); }
  void reset() {}

private:
  bool openSwAudio();
  bool openOmxAudio();
  bool decode (OMXPacket* packet);
  //{{{
  void flushDecoder() {
    if (mSwAudio)
      mSwAudio->reset();
    mOmxAudio->flush();
    }
  //}}}
  //{{{
  void deleteDecoder() {
    delete mSwAudio;
    mSwAudio = nullptr;
    delete mOmxAudio;
    mOmxAudio = nullptr;
    }
  //}}}

  //{{{  vars
  cOmxAudioConfig mConfig;
  cOmxAudio* mOmxAudio = nullptr;
  cSwAudio* mSwAudio = nullptr;
  cOmxReader* mOmxReader = nullptr;
  cOmxStreamInfo mHints;

  std::string mDevice;
  bool mPassThru;
  bool mHwDecode;
  bool mBoostOnDownmix;

  float mCurrentVolume = 0.f;
  float mDrc = 0.f;
  bool mMute = false;
  //}}}
  };
//}}}
//{{{
class cOmxVideoPlayer : public cOmxPlayer {
public:
  cOmxVideoPlayer() : cOmxPlayer() {}
  virtual ~cOmxVideoPlayer() { close(); }

  bool isEOS() { return mPackets.empty() && mDecoder->isEOS(); }
  double getDelay() { return mVideoDelay; }
  double getFPS() { return mFps; };

  void setDelay (double delay) { mVideoDelay = delay; }
  void setAlpha (int alpha) { mDecoder->setAlpha (alpha); }
  void setVideoRect (int aspectMode) { mDecoder->setVideoRect (aspectMode); }
  void setVideoRect (const cRect& SrcRect, const cRect& DestRect) { mDecoder->setVideoRect (SrcRect, DestRect); }

  bool open (cOmxClock* avClock, const cOmxVideoConfig& config);
  void submitEOS() { mDecoder->submitEOS(); }
  void reset();

private:
  bool decode (OMXPacket* packet);
  void flushDecoder() { mDecoder->reset(); }
  void deleteDecoder() { delete mDecoder; mDecoder = nullptr; }

  //{{{  vars
  cOmxVideoConfig mConfig;
  cOmxVideo* mDecoder = nullptr;

  double mVideoDelay = 0.0;
  float mFps = 25.f;
  double mFrametime = 0.0;
  float mDisplayAspect = false;
  //}}}
  };
//}}}
