#pragma once
//{{{  includes
#include <deque>
#include <string>
#include <atomic>
#include <sys/types.h>

#include "PlatformDefs.h"
#include "cSingleLock.h"
#include "cPcmRemap.h"

#include "avLibs.h"

#include "cOmxThread.h"
#include "cOmxCoreComponent.h"
#include "cOmxCoreTunnel.h"
#include "cOmxClock.h"
#include "cOmxReader.h"
#include "cOmxStreamInfo.h"
//}}}
using namespace std;
#define AUDIO_BUFFER_SECONDS 3

//{{{
class cOmxAudioConfig {
public:
  cOmxAudioConfig() {
    layout = PCM_LAYOUT_2_0;
    boostOnDownmix = true;
    passthrough = false;
    hwdecode = false;
    is_live = false;
    queue_size = 3.0f;
    fifo_size = 2.0f;
    }

  cOmxStreamInfo hints;
  CStdString device;
  CStdString subdevice;

  enum PCMLayout layout;
  bool boostOnDownmix;
  bool passthrough;
  bool hwdecode;
  bool is_live;
  float queue_size;
  float fifo_size;
  };
//}}}
//{{{
class cSwAudio {
public:
  cSwAudio();
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

  AVCodecContext* m_pCodecContext;
  SwrContext* m_pConvert;

  enum AVSampleFormat m_iSampleFormat;
  enum AVSampleFormat m_desiredSampleFormat;
  AVFrame* m_pFrame1;

  BYTE* m_pBufferOutput;
  int m_iBufferOutputUsed;
  int m_iBufferOutputAlloced;

  bool m_bOpenedCodec;
  int m_channels;

  bool m_bFirstFrame;
  bool m_bGotFrame;
  bool m_bNoConcatenate;
  unsigned int m_frameSize;
  double m_dts;
  double m_pts;
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
  cOmxAudio();
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
  cCriticalSection m_critSection;

  cOmxCoreComponent m_omx_render_analog;
  cOmxCoreComponent m_omx_render_hdmi;
  cOmxCoreComponent m_omx_splitter;
  cOmxCoreComponent m_omx_mixer;
  cOmxCoreComponent m_omx_decoder;

  cOmxCoreTunnel    m_omx_tunnel_clock_analog;
  cOmxCoreTunnel    m_omx_tunnel_clock_hdmi;
  cOmxCoreTunnel    m_omx_tunnel_mixer;
  cOmxCoreTunnel    m_omx_tunnel_decoder;
  cOmxCoreTunnel    m_omx_tunnel_splitter_analog;
  cOmxCoreTunnel    m_omx_tunnel_splitter_hdmi;
  cAvUtil           mAvUtil;

private:
  bool CanHWDecode (AVCodecID codec);

  bool ApplyVolume();
  void UpdateAttenuation();

  void PrintChannels (OMX_AUDIO_CHANNELTYPE eChannelMapping[]);
  void PrintPCM (OMX_AUDIO_PARAM_PCMMODETYPE *pcm, std::string direction);

  bool          m_Initialized;
  float         m_CurrentVolume;
  bool          m_Mute;
  long          m_drc;
  bool          m_Passthrough;
  unsigned int  m_BytesPerSec;
  unsigned int  m_InputBytesPerSec;
  unsigned int  m_BufferLen;
  unsigned int  m_ChunkLen;
  unsigned int  m_InputChannels;
  unsigned int  m_OutputChannels;
  unsigned int  m_BitsPerSample;
  float         m_maxLevel;
  float         m_amplification;
  float         m_attenuation;
  float         m_submitted;

  cOmxCoreComponent* m_omx_clock;
  cOmxClock*    m_av_clock;
  bool          m_settings_changed;
  bool          m_setStartTime;

  OMX_AUDIO_CODINGTYPE m_eEncoding;
  double        m_last_pts;
  bool          m_submitted_eos;
  bool          m_failed_eos;

  cOmxAudioConfig m_config;

  OMX_AUDIO_CHANNELTYPE m_input_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE m_output_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_DTSTYPE     m_dtsParam;
  WAVEFORMATEXTENSIBLE        m_wave_header;

  //{{{
  typedef struct {
    double pts;
    float level;
  } amplitudes_t;
  //}}}
  std::deque<amplitudes_t> m_ampqueue;
  float m_downmix_matrix[OMX_AUDIO_MAXCHANNELS*OMX_AUDIO_MAXCHANNELS];
  };
//}}}

//{{{
class cOmxPlayerAudio : public cOmxThread {
public:
  cOmxPlayerAudio();
  ~cOmxPlayerAudio();

  bool Open (cOmxClock* av_clock, const cOmxAudioConfig& config, cOmxReader* omx_reader);

  double GetDelay();
  double GetCacheTime();
  double GetCacheTotal();
  double GetCurrentPTS() { return m_iCurrentPts; };
  unsigned int GetCached() { return m_cached_size; };
  //{{{
  unsigned int GetMaxCached() {
    return m_config.queue_size * 1024 * 1024;
    };
  //}}}
  //{{{
  unsigned int GetLevel() {
    return m_config.queue_size ? 100.0f * m_cached_size / (m_config.queue_size * 1024.0f * 1024.0f) : 0;
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

  void SubmitEOS();
  bool IsEOS();

private:
  void Lock() { pthread_mutex_lock (&m_lock); }
  void UnLock() { pthread_mutex_unlock(&m_lock); }

  void LockDecoder() { pthread_mutex_lock (&m_lock_decoder); }
  void UnLockDecoder() { pthread_mutex_unlock (&m_lock_decoder); }

  bool Close();

  bool OpenDecoder();
  void CloseDecoder();

  bool OpenAudioCodec();
  void CloseAudioCodec();

  bool Decode (OMXPacket *pkt);

  //{{{  vars
  pthread_cond_t         m_packet_cond;
  pthread_cond_t         m_audio_cond;

  pthread_mutex_t        m_lock;
  pthread_mutex_t        m_lock_decoder;

  cAvUtil                mAvUtil;
  cAvCodec               mAvCodec;
  cAvFormat              mAvFormat;

  cOmxClock*             m_av_clock;
  cOmxReader*            m_omx_reader;
  cOmxStreamInfo         m_hints;
  cOmxAudioConfig        m_config;
  cOmxAudio*             m_decoder;
  cSwAudio*              m_pAudioCodec;

  AVStream*              m_pStream;
  int                    m_stream_id;
  std::deque<OMXPacket*> m_packets;

  bool                   m_open;
  double                 m_iCurrentPts;

  std::string            m_codec_name;
  std::string            m_device;
  bool                   m_passthrough;
  bool                   m_hw_decode;
  bool                   m_boost_on_downmix;

  bool                   m_bAbort;
  bool                   m_flush;
  std::atomic<bool>      m_flush_requested;
  unsigned int           m_cached_size;

  float                  m_CurrentVolume;
  long                   m_amplification;
  bool                   m_mute;
  bool                   m_player_error;
  //}}}
  };
//}}}
