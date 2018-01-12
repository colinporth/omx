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
  cOmxStreamInfo hints;
  std::string device;
  std::string subdevice;

  enum PCMLayout layout = PCM_LAYOUT_2_0;
  bool boostOnDownmix = true;
  bool passthrough = false;
  bool hwdecode = false;
  bool is_live = false;
  float queue_size = 3.f;
  float fifo_size = 2.f;
  };
//}}}

//{{{
class cSwAudio {
public:
  ~cSwAudio();

  uint64_t getChannelMap();
  unsigned int getFrameSize() { return m_frameSize; }
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

  enum AVSampleFormat m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  enum AVSampleFormat m_desiredSampleFormat = AV_SAMPLE_FMT_NONE;

  unsigned char* mBufferOutput = nullptr;
  int m_iBufferOutputUsed = 0;
  int m_iBufferOutputAlloced = 0;

  int m_channels = 0;

  bool mFirstFrame = true;
  bool mGotFrame = false;
  bool mNoConcatenate = false;
  unsigned int m_frameSize = 0;
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
  unsigned int getSpace() { return m_omx_decoder.GetInputBufferSpace(); }
  unsigned int getChunkLen() { return m_ChunkLen; }
  float getDelay();
  float getCacheTime() { return getDelay(); }
  float getCacheTotal();
  unsigned int getAudioRenderingLatency();
  float getMaxLevel (double &pts);
  uint64_t getChannelLayout (enum PCMLayout layout);
  float getVolume() { return mMute ? 0.f : m_CurrentVolume; }
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
  void printPCM (OMX_AUDIO_PARAM_PCMMODETYPE *pcm, const std::string& direction);

  //{{{  vars
  std::recursive_mutex mMutex;

  cAvUtil mAvUtil;
  cOmxAudioConfig mConfig;
  cOmxClock* mAvClock = nullptr;
  cOmxCoreComponent* m_omx_clock = nullptr;

  cOmxCoreComponent m_omx_render_analog;
  cOmxCoreComponent m_omx_render_hdmi;
  cOmxCoreComponent m_omx_splitter;
  cOmxCoreComponent m_omx_mixer;
  cOmxCoreComponent m_omx_decoder;

  cOmxCoreTunnel m_omx_tunnel_clock_analog;
  cOmxCoreTunnel m_omx_tunnel_clock_hdmi;
  cOmxCoreTunnel m_omx_tunnel_mixer;
  cOmxCoreTunnel m_omx_tunnel_decoder;
  cOmxCoreTunnel m_omx_tunnel_splitter_analog;
  cOmxCoreTunnel m_omx_tunnel_splitter_hdmi;

  OMX_AUDIO_CODINGTYPE m_eEncoding = OMX_AUDIO_CodingPCM;
  OMX_AUDIO_CHANNELTYPE m_input_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE m_output_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;

  bool m_Initialized = false;
  unsigned int m_BytesPerSec = 0;
  unsigned int m_InputBytesPerSec = 0;
  unsigned int m_BufferLen = 0;
  unsigned int m_ChunkLen = 0;
  unsigned int m_InputChannels = 0;
  unsigned int m_OutputChannels = 0;
  unsigned int m_BitsPerSample = 0;

  float m_CurrentVolume = 0.f;
  bool mMute = false;
  float m_maxLevel = 0.f;
  float m_attenuation = 1.f;
  float mDrc = 1.f;

  float m_submitted = 0.f;
  bool mSettingsChanged = false;
  bool  m_setStartTime = false;
  double m_last_pts = DVD_NOPTS_VALUE;
  bool m_submitted_eos = false;
  bool m_failed_eos = false;

  OMX_AUDIO_PARAM_DTSTYPE m_dtsParam;
  WAVEFORMATEXTENSIBLE m_wave_header;

  //{{{  struct amplitudes_t
  typedef struct {
    double pts;
    float level;
    } amplitudes_t;
  //}}}
  std::deque<amplitudes_t> m_ampqueue;
  float m_downmix_matrix[OMX_AUDIO_MAXCHANNELS*OMX_AUDIO_MAXCHANNELS];
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
  double getCurrentPTS() { return m_iCurrentPts; };
  unsigned int getCached() { return mCachedSize; };
  unsigned int getMaxCached() { return mConfig.queue_size * 1024 * 1024; };
  //{{{
  unsigned int getLevel() {
    return mConfig.queue_size ? (100.f * mCachedSize / (mConfig.queue_size * 1024.f * 1024.f)) : 0;
    };
  //}}}
  float getVolume() { return m_CurrentVolume; }
  bool getError() { return !mPlayerError; };
  bool isPassthrough (cOmxStreamInfo hints);
  bool isEOS() { return mPackets.empty() && mOmxAudio->isEOS(); }

  //{{{
  void setVolume (float volume) {
    m_CurrentVolume = volume;
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

  bool open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader);
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

  void closeSwAudio();
  void closeOmxAudio();

  //{{{  vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t  m_packet_cond;
  pthread_cond_t  m_audio_cond;

  bool            mAbort;
  bool            mFlush = false;
  std::atomic<bool> mFlush_requested;
  bool            mPlayerError = false;

  cOmxClock*      mAvClock = nullptr;
  cOmxReader*     m_omx_reader = nullptr;
  cOmxStreamInfo  m_hints;
  cOmxAudioConfig mConfig;
  cOmxAudio*      mOmxAudio = nullptr;
  cSwAudio*       mSwAudio = nullptr;

  cAvUtil         mAvUtil;
  cAvCodec        mAvCodec;
  cAvFormat       mAvFormat;

  unsigned int    mCachedSize = 0;
  std::deque<OMXPacket*> mPackets;

  AVStream*       mStream = nullptr;
  int             m_stream_id = -1;

  double          m_iCurrentPts;

  std::string     m_codec_name;
  std::string     m_device;
  bool            mPassthrough;
  bool            mHwDecode;
  bool            m_boost_on_downmix;

  float           m_CurrentVolume = 0.f;
  float           mDrc = 0.f;
  bool            mMute = false;
  //}}}
  };
