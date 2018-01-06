// OMXVideo.cpp
//{{{  includes
#include <sys/time.h>
#include <inttypes.h>

#include "cVideo.h"

#include "cOmxStreamInfo.h"
#include "../shared/utils/cLog.h"
//}}}
//{{{  decoder defines
#define OMX_VIDEO_DECODER       "OMX.broadcom.video_decode"
#define OMX_H264BASE_DECODER    OMX_VIDEO_DECODER
#define OMX_H264MAIN_DECODER    OMX_VIDEO_DECODER
#define OMX_H264HIGH_DECODER    OMX_VIDEO_DECODER
#define OMX_MPEG4_DECODER       OMX_VIDEO_DECODER
#define OMX_MSMPEG4V1_DECODER   OMX_VIDEO_DECODER
#define OMX_MSMPEG4V2_DECODER   OMX_VIDEO_DECODER
#define OMX_MSMPEG4V3_DECODER   OMX_VIDEO_DECODER
#define OMX_MPEG4EXT_DECODER    OMX_VIDEO_DECODER
#define OMX_MPEG2V_DECODER      OMX_VIDEO_DECODER
#define OMX_VC1_DECODER         OMX_VIDEO_DECODER
#define OMX_WMV3_DECODER        OMX_VIDEO_DECODER
#define OMX_VP6_DECODER         OMX_VIDEO_DECODER
#define OMX_VP8_DECODER         OMX_VIDEO_DECODER
#define OMX_THEORA_DECODER      OMX_VIDEO_DECODER
#define OMX_MJPEG_DECODER       OMX_VIDEO_DECODER
//}}}

//{{{
cOmxVideo::cOmxVideo() {
  m_is_open           = false;
  m_deinterlace       = false;
  m_drop_state        = false;
  m_omx_clock         = NULL;
  m_av_clock          = NULL;
  m_submitted_eos     = false;
  m_failed_eos        = false;
  m_settings_changed  = false;
  m_setStartTime      = false;
  m_transform         = OMX_DISPLAY_ROT0;
  m_pixel_aspect      = 1.0f;
  }
//}}}
//{{{
cOmxVideo::~cOmxVideo() {
  Close();
  }
//}}}

//{{{
bool cOmxVideo::SendDecoderConfig() {

  cSingleLock lock (m_critSection);

  if ((m_config.hints.extrasize > 0) && (m_config.hints.extradata != NULL)) {
    auto omx_buffer = m_omx_decoder.GetInputBuffer();
    if (omx_buffer == NULL) {
      cLog::log (LOGERROR, "cOmxVideo::SendDecoderConfig buffer error");
      return false;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = std::min ((OMX_U32)m_config.hints.extrasize, omx_buffer->nAllocLen);
    memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy ((unsigned char*)omx_buffer->pBuffer, m_config.hints.extradata, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (m_omx_decoder.EmptyThisBuffer(omx_buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::SendDecoderConfig OMX_EmptyThisBuffer()");
      m_omx_decoder.DecoderEmptyBufferDone (m_omx_decoder.GetComponent(), omx_buffer);
      return false;
      }
    }

  return true;
  }
//}}}
//{{{
bool cOmxVideo::NaluFormatStartCodes (enum AVCodecID codec, uint8_t *in_extradata, int in_extrasize) {
// valid avcC atom data always starts with the value 1 (version), otherwise annexb

  switch (codec) {
    case AV_CODEC_ID_H264:
      if (in_extrasize < 7 || in_extradata == NULL)
        return true;
      else if (*in_extradata != 1)
        return true;
    default: break;
    }

  return false;
  }
//}}}

//{{{
bool cOmxVideo::Open (cOmxClock* clock, const cOmxVideoConfig &config) {

  cSingleLock lock (m_critSection);

  bool vflip = false;
  Close();

  std::string decoder_name;
  m_settings_changed = false;
  m_setStartTime = true;

  m_config = config;
  m_video_codec_name = "";
  m_codingType = OMX_VIDEO_CodingUnused;
  m_submitted_eos = false;
  m_failed_eos = false;

  if (!m_config.hints.width || !m_config.hints.height)
    return false;

  switch (m_config.hints.codec) {
    //{{{
    case AV_CODEC_ID_H264: {
      switch(m_config.hints.profile) {
        case FF_PROFILE_H264_BASELINE:
          // (role name) video_decoder.avc
          // H.264 Baseline profile
          decoder_name = OMX_H264BASE_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_H264_MAIN:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264MAIN_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_H264_HIGH:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_UNKNOWN:
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        default:
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        }
      }
    if (m_config.allow_mvc && m_codingType == OMX_VIDEO_CodingAVC) {
      m_codingType = OMX_VIDEO_CodingMVC;
      m_video_codec_name = "omx-mvc";
      }
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_MPEG4:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG4;
      m_video_codec_name = "omx-mpeg4";
      break;
    //}}}
    case AV_CODEC_ID_MPEG1VIDEO:
    //{{{
    case AV_CODEC_ID_MPEG2VIDEO:
      // (role name) video_decoder.mpeg2
      // MPEG-2
      decoder_name = OMX_MPEG2V_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG2;
      m_video_codec_name = "omx-mpeg2";
      break;
    //}}}
    //{{{
    case AV_CODEC_ID_H263:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG4;
      m_video_codec_name = "omx-h263";
      break;
    //}}}
    //{{{
    case AV_CODEC_ID_VP6:
      // this form is encoded upside down
      vflip = true;
      // fall through
    //}}}
    case AV_CODEC_ID_VP6F:
    //{{{
    case AV_CODEC_ID_VP6A:
      // (role name) video_decoder.vp6
      // VP6
      decoder_name = OMX_VP6_DECODER;
      m_codingType = OMX_VIDEO_CodingVP6;
      m_video_codec_name = "omx-vp6";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_VP8:
      // (role name) video_decoder.vp8
      // VP8
      decoder_name = OMX_VP8_DECODER;
      m_codingType = OMX_VIDEO_CodingVP8;
      m_video_codec_name = "omx-vp8";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_THEORA:
      // (role name) video_decoder.theora
      // theora
      decoder_name = OMX_THEORA_DECODER;
      m_codingType = OMX_VIDEO_CodingTheora;
      m_video_codec_name = "omx-theora";
    break;
    //}}}
    case AV_CODEC_ID_MJPEG:
    //{{{
    case AV_CODEC_ID_MJPEGB:
      // (role name) video_decoder.mjpg
      // mjpg
      decoder_name = OMX_MJPEG_DECODER;
      m_codingType = OMX_VIDEO_CodingMJPEG;
      m_video_codec_name = "omx-mjpeg";
    break;
    //}}}
    case AV_CODEC_ID_VC1:
    //{{{
    case AV_CODEC_ID_WMV3:
      // (role name) video_decoder.vc1
      // VC-1, WMV9
      decoder_name = OMX_VC1_DECODER;
      m_codingType = OMX_VIDEO_CodingWMV;
      m_video_codec_name = "omx-vc1";
      break;
    //}}}
    //{{{
    default:
      printf("Vcodec id unknown: %x\n", m_config.hints.codec);
      return false;
    break;
    //}}}
    }

  if (!m_omx_decoder.Initialize (decoder_name, OMX_IndexParamVideoInit))
    return false;
  if (clock == NULL)
    return false;

  m_av_clock = clock;
  m_omx_clock = m_av_clock->getOmxClock();
  if (m_omx_clock->GetComponent() == NULL) {
    m_av_clock = NULL;
    m_omx_clock = NULL;
    return false;
    }

  if (m_omx_decoder.SetStateForComponent (OMX_StateIdle) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open SetStateForComponent");
    return false;
    }

  OMX_VIDEO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = m_omx_decoder.GetInputPort();
  formatType.eCompressionFormat = m_codingType;
  if (m_config.hints.fpsscale > 0 && m_config.hints.fpsrate > 0)
    formatType.xFramerate = (long long)(1<<16)*m_config.hints.fpsrate / m_config.hints.fpsscale;
  else
    formatType.xFramerate = 25 * (1<<16);
  if (m_omx_decoder.SetParameter (OMX_IndexParamVideoPortFormat, &formatType) != OMX_ErrorNone)
    return false;

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = m_omx_decoder.GetInputPort();
  if (m_omx_decoder.GetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamPortDefinition");
    return false;
    }

  portParam.nPortIndex = m_omx_decoder.GetInputPort();
  portParam.nBufferCountActual = m_config.fifo_size ? m_config.fifo_size * 1024 * 1024 / portParam.nBufferSize : 80;
  portParam.format.video.nFrameWidth  = m_config.hints.width;
  portParam.format.video.nFrameHeight = m_config.hints.height;
  if (m_omx_decoder.SetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamPortDefinition");
    return false;
    }

  // request portsettingschanged on aspect ratio change
  OMX_CONFIG_REQUESTCALLBACKTYPE notifications;
  OMX_INIT_STRUCTURE(notifications);
  notifications.nPortIndex = m_omx_decoder.GetOutputPort();
  notifications.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
  notifications.bEnable = OMX_TRUE;
  if (m_omx_decoder.SetParameter ((OMX_INDEXTYPE)OMX_IndexConfigRequestCallback, &notifications) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexConfigRequestCallback");
    return false;
    }

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
  OMX_INIT_STRUCTURE(concanParam);
  concanParam.bStartWithValidFrame = OMX_TRUE;
  if (m_omx_decoder.SetParameter (OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamBrcmVideoDecodeErrorConcealment");
    return false;
    }

  if (NaluFormatStartCodes (m_config.hints.codec, (uint8_t *)m_config.hints.extradata, m_config.hints.extrasize)) {
    OMX_NALSTREAMFORMATTYPE nalStreamFormat;
    OMX_INIT_STRUCTURE(nalStreamFormat);
    nalStreamFormat.nPortIndex = m_omx_decoder.GetInputPort();
    nalStreamFormat.eNaluFormat = OMX_NaluFormatStartCodes;
    if (m_omx_decoder.SetParameter ((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStreamFormat) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamNalStreamFormatSelect");
      return false;
      }
    }

  // Alloc buffers for the omx intput port.
  if (m_omx_decoder.AllocInputBuffers() != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open AllocInputBuffers");
    return false;
    }
  if (m_omx_decoder.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::Open SetStateForComponent");
    return false;
    }

  SendDecoderConfig();

  m_is_open = true;
  m_drop_state = false;
  m_setStartTime = true;
  switch (m_config.hints.orientation) {
    //{{{
    case 90:
      m_transform = OMX_DISPLAY_ROT90;
      break;
    //}}}
    //{{{
    case 180:
      m_transform = OMX_DISPLAY_ROT180;
      break;
    //}}}
    //{{{
    case 270:
      m_transform = OMX_DISPLAY_ROT270;
      break;
    //}}}
    //{{{
    case 1:
      m_transform = OMX_DISPLAY_MIRROR_ROT0;
      break;
    //}}}
    //{{{
    case 91:
      m_transform = OMX_DISPLAY_MIRROR_ROT90;
      break;
    //}}}
    //{{{
    case 181:
      m_transform = OMX_DISPLAY_MIRROR_ROT180;
      break;
    //}}}
    //{{{
    case 271:
      m_transform = OMX_DISPLAY_MIRROR_ROT270;
      break;
    //}}}
    //{{{
    default:
      m_transform = OMX_DISPLAY_ROT0;
      break;
    //}}}
    }
  if (vflip)
     m_transform = OMX_DISPLAY_MIRROR_ROT180;

  if (m_omx_decoder.BadState())
    return false;

  cLog::log (LOGINFO1, "cOmxVideo::Open %p in:%x out:%x deint:%d hdmi:%d",
             m_omx_decoder.GetComponent(), m_omx_decoder.GetInputPort(), m_omx_decoder.GetOutputPort(),
             m_config.deinterlace, m_config.hdmi_clock_sync);

  float fAspect = m_config.hints.aspect ?
    (float)m_config.hints.aspect / (float)m_config.hints.width * (float)m_config.hints.height : 1.0f;
  m_pixel_aspect = fAspect / m_config.display_aspect;
  return true;
  }
//}}}

//{{{
void cOmxVideo::PortSettingsChangedLogger (OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode) {

  cLog::log (LOGNOTICE, "cOmxVideo::PortSettingsChanged %dx%d@%.2f int:%d deint:%d par:%.2f disp:%d lay:%d alpha:%d aspect:%d",
             port_image.format.video.nFrameWidth, port_image.format.video.nFrameHeight,
             port_image.format.video.xFramerate / (float)(1<<16),
             interlaceEMode, m_deinterlace,  m_pixel_aspect, m_config.display,
             m_config.layer, m_config.alpha, m_config.aspectMode);
  }
//}}}
//{{{
bool cOmxVideo::PortSettingsChanged() {

  cSingleLock lock (m_critSection);

  if (m_settings_changed)
    m_omx_decoder.DisablePort (m_omx_decoder.GetOutputPort(), true);

  OMX_PARAM_PORTDEFINITIONTYPE port_image;
  OMX_INIT_STRUCTURE(port_image);
  port_image.nPortIndex = m_omx_decoder.GetOutputPort();
  if (m_omx_decoder.GetParameter (OMX_IndexParamPortDefinition, &port_image) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamPortDefinition");

  OMX_CONFIG_POINTTYPE pixel_aspect;
  OMX_INIT_STRUCTURE(pixel_aspect);
  pixel_aspect.nPortIndex = m_omx_decoder.GetOutputPort();
  if (m_omx_decoder.GetParameter (OMX_IndexParamBrcmPixelAspectRatio, &pixel_aspect) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamBrcmPixelAspectRatio");

  if (pixel_aspect.nX && pixel_aspect.nY && !m_config.hints.forced_aspect) {
    float fAspect = (float)pixel_aspect.nX / (float)pixel_aspect.nY;
    m_pixel_aspect = fAspect / m_config.display_aspect;
    }

  if (m_settings_changed) {
    PortSettingsChangedLogger (port_image, -1);
    SetVideoRect();
    m_omx_decoder.EnablePort (m_omx_decoder.GetOutputPort(), true);
    return true;
    }

  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);
  interlace.nPortIndex = m_omx_decoder.GetOutputPort();
  m_omx_decoder.GetConfig (OMX_IndexConfigCommonInterlace, &interlace);

  if (m_config.deinterlace == VS_DEINTERLACEMODE_FORCE)
    m_deinterlace = true;
  else if (m_config.deinterlace == VS_DEINTERLACEMODE_OFF)
    m_deinterlace = false;
  else
    m_deinterlace = interlace.eMode != OMX_InterlaceProgressive;

  if (!m_omx_render.Initialize ("OMX.broadcom.video_render", OMX_IndexParamVideoInit))
    return false;

  m_omx_render.ResetEos();

  PortSettingsChangedLogger (port_image, interlace.eMode);

  if (!m_omx_sched.Initialize ("OMX.broadcom.video_scheduler", OMX_IndexParamVideoInit))
    return false;

  if (m_deinterlace)
    if (!m_omx_image_fx.Initialize ("OMX.broadcom.image_fx", OMX_IndexParamImageInit))
      return false;

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = m_omx_render.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_ALPHA | OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
  configDisplay.alpha = m_config.alpha;
  configDisplay.num = m_config.display;
  configDisplay.layer = m_config.layer;
  configDisplay.transform = m_transform;
  if (m_omx_render.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone) {
    cLog::log (LOGINFO1, "cOmxVideo::PortSettingsChanged OMX_IndexConfigDisplayRegion %d", m_transform);
    return false;
    }

  SetVideoRect();

  if (m_config.hdmi_clock_sync) {
    OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
    OMX_INIT_STRUCTURE(latencyTarget);
    latencyTarget.nPortIndex = m_omx_render.GetInputPort();
    latencyTarget.bEnabled = OMX_TRUE;
    latencyTarget.nFilter = 2;
    latencyTarget.nTarget = 4000;
    latencyTarget.nShift = 3;
    latencyTarget.nSpeedFactor = -135;
    latencyTarget.nInterFactor = 500;
    latencyTarget.nAdjCap = 20;

    if (m_omx_render.SetConfig (OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexConfigLatencyTarget");
      return false;
      }
    }

  if (m_deinterlace) {
    bool advanced_deinterlace = m_config.advanced_hd_deinterlace || port_image.format.video.nFrameWidth * port_image.format.video.nFrameHeight <= 576 * 720;
    if (!advanced_deinterlace) {
      // Image_fx assumed 3 frames of context. and simple deinterlace don't require this
      OMX_PARAM_U32TYPE extra_buffers;
      OMX_INIT_STRUCTURE(extra_buffers);
      extra_buffers.nU32 = -2;
      if (m_omx_image_fx.SetParameter (OMX_IndexParamBrcmExtraBuffers, &extra_buffers) != OMX_ErrorNone) {
        cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamBrcmExtraBuffers");
        return false;
        }
      }

    OMX_CONFIG_IMAGEFILTERPARAMSTYPE image_filter;
    OMX_INIT_STRUCTURE(image_filter);
    image_filter.nPortIndex = m_omx_image_fx.GetOutputPort();
    image_filter.nNumParams = 4;
    image_filter.nParams[0] = 3;
    image_filter.nParams[1] = 0; // default frame interval
    image_filter.nParams[2] = 0; // half framerate
    image_filter.nParams[3] = 1; // use qpus
    if (advanced_deinterlace)
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;
    else
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceFast;
    if (m_omx_image_fx.SetConfig (OMX_IndexConfigCommonImageFilterParameters, &image_filter) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexConfigCommonImageFilterParameters");
      return false;
      }
    }

  if (m_deinterlace) {
    m_omx_tunnel_decoder.Initialize (&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_image_fx, m_omx_image_fx.GetInputPort());
    m_omx_tunnel_image_fx.Initialize (&m_omx_image_fx, m_omx_image_fx.GetOutputPort(), &m_omx_sched, m_omx_sched.GetInputPort());
    }
  else
    m_omx_tunnel_decoder.Initialize (&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_sched, m_omx_sched.GetInputPort());
  m_omx_tunnel_sched.Initialize (&m_omx_sched, m_omx_sched.GetOutputPort(), &m_omx_render, m_omx_render.GetInputPort());
  m_omx_tunnel_clock.Initialize (m_omx_clock, m_omx_clock->GetInputPort() + 1, &m_omx_sched, m_omx_sched.GetOutputPort() + 1);
  if (m_omx_tunnel_clock.Establish() != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged m_omx_tunnel_clock.Establish");
    return false;
    }

  if (m_omx_tunnel_decoder.Establish() != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged m_omx_tunnel_decoder.Establish");
    return false;
    }

  if (m_deinterlace) {
    if (m_omx_tunnel_image_fx.Establish() != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged m_omx_tunnel_image_fx.Establish");
      return false;
      }
    if (m_omx_image_fx.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged m_omx_image_fx.SetStateForComponent");
      return false;
      }
    }

  if (m_omx_tunnel_sched.Establish() != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged m_omx_tunnel_sched.Establish");
    return false;
    }
  if (m_omx_sched.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged - m_omx_sched.SetStateForComponent");
    return false;
    }
  if (m_omx_render.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged - m_omx_render.SetStateForComponent");
    return false;
    }

  m_settings_changed = true;
  return true;
  }
//}}}

//{{{
unsigned int cOmxVideo::GetSize() {
  cSingleLock lock (m_critSection);
  return m_omx_decoder.GetInputBufferSize();
  }
//}}}
//{{{
int cOmxVideo::GetInputBufferSize() {
  cSingleLock lock (m_critSection);
  return m_omx_decoder.GetInputBufferSize();
  }
//}}}
//{{{
unsigned int cOmxVideo::GetFreeSpace() {
  cSingleLock lock (m_critSection);
  return m_omx_decoder.GetInputBufferSpace();
  }
//}}}

//{{{
void cOmxVideo::SetDropState (bool bDrop) {
  m_drop_state = bDrop;
  }
//}}}
//{{{
void cOmxVideo::SetVideoRect (const CRect& SrcRect, const CRect& DestRect) {
  m_config.src_rect = SrcRect;
  m_config.dst_rect = DestRect;
  SetVideoRect();
  }
//}}}
//{{{
void cOmxVideo::SetVideoRect (int aspectMode) {
  m_config.aspectMode = aspectMode;
  SetVideoRect();
  }
//}}}
//{{{
void cOmxVideo::SetVideoRect() {

  if (!m_is_open)
    return;

  cSingleLock lock (m_critSection);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = m_omx_render.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT | OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_SRC_RECT | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_PIXEL);
  configDisplay.noaspect = m_config.aspectMode == 3 ? OMX_TRUE : OMX_FALSE;
  configDisplay.mode = m_config.aspectMode == 2 ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;

  configDisplay.src_rect.x_offset = (int)(m_config.src_rect.x1 + 0.5f);
  configDisplay.src_rect.y_offset = (int)(m_config.src_rect.y1 + 0.5f);
  configDisplay.src_rect.width = (int)(m_config.src_rect.Width() + 0.5f);
  configDisplay.src_rect.height = (int)(m_config.src_rect.Height() + 0.5f);

  if (m_config.dst_rect.x2 > m_config.dst_rect.x1 && m_config.dst_rect.y2 > m_config.dst_rect.y1) {
    configDisplay.set = (OMX_DISPLAYSETTYPE)(configDisplay.set | OMX_DISPLAY_SET_DEST_RECT);
    configDisplay.fullscreen = OMX_FALSE;
    if (m_config.aspectMode != 1 && m_config.aspectMode != 2 && m_config.aspectMode != 3)
      configDisplay.noaspect = OMX_TRUE;
    configDisplay.dest_rect.x_offset = (int)(m_config.dst_rect.x1 + 0.5f);
    configDisplay.dest_rect.y_offset = (int)(m_config.dst_rect.y1 + 0.5f);
    configDisplay.dest_rect.width = (int)(m_config.dst_rect.Width() + 0.5f);
    configDisplay.dest_rect.height = (int)(m_config.dst_rect.Height() + 0.5f);
    }
  else
    configDisplay.fullscreen = OMX_TRUE;

  if (configDisplay.noaspect == OMX_FALSE && m_pixel_aspect != 0.0f) {
    AVRational aspect = av_d2q (m_pixel_aspect, 100);
    configDisplay.pixel_x = aspect.num;
    configDisplay.pixel_y = aspect.den;
    }
  else {
    configDisplay.pixel_x = 0;
    configDisplay.pixel_y = 0;
    }

  if (m_omx_render.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexConfigDisplayRegion");
  }
//}}}
//{{{
void cOmxVideo::SetAlpha (int alpha) {

  if (!m_is_open)
    return;

  cSingleLock lock (m_critSection);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = m_omx_render.GetInputPort();
  configDisplay.set = OMX_DISPLAY_SET_ALPHA;
  configDisplay.alpha = alpha;
  if (m_omx_render.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::SetAlpha");
  }
//}}}

//{{{
bool cOmxVideo::Decode (uint8_t* data, int size, double dts, double pts) {

  cSingleLock lock (m_critSection);

  if (m_drop_state || !m_is_open )
    return true;

  auto demuxer_content = data;
  unsigned int demuxer_bytes = (unsigned int)size;
  OMX_U32 nFlags = 0;
  if (m_setStartTime) {
    nFlags |= OMX_BUFFERFLAG_STARTTIME;
    cLog::log (LOGINFO1, "cOmxVideo::Decode setStartTime:%f", (pts == DVD_NOPTS_VALUE ? 0.0 : pts) / DVD_TIME_BASE);
    m_setStartTime = false;
    }
  if ((pts == DVD_NOPTS_VALUE) && (dts == DVD_NOPTS_VALUE))
    nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  else if (pts == DVD_NOPTS_VALUE)
    nFlags |= OMX_BUFFERFLAG_TIME_IS_DTS;

  while (demuxer_bytes) {
    // 500ms timeout
    auto omx_buffer = m_omx_decoder.GetInputBuffer (500);
    if (omx_buffer == NULL) {
      cLog::log (LOGERROR, "cOmxVideo::Decode timeout");
      return false;
      }
    omx_buffer->nFlags = nFlags;
    omx_buffer->nOffset = 0;
    omx_buffer->nTimeStamp = toOmxTime ((uint64_t)(pts != DVD_NOPTS_VALUE ? pts : dts != DVD_NOPTS_VALUE ? dts : 0));
    omx_buffer->nFilledLen = std::min ((OMX_U32)demuxer_bytes, omx_buffer->nAllocLen);
    memcpy (omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);
    demuxer_bytes -= omx_buffer->nFilledLen;
    demuxer_content += omx_buffer->nFilledLen;
    if (demuxer_bytes == 0)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    if (m_omx_decoder.EmptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::Decode OMX_EmptyThisBuffer");
      m_omx_decoder.DecoderEmptyBufferDone (m_omx_decoder.GetComponent(), omx_buffer);
      return false;
      }

    if (m_omx_decoder.WaitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone) {
      if (!PortSettingsChanged()) {
        cLog::log (LOGERROR, "cOmxVideo::Decode PortSettingsChanged");
        return false;
        }
      }
    if (m_omx_decoder.WaitForEvent (OMX_EventParamOrConfigChanged, 0) == OMX_ErrorNone)
      if (!PortSettingsChanged())
        cLog::log (LOGERROR, "OMXVideo::Decode PortSettingsChanged (EventParamOrConfigChanged)");
    }

  return true;
  }
//}}}
//{{{
void cOmxVideo::Reset() {

  cSingleLock lock (m_critSection);

  if (!m_is_open)
    return;

  m_setStartTime = true;
  m_omx_decoder.FlushInput();
  if (m_deinterlace)
    m_omx_image_fx.FlushInput();

  m_omx_render.ResetEos();
  }
//}}}

//{{{
bool cOmxVideo::IsEOS() {

  cSingleLock lock (m_critSection);
  if (!m_is_open)
    return true;

  if (!m_failed_eos && !m_omx_render.IsEOS())
    return false;

  if (m_submitted_eos) {
    cLog::log (LOGINFO, "cOmxVideo::IsEOS");
    m_submitted_eos = false;
    }

  return true;
  }
//}}}
//{{{
void cOmxVideo::SubmitEOS() {

  cSingleLock lock (m_critSection);

  if (!m_is_open)
    return;

  m_submitted_eos = true;
  m_failed_eos = false;

  auto omx_buffer = m_omx_decoder.GetInputBuffer (1000);
  if (omx_buffer == NULL) {
    cLog::log(LOGERROR, "cOmxVideo::SubmitEOS GetInputBuffer");
    m_failed_eos = true;
    return;
    }
  omx_buffer->nOffset = 0;
  omx_buffer->nFilledLen = 0;
  omx_buffer->nTimeStamp = toOmxTime (0LL);
  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  if (m_omx_decoder.EmptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
    cLog::log (LOGERROR, "cOmxVideo::SubmitEOS OMX_EmptyThisBuffer");
    m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
    return;
    }

  cLog::log (LOGINFO, "cOmxVideo::SubmitEOS");
  }
//}}}

// private
//{{{
void cOmxVideo::Close() {

  cSingleLock lock (m_critSection);

  m_omx_tunnel_clock.Deestablish();
  m_omx_tunnel_decoder.Deestablish();
  if (m_deinterlace)
    m_omx_tunnel_image_fx.Deestablish();
  m_omx_tunnel_sched.Deestablish();

  m_omx_decoder.FlushInput();

  m_omx_sched.Deinitialize();
  m_omx_decoder.Deinitialize();
  if (m_deinterlace)
    m_omx_image_fx.Deinitialize();
  m_omx_render.Deinitialize();

  m_is_open = false;
  m_video_codec_name = "";
  m_deinterlace = false;
  m_av_clock = NULL;
  }
//}}}
