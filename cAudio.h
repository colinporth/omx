#pragma once
//{{{  includes
#include <deque>
#include <string>
#include <atomic>
#include <sys/types.h>

#include "platformDefs.h"
#include "cSingleLock.h"
#include "cPcmRemap.h"

#include "avLibs.h"

#include "cOmxThread.h"
#include "cOmxCoreComponent.h"
#include "cOmxCoreTunnel.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"

using namespace std;
//}}}
#define AUDIO_BUFFER_SECONDS 3

//{{{
class cOmxAudioConfig {
public:
  cOmxAudioConfig() {}

  cOmxStreamInfo hints;
  string device;
  string subdevice;

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

  bool Open (cOmxStreamInfo &hints, enum PCMLayout layout);
  void Dispose();

  int Decode (BYTE* pData, int iSize, double dts, double pts);
  int GetData (BYTE** dst, double &dts, double &pts);
  void Reset();

  int GetChannels();
  uint64_t GetChannelMap();
  int GetSampleRate();
  int GetBitsPerSample();
  int GetBitRate();
  unsigned int GetFrameSize() { return m_frameSize; }

  static const char* GetName() { return "FFmpeg"; }

protected:
  cAvCodec mAvCodec;
  cAvUtil mAvUtil;
  cSwResample mSwResample;

  AVCodecContext* m_pCodecContext = NULL;
  SwrContext* m_pConvert = NULL;

  enum AVSampleFormat m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  enum AVSampleFormat m_desiredSampleFormat = AV_SAMPLE_FMT_NONE;
  AVFrame* m_pFrame1 = NULL;

  BYTE* m_pBufferOutput = NULL;
  int m_iBufferOutputUsed = 0;
  int m_iBufferOutputAlloced = 0;

  bool m_bOpenedCodec = false;
  int m_channels = 0;

  bool m_bFirstFrame = true;
  bool m_bGotFrame = false;
  bool m_bNoConcatenate = false;
  unsigned int m_frameSize = 0;
  double m_dts = 0.0;
  double m_pts = 0.0;
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

  bool Initialize (cOmxClock *clock, const cOmxAudioConfig &config, uint64_t channelMap, unsigned int uiBitsPerSample);
  bool Deinitialize();
  bool PortSettingsChanged();

  unsigned int GetSpace();
  unsigned int GetChunkLen();
  float GetDelay();
  float GetCacheTime();
  float GetCacheTotal();
  unsigned int GetAudioRenderingLatency();
  float GetMaxLevel (double &pts);
  uint64_t GetChannelLayout (enum PCMLayout layout);
  float GetVolume();

  void SetMute (bool bOnOff);
  void SetVolume (float nVolume);
  void SetCodingType (AVCodecID codec);
  void SetDynamicRangeCompression (long drc);
  bool SetClock (cOmxClock *clock);

  unsigned int AddPackets (const void* data, unsigned int len, double dts, double pts, unsigned int frame_size);
  unsigned int AddPackets (const void* data, unsigned int len);
  void Process();
  void Flush();

  bool IsEOS();
  void SubmitEOS();

  static bool HWDecode (AVCodecID codec);

  void BuildChannelMap (enum PCMChannels *channelMap, uint64_t layout);
  int BuildChannelMapCEA (enum PCMChannels *channelMap, uint64_t layout);
  void BuildChannelMapOMX (enum OMX_AUDIO_CHANNELTYPE *channelMap, uint64_t layout);

protected:
  //{{{  vars
  cCriticalSection mCrtiticalSection;

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

  cAvUtil mAvUtil;
  //}}}

private:
  bool CanHWDecode (AVCodecID codec);

  bool ApplyVolume();
  void UpdateAttenuation();

  void PrintChannels (OMX_AUDIO_CHANNELTYPE eChannelMapping[]);
  void PrintPCM (OMX_AUDIO_PARAM_PCMMODETYPE *pcm, const std::string& direction);

  //{{{  vars
  bool          m_Initialized = false;
  float         m_CurrentVolume = 0.f;
  bool          m_Mute = false;
  long          m_drc = 0;
  bool          m_Passthrough = false;
  unsigned int  m_BytesPerSec = 0;
  unsigned int  m_InputBytesPerSec = 0;
  unsigned int  m_BufferLen = 0;
  unsigned int  m_ChunkLen = 0;
  unsigned int  m_InputChannels = 0;
  unsigned int  m_OutputChannels = 0;
  unsigned int  m_BitsPerSample = 0;
  float         m_maxLevel = 0.f;
  float         m_amplification = 1.f;
  float         m_attenuation = 1.f;
  float         m_submitted = 0.f;

  cOmxCoreComponent* m_omx_clock = nullptr;
  cOmxClock*    m_av_clock = nullptr;
  bool          m_settings_changed = false;
  bool          m_setStartTime = false;

  OMX_AUDIO_CODINGTYPE m_eEncoding = OMX_AUDIO_CodingPCM;
  double        m_last_pts = DVD_NOPTS_VALUE;
  bool          m_submitted_eos = false;
  bool          m_failed_eos = false;

  cOmxAudioConfig m_config;

  OMX_AUDIO_CHANNELTYPE m_input_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE m_output_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_DTSTYPE m_dtsParam;
  WAVEFORMATEXTENSIBLE m_wave_header;

  //{{{
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

class cOmxPlayerAudio : public cOmxThread {
public:
  cOmxPlayerAudio();
  ~cOmxPlayerAudio();

  bool Open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader);

  double GetDelay();
  double GetCacheTime();
  double GetCacheTotal();
  double GetCurrentPTS() { return m_iCurrentPts; };
  //{{{
  unsigned int GetLevel() {
    return m_config.queue_size ? (100.f * m_cached_size / (m_config.queue_size * 1024.f * 1024.f)) : 0;
    };
  //}}}
  unsigned int GetCached() { return m_cached_size; };
  //{{{
  unsigned int GetMaxCached() {
    return m_config.queue_size * 1024 * 1024;
    };
  //}}}
  float GetVolume() { return m_CurrentVolume; }
  bool IsPassthrough (cOmxStreamInfo hints);
  bool Error() { return !m_player_error; };

  //{{{
  void SetVolume (float fVolume) {
    m_CurrentVolume = fVolume;
    if (m_decoder)
      m_decoder->SetVolume(fVolume);
    }
  //}}}
  //{{{
  void SetMute (bool bOnOff) {
    m_mute = bOnOff;
    if (m_decoder)
      m_decoder->SetMute(bOnOff);
      }
  //}}}
  //{{{
  void SetDynamicRangeCompression (long drc) {
    m_amplification = drc;
    if (m_decoder)
      m_decoder->SetDynamicRangeCompression(drc);
    }
  //}}}

  bool AddPacket (OMXPacket *pkt);
  void Process();
  void Flush();

  bool IsEOS();
  void SubmitEOS();

private:
  bool Close();

  void Lock() { pthread_mutex_lock (&mLock); }
  void UnLock() { pthread_mutex_unlock (&mLock); }
  void LockDecoder() { pthread_mutex_lock (&mLockDecoder); }
  void UnLockDecoder() { pthread_mutex_unlock (&mLockDecoder); }

  bool OpenDecoder();
  void CloseDecoder();
  bool OpenAudioCodec();
  void CloseAudioCodec();

  bool Decode (OMXPacket *pkt);

  //{{{  vars
  pthread_mutex_t mLock;
  pthread_mutex_t mLockDecoder;
  pthread_cond_t  m_packet_cond;
  pthread_cond_t  m_audio_cond;

  bool            m_open = false;
  bool            m_bAbort;
  bool            m_flush = false;
  std::atomic<bool> m_flush_requested;
  bool            m_player_error = false;

  cOmxClock*      m_av_clock = nullptr;
  cOmxReader*     m_omx_reader = nullptr;
  cOmxStreamInfo  m_hints;
  cOmxAudioConfig m_config;
  cOmxAudio*      m_decoder = nullptr;
  cSwAudio*       m_pAudioCodec = nullptr;

  cAvUtil         mAvUtil;
  cAvCodec        mAvCodec;
  cAvFormat       mAvFormat;

  unsigned int    m_cached_size = 0;
  std::deque<OMXPacket*> m_packets;

  AVStream*       m_pStream = nullptr;
  int             m_stream_id = -1;

  double          m_iCurrentPts;

  std::string     m_codec_name;
  std::string     m_device;
  bool            m_passthrough;
  bool            m_hw_decode;
  bool            m_boost_on_downmix;

  float           m_CurrentVolume = 0.f;
  long            m_amplification = 0;
  bool            m_mute = false;
  //}}}
  };
