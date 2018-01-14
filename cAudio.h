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
class cOmxAudioConfig {
public:
  cOmxStreamInfo mHints;
  std::string mDevice;
  std::string mSubdevice;

  enum PCMLayout mLayout = PCM_LAYOUT_2_0;
  bool mBoostOnDownmix = true;
  bool mPassthrough = false;
  bool mHwDecode = false;
  bool mIsLive = false;

  float mQueueSize = 3.f;
  float mFfoSize = 2.f;
  };
//}}}

//{{{
class cSwAudio {
public:
  ~cSwAudio();

  uint64_t getChannelMap();
  unsigned int getFrameSize() { return mFrameSize; }
  int getData (unsigned char** dst, double &dts, double &pts);
  int getChannels() { return mCodecContext->channels; }
  int getSampleRate() { return mCodecContext->sample_rate; }
  int getBitRate() { return mCodecContext->bit_rate; }
  int getBitsPerSample() { return mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32; }

  bool open (cOmxStreamInfo &hints, enum PCMLayout layout);
  int decode (unsigned char* pData, int iSize, double dts, double pts);
  void reset();
  void dispose();

protected:
  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cSwResample mSwResample;

  SwrContext* mConvert = nullptr;
  AVCodecContext* mCodecContext = nullptr;
  AVFrame* mFrame1 = nullptr;

  enum AVSampleFormat mISampleFormat = AV_SAMPLE_FMT_NONE;
  enum AVSampleFormat mDesiredSampleFormat = AV_SAMPLE_FMT_NONE;

  unsigned char* mBufferOutput = nullptr;
  int mIBufferOutputUsed = 0;
  int mIBufferOutputAlloced = 0;

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

  unsigned int getSpace() { return mOmxDecoder.GetInputBufferSpace(); }
  unsigned int getChunkLen() { return mChunkLen; }
  float getDelay();
  float getCacheTime() { return getDelay(); }
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

  bool initialize (cOmxClock *clock, const cOmxAudioConfig &config, uint64_t channelMap, unsigned int uiBitsPerSample);
  void buildChannelMap (enum PCMChannels *channelMap, uint64_t layout);
  int buildChannelMapCEA (enum PCMChannels *channelMap, uint64_t layout);
  void buildChannelMapOMX (enum OMX_AUDIO_CHANNELTYPE *channelMap, uint64_t layout);
  bool portSettingsChanged();
  unsigned int addPackets (const void* data, unsigned int len, double dts, double pts, unsigned int frame_size);
  void process();
  void submitEOS();
  void flush();
  bool deinitialize();

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
  cOmxCoreComponent* mOmxClock = nullptr;

  cOmxCoreComponent mOmxRenderAnalog;
  cOmxCoreComponent mOmxRenderHdmi;
  cOmxCoreComponent mOmxSplitter;
  cOmxCoreComponent mOmxMixer;
  cOmxCoreComponent mOmxDecoder;

  cOmxCoreTunnel mOmxTunnelClockAnalog;
  cOmxCoreTunnel mOmxTunnelClockHdmi;
  cOmxCoreTunnel mOmxTunnelMixer;
  cOmxCoreTunnel mOmxTunnelDecoder;
  cOmxCoreTunnel mOmxTunnelSplitterAnalog;
  cOmxCoreTunnel mOmxTunnelSplitterHdmi;

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
  bool mSettingsChanged = false;
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

class cOmxPlayerAudio {
public:
  cOmxPlayerAudio();
  ~cOmxPlayerAudio();

  double getDelay() { return mOmxAudio->getDelay(); }
  double getCacheTime() { return mOmxAudio->getCacheTime(); }
  double getCacheTotal() { return  mOmxAudio->getCacheTotal(); }
  double getCurrentPTS() { return mCurrentPts; };
  unsigned int getCached() { return mCachedSize; };
  unsigned int getMaxCached() { return mConfig.mQueueSize * 1024 * 1024; };
  //{{{
  unsigned int getLevel() {
    return mConfig.mQueueSize ? (100.f * mCachedSize / (mConfig.mQueueSize * 1024.f * 1024.f)) : 0;
    };
  //}}}
  float getVolume() { return mCurrentVolume; }
  bool isPassthrough (cOmxStreamInfo hints);
  bool isEOS() { return mPackets.empty() && mOmxAudio->isEOS(); }

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

  bool open (cOmxClock* avClock, const cOmxAudioConfig& config, cOmxReader* omxReader);
  void run();
  bool addPacket (OMXPacket* packet);
  void submitEOS();
  void flush();
  bool close();

private:
  void lock() { pthread_mutex_lock (&mLock); }
  void unLock() { pthread_mutex_unlock (&mLock); }
  void lockDecoder() { pthread_mutex_lock (&mLockDecoder); }
  void unLockDecoder() { pthread_mutex_unlock (&mLockDecoder); }

  bool openSwAudio();
  bool openOmxAudio();
  bool decode (OMXPacket *packet);

  //{{{  vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t mPacketCond;
  pthread_cond_t mAudioCond;

  cOmxClock* mAvClock = nullptr;
  cOmxReader* mOmxReader = nullptr;
  cOmxStreamInfo mHints;
  cOmxAudioConfig mConfig;
  cOmxAudio* mOmxAudio = nullptr;
  cSwAudio* mSwAudio = nullptr;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  bool mAbort;
  bool mFlush = false;
  std::atomic<bool> mFlushRequested;
  unsigned int mCachedSize = 0;
  std::deque<OMXPacket*> mPackets;

  AVStream* mStream = nullptr;
  int mStreamId = -1;

  double mCurrentPts;

  std::string mDevice;
  bool mPassthrough;
  bool mHwDecode;
  bool mBoostOnDownmix;

  float mCurrentVolume = 0.f;
  float mDrc = 0.f;
  bool mMute = false;
  //}}}
  };
