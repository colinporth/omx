//{{{  includes
#include "cAudio.h"

#include <algorithm>
#include "cLog.h"

#ifndef VOLUME_MINIMUM
  #define VOLUME_MINIMUM 0
#endif

using namespace std;
//}}}

#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};
//{{{
static unsigned count_bits (uint64_t value) {
  unsigned bits = 0;
  for (; value; ++bits)
    value &= value - 1;
  return bits;
  }
//}}}

//{{{
cOmxAudio::cOmxAudio() :
  m_Initialized     (false  ),
  m_CurrentVolume   (0      ),
  m_Mute            (false  ),
  m_drc             (0      ),
  m_BytesPerSec     (0      ),
  m_InputBytesPerSec(0      ),
  m_BufferLen       (0      ),
  m_ChunkLen        (0      ),
  m_InputChannels   (0      ),
  m_OutputChannels  (0      ),
  m_BitsPerSample   (0      ),
  m_maxLevel        (0.0f   ),
  m_amplification   (1.0f   ),
  m_attenuation     (1.0f   ),
  m_submitted       (0.0f   ),
  m_omx_clock       (NULL   ),
  m_av_clock        (NULL   ),
  m_settings_changed(false  ),
  m_setStartTime    (false  ),
  m_eEncoding       (OMX_AUDIO_CodingPCM),
  m_last_pts        (DVD_NOPTS_VALUE),
  m_submitted_eos   (false  ),
  m_failed_eos      (false  )
{
}
//}}}
//{{{
cOmxAudio::~cOmxAudio()
{
  Deinitialize();
}
//}}}

//{{{
bool cOmxAudio::PortSettingsChanged() {

  cSingleLock lock (m_critSection);

  if (m_settings_changed) {
    m_omx_decoder.DisablePort (m_omx_decoder.GetOutputPort(), true);
    m_omx_decoder.EnablePort (m_omx_decoder.GetOutputPort(), true);
    return true;
    }

  if(!m_config.passthrough) {
    if (!m_omx_mixer.Initialize ("OMX.broadcom.audio_mixer", OMX_IndexParamAudioInit))
      return false;
    }
  if(m_config.device == "omx:both") {
    if (!m_omx_splitter.Initialize ("OMX.broadcom.audio_splitter", OMX_IndexParamAudioInit))
      return false;
    }
  if (m_config.device == "omx:both" || m_config.device == "omx:local") {
    if (!m_omx_render_analog.Initialize ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
    }
  if (m_config.device == "omx:both" || m_config.device == "omx:hdmi") {
    if (!m_omx_render_hdmi.Initialize ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
    }
  if (m_config.device == "omx:alsa") {
    if (!m_omx_render_analog.Initialize ("OMX.alsa.audio_render", OMX_IndexParamAudioInit))
      return false;
    }

  UpdateAttenuation();

  if (m_omx_mixer.IsInitialized()) {
    /* setup mixer output */
    OMX_INIT_STRUCTURE(m_pcm_output);
    m_pcm_output.nPortIndex = m_omx_decoder.GetOutputPort();
    if (m_omx_decoder.GetParameter(OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged  GetParameter");
      return false;
      }

    memcpy(m_pcm_output.eChannelMapping, m_output_channels, sizeof(m_output_channels));
    // round up to power of 2
    m_pcm_output.nChannels = m_OutputChannels > 4 ? 8 : m_OutputChannels > 2 ? 4 : m_OutputChannels;
    /* limit samplerate (through resampling) if requested */
    m_pcm_output.nSamplingRate = std::min (std::max ((int)m_pcm_output.nSamplingRate, 8000), 192000);

    m_pcm_output.nPortIndex = m_omx_mixer.GetOutputPort();
    if (m_omx_mixer.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged SetParameter");
      return false;
      }

    cLog::Log (LOGDEBUG, "cOmxAudio::PortSettingsChanged Output bps %d samplerate %d channels %d buffer size %d bytes per second %d",
               (int)m_pcm_output.nBitPerSample, (int)m_pcm_output.nSamplingRate,
               (int)m_pcm_output.nChannels, m_BufferLen, m_BytesPerSec);
    PrintPCM (&m_pcm_output, std::string ("output"));

    if (m_omx_splitter.IsInitialized() ) {
      m_pcm_output.nPortIndex = m_omx_splitter.GetInputPort();
      if (m_omx_splitter.SetParameter (OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_splitter SetParameter");
        return false;
        }

      m_pcm_output.nPortIndex = m_omx_splitter.GetOutputPort();
      if (m_omx_splitter.SetParameter (OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_splitter SetParameter");
        return false;
        }
      m_pcm_output.nPortIndex = m_omx_splitter.GetOutputPort() + 1;
      if (m_omx_splitter.SetParameter (OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_splitter SetParameter");
        return false;
        }
      }

    if (m_omx_render_analog.IsInitialized() ) {
      m_pcm_output.nPortIndex = m_omx_render_analog.GetInputPort();
      if (m_omx_render_analog.SetParameter (OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_analog SetParameter");
        return false;
        }
      }

    if (m_omx_render_hdmi.IsInitialized() ) {
      m_pcm_output.nPortIndex = m_omx_render_hdmi.GetInputPort();
      if (m_omx_render_hdmi.SetParameter (OMX_IndexParamAudioPcm, &m_pcm_output) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_hdmi SetParameter");
        return false;
        }
      }
    }

  if (m_omx_render_analog.IsInitialized()) {
    m_omx_tunnel_clock_analog.Initialize (m_omx_clock, m_omx_clock->GetInputPort(),
                                          &m_omx_render_analog, m_omx_render_analog.GetInputPort()+1);

    if (m_omx_tunnel_clock_analog.Establish() != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_tunnel_clock_analog.Establish");
      return false;
      }
    m_omx_render_analog.ResetEos();
    }

  if (m_omx_render_hdmi.IsInitialized() ) {
    m_omx_tunnel_clock_hdmi.Initialize(m_omx_clock, m_omx_clock->GetInputPort() + (m_omx_render_analog.IsInitialized() ? 2 : 0),
      &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort()+1);

    if (m_omx_tunnel_clock_hdmi.Establish() != OMX_ErrorNone) {
      cLog::Log(LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_tunnel_clock_hdmi.Establish");
      return false;
      }
    m_omx_render_hdmi.ResetEos();
    }

  if (m_omx_render_analog.IsInitialized() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps, it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches, but can affect video/display sync
    // when in dual audio mode, make analogue the slave
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = m_config.is_live || m_config.device == "omx:both" ? OMX_FALSE : OMX_TRUE;
    if (m_omx_render_analog.SetConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char *)audioDest.sName, m_config.device == "omx:alsa" ? m_config.subdevice.c_str() : "local", sizeof(audioDest.sName));
    if (m_omx_render_analog.SetConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      cLog::Log(LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_analog.SetConfig");
      return false;
      }
    }

  if (m_omx_render_hdmi.IsInitialized() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps, it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches, but can affect video/display sync
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = m_config.is_live ? OMX_FALSE:OMX_TRUE;
    if (m_omx_render_hdmi.SetConfig(OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char *)audioDest.sName, "hdmi", strlen("hdmi"));
    if (m_omx_render_hdmi.SetConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_hdmi.SetConfig");
      return false;
      }
    }

  if (m_omx_splitter.IsInitialized() ) {
    m_omx_tunnel_splitter_analog.Initialize(&m_omx_splitter, m_omx_splitter.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
    if (m_omx_tunnel_splitter_analog.Establish() != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged  m_omx_tunnel_splitter_analog.Establish");
      return false;
      }

    m_omx_tunnel_splitter_hdmi.Initialize(&m_omx_splitter, m_omx_splitter.GetOutputPort() + 1, &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
    if (m_omx_tunnel_splitter_hdmi.Establish() != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_tunnel_splitter_hdmi.Establish");
      return false;
      }
    }

  if (m_omx_mixer.IsInitialized()) {
    m_omx_tunnel_decoder.Initialize (&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_mixer, m_omx_mixer.GetInputPort());
    if (m_omx_splitter.IsInitialized())
      m_omx_tunnel_mixer.Initialize (&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_splitter, m_omx_splitter.GetInputPort());
    else {
      if (m_omx_render_analog.IsInitialized())
        m_omx_tunnel_mixer.Initialize(&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
      if (m_omx_render_hdmi.IsInitialized())
        m_omx_tunnel_mixer.Initialize(&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
      }
    cLog::Log (LOGDEBUG, "cOmxAudio::PortSettingsChanged bits:%d mode:%d channels:%d srate:%d nopassthrough",
               (int)m_pcm_input.nBitPerSample, m_pcm_input.ePCMMode,
               (int)m_pcm_input.nChannels, (int)m_pcm_input.nSamplingRate);
  }
  else {
    if (m_omx_render_analog.IsInitialized())
      m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
    else if (m_omx_render_hdmi.IsInitialized())
      m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
     cLog::Log (LOGDEBUG, "cOmxAudio::PortSettingsChanged bits:%d mode:%d ch:%d srate:%d passthrough", 0, 0, 0, 0);
     }

  if (m_omx_tunnel_decoder.Establish() != OMX_ErrorNone) {
    cLog::Log(LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_tunnel_decoder.Establish");
    return false;
    }

  if (m_omx_mixer.IsInitialized()) {
    if (m_omx_mixer.SetStateForComponent(OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_mixer OMX_StateExecuting");
      return false;
      }
    }

  if (m_omx_mixer.IsInitialized()) {
    if (m_omx_tunnel_mixer.Establish() != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_tunnel_decoder.Establish");
      return false;
      }
    }

  if (m_omx_splitter.IsInitialized() ) {
    if (m_omx_splitter.SetStateForComponent(OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_splitter OMX_StateExecuting");
      return false;
      }
    }

  if (m_omx_render_analog.IsInitialized() ) {
    if (m_omx_render_analog.SetStateForComponent(OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_analog OMX_StateExecuting");
      return false;
      }
    }

  if (m_omx_render_hdmi.IsInitialized() ) {
    if (m_omx_render_hdmi.SetStateForComponent(OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::PortSettingsChanged m_omx_render_hdmi OMX_StateExecuting");
      return false;
      }
    }

  m_settings_changed = true;
  return true;
  }
//}}}
//{{{
bool cOmxAudio::Initialize (cOmxClock *clock, const cOmxAudioConfig &config,
                            uint64_t channelMap, unsigned int uiBitsPerSample) {

  cSingleLock lock (m_critSection);

  Deinitialize();

  m_config = config;
  m_InputChannels = count_bits(channelMap);

  if (m_InputChannels == 0)
    return false;
  if (m_config.hints.samplerate == 0)
    return false;

  m_av_clock = clock;
  if (!m_av_clock)
    return false;

  /* passthrough overwrites hw decode */
  if (m_config.passthrough)
    m_config.hwdecode = false;
  else if (m_config.hwdecode)
    /* check again if we are capable to hw decode the format */
    m_config.hwdecode = CanHWDecode(m_config.hints.codec);

  if (m_config.passthrough || m_config.hwdecode)
    SetCodingType (m_config.hints.codec);
  else
    SetCodingType (AV_CODEC_ID_PCM_S16LE);

  m_omx_clock = m_av_clock->getOmxClock();
  m_drc = 0;
  memset (m_input_channels, 0x0, sizeof(m_input_channels));
  memset (m_output_channels, 0x0, sizeof(m_output_channels));
  memset (&m_wave_header, 0x0, sizeof(m_wave_header));
  m_wave_header.Format.nChannels = 2;
  m_wave_header.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

  // set the input format, and get the channel layout so we know what we need to open
  if (!m_config.passthrough && channelMap) {
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];
    // force out layout to stereo if input is not multichannel - it gives the receiver a chance to upmix
    if (channelMap == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT) || channelMap == AV_CH_FRONT_CENTER)
      m_config.layout = PCM_LAYOUT_2_0;
    BuildChannelMap(inLayout, channelMap);
    m_OutputChannels = BuildChannelMapCEA(outLayout, GetChannelLayout(m_config.layout));
    cPcmRemap m_remap;
    m_remap.Reset();
    m_remap.SetInputFormat (m_InputChannels, inLayout, uiBitsPerSample / 8, m_config.hints.samplerate, m_config.layout, m_config.boostOnDownmix);
    m_remap.SetOutputFormat(m_OutputChannels, outLayout);
    m_remap.GetDownmixMatrix(m_downmix_matrix);
    m_wave_header.dwChannelMask = channelMap;
    BuildChannelMapOMX (m_input_channels, channelMap);
    BuildChannelMapOMX (m_output_channels, GetChannelLayout(m_config.layout));
    }

  m_BitsPerSample = uiBitsPerSample;
  m_BytesPerSec   = m_config.hints.samplerate * 2 << rounded_up_channels_shift[m_InputChannels];
  m_BufferLen     = m_BytesPerSec * AUDIO_BUFFER_SECONDS;
  m_InputBytesPerSec = m_config.hints.samplerate * m_BitsPerSample * m_InputChannels >> 3;

  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so a full 48K input buffer will fit the output buffer
  m_ChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (m_InputChannels * m_BitsPerSample) >> (rounded_up_channels_shift[m_InputChannels] + 4);

  m_wave_header.Samples.wSamplesPerBlock    = 0;
  m_wave_header.Format.nChannels            = m_InputChannels;
  m_wave_header.Format.nBlockAlign          = m_InputChannels * (m_BitsPerSample >> 3);
  // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
  m_wave_header.Format.wFormatTag           = m_BitsPerSample == 32 ? 0x8000 : WAVE_FORMAT_PCM;
  m_wave_header.Format.nSamplesPerSec       = m_config.hints.samplerate;
  m_wave_header.Format.nAvgBytesPerSec      = m_BytesPerSec;
  m_wave_header.Format.wBitsPerSample       = m_BitsPerSample;
  m_wave_header.Samples.wValidBitsPerSample = m_BitsPerSample;
  m_wave_header.Format.cbSize               = 0;
  m_wave_header.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;

  if (!m_omx_decoder.Initialize ("OMX.broadcom.audio_decode", OMX_IndexParamAudioInit))
    return false;

  OMX_CONFIG_BOOLEANTYPE boolType;
  OMX_INIT_STRUCTURE(boolType);
  if (m_config.passthrough)
    boolType.bEnabled = OMX_TRUE;
  else
    boolType.bEnabled = OMX_FALSE;
  if (m_omx_decoder.SetParameter (OMX_IndexParamBrcmDecoderPassThrough, &boolType) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize OMX_IndexParamBrcmDecoderPassThrough");
    return false;
    }

  // set up the number/size of buffers for decoder input
  OMX_PARAM_PORTDEFINITIONTYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetInputPort();
  if (m_omx_decoder.GetParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize OMX_IndexParamPortDefinition");
    return false;
    }

  port_param.format.audio.eEncoding = m_eEncoding;
  port_param.nBufferSize = m_ChunkLen;
  port_param.nBufferCountActual = std::max (port_param.nBufferCountMin, 16U);
  if (m_omx_decoder.SetParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize error set OMX_IndexParamPortDefinition");
    return false;
    }

  // set up the number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetOutputPort();
  if (m_omx_decoder.GetParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize get OMX_IndexParamPortDefinition out");
    return false;
    }

  port_param.nBufferCountActual = std::max ((unsigned int)port_param.nBufferCountMin, m_BufferLen / port_param.nBufferSize);
  if (m_omx_decoder.SetParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize error set OMX_IndexParamPortDefinition out");
    return false;
    }

  OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = m_omx_decoder.GetInputPort();
  formatType.eEncoding = m_eEncoding;
  if (m_omx_decoder.SetParameter (OMX_IndexParamAudioPortFormat, &formatType) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize  OMX_IndexParamAudioPortFormat");
    return false;
    }

  if (m_omx_decoder.AllocInputBuffers() != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize alloc buffers");
    return false;
    }

  if (m_omx_decoder.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::Initialize -  OMX_StateExecuting");
    return false;
    }

  if (m_eEncoding == OMX_AUDIO_CodingPCM) {
    auto omx_buffer = m_omx_decoder.GetInputBuffer();
    if (omx_buffer == NULL) {
      cLog::Log (LOGERROR, "cOmxAudio::Initialize buffer error");
      return false;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = std::min (sizeof(m_wave_header), omx_buffer->nAllocLen);
    memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy ((unsigned char*)omx_buffer->pBuffer, &m_wave_header, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (m_omx_decoder.EmptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::InitializeO MX_EmptyThisBuffer()");
      m_omx_decoder.DecoderEmptyBufferDone (m_omx_decoder.GetComponent(), omx_buffer);
      return false;
      }
    }
  else if (m_config.hwdecode) {
    // send decoder config
    if (m_config.hints.extrasize > 0 && m_config.hints.extradata != NULL) {
      auto omx_buffer = m_omx_decoder.GetInputBuffer();
      if (omx_buffer == NULL) {
        cLog::Log (LOGERROR, "cOmxAudio::Initialize buffer error");
        return false;
        }

      omx_buffer->nOffset = 0;
      omx_buffer->nFilledLen = std::min((OMX_U32)m_config.hints.extrasize, omx_buffer->nAllocLen);
      memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy((unsigned char *)omx_buffer->pBuffer, m_config.hints.extradata, omx_buffer->nFilledLen);
      omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
      if (m_omx_decoder.EmptyThisBuffer(omx_buffer) != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxAudio::Initialize OMX_EmptyThisBuffer()");
        m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
        return false;
        }
      }
    }

  /* return on decoder error so m_Initialized stays false */
  if (m_omx_decoder.BadState())
    return false;

  OMX_INIT_STRUCTURE(m_pcm_input);
  m_pcm_input.nPortIndex = m_omx_decoder.GetInputPort();
  memcpy (m_pcm_input.eChannelMapping, m_input_channels, sizeof(m_input_channels));
  m_pcm_input.eNumData = OMX_NumericalDataSigned;
  m_pcm_input.eEndian = OMX_EndianLittle;
  m_pcm_input.bInterleaved = OMX_TRUE;
  m_pcm_input.nBitPerSample = m_BitsPerSample;
  m_pcm_input.ePCMMode = OMX_AUDIO_PCMModeLinear;
  m_pcm_input.nChannels = m_InputChannels;
  m_pcm_input.nSamplingRate = m_config.hints.samplerate;

  m_Initialized   = true;
  m_settings_changed = false;
  m_setStartTime  = true;
  m_submitted_eos = false;
  m_failed_eos = false;
  m_last_pts = DVD_NOPTS_VALUE;
  m_submitted = 0.0f;
  m_maxLevel = 0.0f;

  cLog::Log (LOGDEBUG, "cOmxAudio::Initialize Input bitsPer:%d rate:%d ch:%d buffer size:%d bytesPer:%d",
             (int)m_pcm_input.nBitPerSample, (int)m_pcm_input.nSamplingRate, (int)m_pcm_input.nChannels, m_BufferLen, m_InputBytesPerSec);
  PrintPCM (&m_pcm_input, std::string ("input"));
  cLog::Log (LOGDEBUG, "cOmxAudio::Initialize dev:%s pass:%d hw:%d",
             m_config.device.c_str(), m_config.passthrough, m_config.hwdecode);

  return true;
  }
//}}}
//{{{
bool cOmxAudio::Deinitialize()
{
  cSingleLock lock (m_critSection);

  if ( m_omx_tunnel_clock_analog.IsInitialized() )
    m_omx_tunnel_clock_analog.Deestablish();
  if ( m_omx_tunnel_clock_hdmi.IsInitialized() )
    m_omx_tunnel_clock_hdmi.Deestablish();

  // ignore expected errors on teardown
  if ( m_omx_mixer.IsInitialized() )
    m_omx_mixer.IgnoreNextError(OMX_ErrorPortUnpopulated);
  else
  {
    if ( m_omx_render_hdmi.IsInitialized() )
      m_omx_render_hdmi.IgnoreNextError(OMX_ErrorPortUnpopulated);
    if ( m_omx_render_analog.IsInitialized() )
      m_omx_render_analog.IgnoreNextError(OMX_ErrorPortUnpopulated);
  }

  m_omx_tunnel_decoder.Deestablish();
  if ( m_omx_tunnel_mixer.IsInitialized() )
    m_omx_tunnel_mixer.Deestablish();
  if ( m_omx_tunnel_splitter_hdmi.IsInitialized() )
    m_omx_tunnel_splitter_hdmi.Deestablish();
  if ( m_omx_tunnel_splitter_analog.IsInitialized() )
    m_omx_tunnel_splitter_analog.Deestablish();

  m_omx_decoder.FlushInput();

  m_omx_decoder.Deinitialize();
  if ( m_omx_mixer.IsInitialized() )
    m_omx_mixer.Deinitialize();
  if ( m_omx_splitter.IsInitialized() )
    m_omx_splitter.Deinitialize();
  if ( m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.Deinitialize();
  if ( m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.Deinitialize();

  m_BytesPerSec = 0;
  m_BufferLen   = 0;

  m_omx_clock = NULL;
  m_av_clock  = NULL;

  m_Initialized = false;

  while(!m_ampqueue.empty())
    m_ampqueue.pop_front();

  m_last_pts      = DVD_NOPTS_VALUE;
  m_submitted     = 0.0f;
  m_maxLevel      = 0.0f;

  return true;
}
//}}}

//{{{
void cOmxAudio::Flush() {

  cSingleLock lock (m_critSection);
  if (!m_Initialized)
    return;

  m_omx_decoder.FlushAll();
  if (m_omx_mixer.IsInitialized() )
    m_omx_mixer.FlushAll();
  if (m_omx_splitter.IsInitialized() )
    m_omx_splitter.FlushAll();

  if (m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.FlushAll();
  if (m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.FlushAll();

  while (!m_ampqueue.empty())
    m_ampqueue.pop_front();

  if (m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.ResetEos();
  if (m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.ResetEos();

  m_last_pts      = DVD_NOPTS_VALUE;
  m_submitted     = 0.0f;
  m_maxLevel      = 0.0f;
  m_setStartTime  = true;
  }
//}}}
//{{{
float cOmxAudio::GetVolume() {
  return m_Mute ? VOLUME_MINIMUM : m_CurrentVolume;
  }
//}}}

//{{{
unsigned int cOmxAudio::AddPackets (const void* data, unsigned int len,
                                    double dts, double pts, unsigned int frame_size) {

  if (!m_Initialized) {
    cLog::Log (LOGERROR,"cOmxAudio::AddPackets not inited");
    return len;
    }

  cSingleLock lock (m_critSection);

  unsigned pitch = (m_config.passthrough || m_config.hwdecode) ? 1:(m_BitsPerSample >> 3) * m_InputChannels;
  unsigned int demuxer_samples = len / pitch;
  unsigned int demuxer_samples_sent = 0;
  auto demuxer_content = (uint8_t *)data;

  OMX_BUFFERHEADERTYPE* omx_buffer = NULL;
  while (demuxer_samples_sent < demuxer_samples) {
    // 200ms timeout
    omx_buffer = m_omx_decoder.GetInputBuffer (200);
    if (omx_buffer == NULL) {
      cLog::Log (LOGERROR, "cOmxAudio::Decode timeout");
      return len;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFlags  = 0;

    // we want audio_decode output buffer size to be no more than AUDIO_DECODE_OUTPUT_BUFFER.
    // it will be 16-bit and rounded up to next power of 2 in channels
    unsigned int max_buffer = AUDIO_DECODE_OUTPUT_BUFFER * (m_InputChannels * m_BitsPerSample) >> (rounded_up_channels_shift[m_InputChannels] + 4);
    unsigned int remaining = demuxer_samples-demuxer_samples_sent;
    unsigned int samples_space = std::min(max_buffer, omx_buffer->nAllocLen)/pitch;
    unsigned int samples = std::min(remaining, samples_space);

    omx_buffer->nFilledLen = samples * pitch;

    unsigned int frames = frame_size ? len/frame_size:0;
    if ((samples < demuxer_samples || frames > 1) &&
        m_BitsPerSample==32 && !(m_config.passthrough || m_config.hwdecode)) {
      const unsigned int sample_pitch = m_BitsPerSample >> 3;
      const unsigned int frame_samples = frame_size / pitch;
      const unsigned int plane_size = frame_samples * sample_pitch;
      const unsigned int out_plane_size = samples * sample_pitch;
      for (unsigned int sample = 0; sample < samples;) {
        unsigned int frame = (demuxer_samples_sent + sample) / frame_samples;
        unsigned int sample_in_frame = (demuxer_samples_sent + sample) - frame * frame_samples;
        int out_remaining = std::min(std::min(frame_samples - sample_in_frame, samples), samples-sample);
        auto src = demuxer_content + frame*frame_size + sample_in_frame * sample_pitch;
        auto dst = (uint8_t*)omx_buffer->pBuffer + sample * sample_pitch;
        for (unsigned int channel = 0; channel < m_InputChannels; channel++) {
          memcpy (dst, src, out_remaining * sample_pitch);
          src += plane_size;
          dst += out_plane_size;
          }
        sample += out_remaining;
        }
      }
    else
      memcpy (omx_buffer->pBuffer, demuxer_content + demuxer_samples_sent * pitch, omx_buffer->nFilledLen);

    uint64_t val = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;
    if (m_setStartTime) {
      omx_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
      m_last_pts = pts;
      cLog::Log (LOGDEBUG, "cOmxAudio::AddPackets setStartTime:%f", (float)val / DVD_TIME_BASE);
      m_setStartTime = false;
      }
    else {
      if (pts == DVD_NOPTS_VALUE) {
        omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        m_last_pts = pts;
        }
      else if (m_last_pts != pts) {
        if (pts > m_last_pts)
          m_last_pts = pts;
        else
          omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        }
      else if (m_last_pts == pts)
        omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }

    omx_buffer->nTimeStamp = toOmxTime (val);
    demuxer_samples_sent += samples;
    if (demuxer_samples_sent == demuxer_samples)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    if (m_omx_decoder.EmptyThisBuffer(omx_buffer) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::AddPackets OMX_EmptyThisBuffer");
      m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
      return 0;
      }

    if (m_omx_decoder.WaitForEvent(OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone)
      if (!PortSettingsChanged())
        cLog::Log (LOGERROR, "cOmxAudio::AddPackets PortSettingsChanged");
    }

  m_submitted += (float)demuxer_samples / m_config.hints.samplerate;
  UpdateAttenuation();
  return len;
  }
//}}}
//{{{
unsigned int cOmxAudio::AddPackets (const void* data, unsigned int len) {
  return AddPackets (data, len, 0, 0, 0);
  }
//}}}

//{{{
unsigned int cOmxAudio::GetSpace() {
  return m_omx_decoder.GetInputBufferSpace();
  }
//}}}
//{{{
float cOmxAudio::GetDelay() {

  cSingleLock lock (m_critSection);

  double stamp = DVD_NOPTS_VALUE;
  if (m_last_pts != DVD_NOPTS_VALUE && m_av_clock)
    stamp = m_av_clock->getMediaTime();

  // if possible the delay is current media time - time of last submitted packet
  if (stamp != DVD_NOPTS_VALUE)
    return (m_last_pts - stamp) * (1.0 / DVD_TIME_BASE);
  else { // just measure the input fifo
    unsigned int used = m_omx_decoder.GetInputBufferSize() - m_omx_decoder.GetInputBufferSpace();
    return m_InputBytesPerSec ? (float)used / (float)m_InputBytesPerSec : 0.0f;
    }
  }
//}}}
//{{{
float cOmxAudio::GetCacheTime() {
  return GetDelay();
  }
//}}}
//{{{
float cOmxAudio::GetCacheTotal() {
  float audioplus_buffer = m_config.hints.samplerate ? 32.0f * 512.0f / m_config.hints.samplerate : 0.0f;
  float input_buffer = m_InputBytesPerSec ?
    (float)m_omx_decoder.GetInputBufferSize() / (float)m_InputBytesPerSec : 0;
  return AUDIO_BUFFER_SECONDS + input_buffer + audioplus_buffer;
  }
//}}}
//{{{
unsigned int cOmxAudio::GetChunkLen() {
  return m_ChunkLen;
  }
//}}}
//{{{
unsigned int cOmxAudio::GetAudioRenderingLatency() {

  cSingleLock lock (m_critSection);

  if (!m_Initialized)
    return 0;

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);
  if (m_omx_render_analog.IsInitialized()) {
    param.nPortIndex = m_omx_render_analog.GetInputPort();
    if (m_omx_render_analog.GetConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::GetAudioRenderingLatency analog OMX_IndexConfigAudioRenderingLatency");
      return 0;
      }
    }

  else if (m_omx_render_hdmi.IsInitialized()) {
    param.nPortIndex = m_omx_render_hdmi.GetInputPort();
    if (m_omx_render_hdmi.GetConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::GetAudioRenderingLatency hdmi OMX_IndexConfigAudioRenderingLatency");
      return 0;
      }
    }

  return param.nU32;
  }
//}}}
//{{{
float cOmxAudio::GetMaxLevel (double &pts) {

  cSingleLock lock (m_critSection);
  if(!m_Initialized)
    return 0;

  OMX_CONFIG_BRCMAUDIOMAXSAMPLE param;
  OMX_INIT_STRUCTURE(param);
  if (m_omx_decoder.IsInitialized()) {
    param.nPortIndex = m_omx_decoder.GetInputPort();
    if (m_omx_decoder.GetConfig(OMX_IndexConfigBrcmAudioMaxSample, &param) != OMX_ErrorNone) {
      cLog::Log(LOGERROR, "cOmxAudio::GetMaxLevel OMX_IndexConfigBrcmAudioMaxSample");
      return 0;
      }
    }

  pts = fromOmxTime (param.nTimeStamp);
  return (float)param.nMaxSample * (100.0f / (1<<15));
  }
//}}}
//{{{
uint64_t cOmxAudio::GetChannelLayout (enum PCMLayout layout) {

  uint64_t layouts[] = {
    /* 2.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT,
    /* 2.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 3.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER,
    /* 3.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_LOW_FREQUENCY,
    /* 4.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 4.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 5.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 5.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 7.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 7.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY
    };

  return (int)layout < 10 ? layouts[(int)layout] : 0;
  }
//}}}

//{{{
void cOmxAudio::SetCodingType (AVCodecID codec) {

  switch(codec) {
    case AV_CODEC_ID_DTS:
      cLog::Log (LOGDEBUG, "cOmxAudio::SetCodingType OMX_AUDIO_CodingDTS");
      m_eEncoding = OMX_AUDIO_CodingDTS;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::Log (LOGDEBUG, "cOmxAudio::SetCodingType OMX_AUDIO_CodingDDP");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      break;

    default:
      cLog::Log (LOGDEBUG, "cOmxAudio::SetCodingType OMX_AUDIO_CodingPCM");
      m_eEncoding = OMX_AUDIO_CodingPCM;
      break;
    }
  }
//}}}
//{{{
void cOmxAudio::SetDynamicRangeCompression (long drc) {
  cSingleLock lock (m_critSection);
  m_amplification = powf(10.0f, (float)drc / 2000.0f);
  if (m_settings_changed)
    UpdateAttenuation();
  }
//}}}
//{{{
void cOmxAudio::SetMute (bool bMute) {
  cSingleLock lock (m_critSection);
  m_Mute = bMute;
  if (m_settings_changed)
    UpdateAttenuation();
  }
//}}}
//{{{
void cOmxAudio::SetVolume (float fVolume) {
  cSingleLock lock (m_critSection);
  m_CurrentVolume = fVolume;
  if (m_settings_changed)
    UpdateAttenuation();
  }
//}}}

//{{{
void cOmxAudio::SubmitEOS() {

  cSingleLock lock (m_critSection);
  if (!m_Initialized)
    return;

  m_submitted_eos = true;
  m_failed_eos = false;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer(1000);
  if (omx_buffer == NULL) {
    cLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", "cOmxAudio", __func__, omx_err);
    m_failed_eos = true;
    return;
    }
  omx_buffer->nOffset = 0;
  omx_buffer->nFilledLen = 0;
  omx_buffer->nTimeStamp = toOmxTime (0LL);
  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
  if (omx_err != OMX_ErrorNone) {
    cLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)", "cOmxAudio", __func__, omx_err);
    m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
    return;
    }

  cLog::Log (LOGINFO, "cOmxAudio::SubmitEOS()");
  }
//}}}
//{{{
bool cOmxAudio::IsEOS() {

  if (!m_Initialized)
    return true;

  unsigned int latency = GetAudioRenderingLatency();

  cSingleLock lock (m_critSection);

  if (!m_failed_eos && !(m_omx_decoder.IsEOS() && latency == 0))
    return false;

  if (m_submitted_eos) {
    cLog::Log (LOGINFO, "cOmxAudio::IsEOS");
    m_submitted_eos = false;
    }

  return true;
  }
//}}}

//{{{
bool cOmxAudio::CanHWDecode (AVCodecID codec) {

  switch(codec) {
    case AV_CODEC_ID_VORBIS:
      cLog::Log(LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingVORBIS");
      m_eEncoding = OMX_AUDIO_CodingVORBIS;
      m_config.hwdecode = true;
      break;
    case AV_CODEC_ID_AAC:
      cLog::Log(LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingAAC");
      m_eEncoding = OMX_AUDIO_CodingAAC;
      m_config.hwdecode = true;
      break;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      cLog::Log (LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingMP3");
      m_eEncoding = OMX_AUDIO_CodingMP3;
      m_config.hwdecode = true;
      break;

    case AV_CODEC_ID_DTS:
      cLog::Log (LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingDTS");
      m_eEncoding = OMX_AUDIO_CodingDTS;
      m_config.hwdecode = true;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::Log (LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingDDP");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      m_config.hwdecode = true;
      break;

    default:
      cLog::Log (LOGDEBUG, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingPCM");
      m_eEncoding = OMX_AUDIO_CodingPCM;
      m_config.hwdecode = false;
      break;
    }

  return m_config.hwdecode;
  }
//}}}
//{{{
bool cOmxAudio::HWDecode (AVCodecID codec) {

  bool ret = false;
  switch(codec) {
    case AV_CODEC_ID_VORBIS:
      cLog::Log(LOGDEBUG, "cOmxAudio::HWDecode AV_CODEC_ID_VORBIS");
      ret = true;
      break;
    case AV_CODEC_ID_AAC:
      cLog::Log(LOGDEBUG, "cOmxAudio::HWDecode AV_CODEC_ID_AAC");
      ret = true;
      break;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      cLog::Log (LOGDEBUG, "cOmxAudio::HWDecode AV_CODEC_ID_MP2 / AV_CODEC_ID_MP3");
      ret = true;
      break;

    case AV_CODEC_ID_DTS:
      cLog::Log (LOGDEBUG, "cOmxAudio::HWDecode AV_CODEC_ID_DTS");
      ret = true;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::Log (LOGDEBUG, "cOmxAudio::HWDecode AV_CODEC_ID_AC3 / AV_CODEC_ID_EAC3");
      ret = true;
      break;

    default:
      ret = false;
      break;
    }

  return ret;
  }
//}}}

//{{{
void cOmxAudio::BuildChannelMap (enum PCMChannels *channelMap, uint64_t layout) {

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = PCM_FRONT_LEFT           ;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = PCM_FRONT_RIGHT          ;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = PCM_FRONT_CENTER         ;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = PCM_LOW_FREQUENCY        ;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = PCM_BACK_LEFT            ;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = PCM_BACK_RIGHT           ;
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) channelMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) channelMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
  if (layout & AV_CH_BACK_CENTER          ) channelMap[index++] = PCM_BACK_CENTER          ;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = PCM_SIDE_LEFT            ;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = PCM_SIDE_RIGHT           ;
  if (layout & AV_CH_TOP_CENTER           ) channelMap[index++] = PCM_TOP_CENTER           ;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) channelMap[index++] = PCM_TOP_FRONT_LEFT       ;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) channelMap[index++] = PCM_TOP_FRONT_CENTER     ;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) channelMap[index++] = PCM_TOP_FRONT_RIGHT      ;
  if (layout & AV_CH_TOP_BACK_LEFT        ) channelMap[index++] = PCM_TOP_BACK_LEFT        ;
  if (layout & AV_CH_TOP_BACK_CENTER      ) channelMap[index++] = PCM_TOP_BACK_CENTER      ;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) channelMap[index++] = PCM_TOP_BACK_RIGHT       ;

  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;
  }
//}}}
//{{{
// See CEA spec: Table 20, Audio InfoFrame data byte 4 for the ordering here
int cOmxAudio::BuildChannelMapCEA (enum PCMChannels *channelMap, uint64_t layout) {

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = PCM_FRONT_LEFT;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = PCM_FRONT_RIGHT;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = PCM_LOW_FREQUENCY;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = PCM_FRONT_CENTER;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = PCM_BACK_LEFT;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = PCM_BACK_RIGHT;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = PCM_SIDE_LEFT;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = PCM_SIDE_RIGHT;

  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;

  int num_channels = 0;
  for (index=0; index<OMX_AUDIO_MAXCHANNELS; index++)
    if (channelMap[index] != PCM_INVALID)
       num_channels = index+1;

  // round up to power of 2
  num_channels = num_channels > 4 ? 8 : num_channels > 2 ? 4 : num_channels;
  return num_channels;
  }
//}}}
//{{{
void cOmxAudio::BuildChannelMapOMX (enum OMX_AUDIO_CHANNELTYPE * channelMap, uint64_t layout) {

  int index = 0;

  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = OMX_AUDIO_ChannelLF;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = OMX_AUDIO_ChannelRF;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = OMX_AUDIO_ChannelCF;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = OMX_AUDIO_ChannelLFE;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = OMX_AUDIO_ChannelLR;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = OMX_AUDIO_ChannelRR;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = OMX_AUDIO_ChannelLS;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = OMX_AUDIO_ChannelRS;
  if (layout & AV_CH_BACK_CENTER          ) channelMap[index++] = OMX_AUDIO_ChannelCS;

  // following are not in openmax spec, but gpu does accept them
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)10;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)11;
  if (layout & AV_CH_TOP_CENTER           ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)12;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)13;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)14;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)15;
  if (layout & AV_CH_TOP_BACK_LEFT        ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)16;
  if (layout & AV_CH_TOP_BACK_CENTER      ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)17;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)18;

  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = OMX_AUDIO_ChannelNone;
  }
//}}}

// private
//{{{
bool cOmxAudio::ApplyVolume() {

  float m_ac3Gain = 12.0f;

  cSingleLock lock (m_critSection);
  if (!m_Initialized || m_config.passthrough)
    return false;

  // the analogue volume is too quiet for some. Allow use of an advancedsetting to boost this (at risk of distortion) (deprecated)
  float fVolume = m_Mute ? VOLUME_MINIMUM : m_CurrentVolume;
  double gain = pow(10, (m_ac3Gain - 12.0f) / 20.0);
  const float* coeff = m_downmix_matrix;

  OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
  OMX_INIT_STRUCTURE(mix);
  assert (sizeof(mix.coeff) / sizeof(mix.coeff[0]) == 64);
  if (m_amplification != 1.0) {
    // reduce scaling so overflow can be seen
    for (size_t i = 0; i < 8*8; ++i)
      mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * 0.01f));
    mix.nPortIndex = m_omx_decoder.GetInputPort();
    if (m_omx_decoder.SetConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxAudio::ApplyVolume OMX_IndexConfigBrcmAudioDownmixCoefficients");
      return false;
      }
    }

  for (size_t i = 0; i < 8*8; ++i)
    mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * fVolume * m_amplification * m_attenuation));

  mix.nPortIndex = m_omx_mixer.GetInputPort();
  if (m_omx_mixer.SetConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "cOmxAudio::ApplyVolume OMX_IndexConfigBrcmAudioDownmixCoefficients");
    return false;
    }

  cLog::Log (LOGINFO, "cOmxAudio::ApplyVolume %.2f (* %.2f * %.2f)", fVolume, m_amplification, m_attenuation);
  return true;
  }
//}}}
//{{{
void cOmxAudio::UpdateAttenuation() {

  if (m_amplification == 1.0) {
    ApplyVolume();
    return;
    }

  double level_pts = 0.0;
  float level = GetMaxLevel(level_pts);
  if (level_pts != 0.0) {
    amplitudes_t v;
    v.level = level;
    v.pts = level_pts;
    m_ampqueue.push_back(v);
    }

  double stamp = m_av_clock->getMediaTime();
  // discard too old data
  while (!m_ampqueue.empty()) {
    amplitudes_t &v = m_ampqueue.front();
    /* we'll also consume if queue gets unexpectedly long to avoid filling memory */
    if (v.pts == DVD_NOPTS_VALUE || v.pts < stamp || v.pts - stamp > DVD_SEC_TO_TIME(15.0))
      m_ampqueue.pop_front();
    else
      break;
    }

  float maxlevel = 0.0f, imminent_maxlevel = 0.0f;
  for (int i=0; i < (int)m_ampqueue.size(); i++) {
    amplitudes_t &v = m_ampqueue[i];
    maxlevel = std::max(maxlevel, v.level);
    // check for maximum volume in next 200ms
    if (v.pts != DVD_NOPTS_VALUE && v.pts < stamp + DVD_SEC_TO_TIME(0.2))
      imminent_maxlevel = std::max(imminent_maxlevel, v.level);
    }

  if (maxlevel != 0.0) {
    float m_limiterHold = 0.025f;
    float m_limiterRelease = 0.100f;
    float alpha_h = -1.0f/(0.025f*log10f(0.999f));
    float alpha_r = -1.0f/(0.100f*log10f(0.900f));
    float decay  = powf (10.0f, -1.0f / (alpha_h * m_limiterHold));
    float attack = powf (10.0f, -1.0f / (alpha_r * m_limiterRelease));
    // if we are going to clip imminently then deal with it now
    if (imminent_maxlevel > m_maxLevel)
      m_maxLevel = imminent_maxlevel;
    // clip but not imminently can ramp up more slowly
    else if (maxlevel > m_maxLevel)
      m_maxLevel = attack * m_maxLevel + (1.0f-attack) * maxlevel;
    // not clipping, decay more slowly
    else
      m_maxLevel = decay  * m_maxLevel + (1.0f-decay ) * maxlevel;

    // want m_maxLevel * amp -> 1.0
    float amp = m_amplification * m_attenuation;

    // We fade in the attenuation over first couple of seconds
    float start = std::min (std::max ((m_submitted-1.0f), 0.0f), 1.0f);
    float attenuation = std::min( 1.0f, std::max(m_attenuation / (amp * m_maxLevel), 1.0f/m_amplification));
    m_attenuation = (1.0f - start) * 1.0f/m_amplification + start * attenuation;
    }
  else
    m_attenuation = 1.0f / m_amplification;

  ApplyVolume();
  }
//}}}

//{{{
void cOmxAudio::PrintChannels (OMX_AUDIO_CHANNELTYPE eChannelMapping[]) {

  for (int i = 0; i < OMX_AUDIO_MAXCHANNELS; i++) {
    switch (eChannelMapping[i]) {
      case OMX_AUDIO_ChannelLF:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLF");
        break;

      case OMX_AUDIO_ChannelRF:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRF");
        break;

      case OMX_AUDIO_ChannelCF:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelCF");
        break;

      case OMX_AUDIO_ChannelLS:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLS");
        break;

      case OMX_AUDIO_ChannelRS:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRS");
        break;

      case OMX_AUDIO_ChannelLFE:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLFE");
        break;

      case OMX_AUDIO_ChannelCS:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelCS");
        break;

      case OMX_AUDIO_ChannelLR:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLR");
        break;

      case OMX_AUDIO_ChannelRR:
        cLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRR");
        break;

      case OMX_AUDIO_ChannelNone:
      case OMX_AUDIO_ChannelKhronosExtensions:
      case OMX_AUDIO_ChannelVendorStartUnused:
      case OMX_AUDIO_ChannelMax:

      default:
        break;
      }
    }
  }
//}}}
//{{{
void cOmxAudio::PrintPCM (OMX_AUDIO_PARAM_PCMMODETYPE *pcm, std::string direction) {

  cLog::Log (LOGDEBUG, "pcm->direction    : %s", direction.c_str());
  cLog::Log (LOGDEBUG, "pcm->nPortIndex   : %d", (int)pcm->nPortIndex);
  cLog::Log (LOGDEBUG, "pcm->eNumData     : %d", pcm->eNumData);
  cLog::Log (LOGDEBUG, "pcm->eEndian      : %d", pcm->eEndian);
  cLog::Log (LOGDEBUG, "pcm->bInterleaved : %d", (int)pcm->bInterleaved);
  cLog::Log (LOGDEBUG, "pcm->nBitPerSample: %d", (int)pcm->nBitPerSample);
  cLog::Log (LOGDEBUG, "pcm->ePCMMode     : %d", pcm->ePCMMode);
  cLog::Log (LOGDEBUG, "pcm->nChannels    : %d", (int)pcm->nChannels);
  cLog::Log (LOGDEBUG, "pcm->nSamplingRate: %d", (int)pcm->nSamplingRate);

  PrintChannels (pcm->eChannelMapping);
  }
//}}}
