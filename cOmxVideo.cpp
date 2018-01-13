// OMXVideo.cpp
//{{{  includes
#include <sys/time.h>
#include <inttypes.h>

#include "cVideo.h"

#include "cOmxStreamInfo.h"
#include "../shared/utils/cLog.h"

using namespace std;
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
cOmxVideo::~cOmxVideo() {
  close();
  }
//}}}

//{{{
bool cOmxVideo::sendDecoderConfig() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if ((mConfig.hints.extrasize > 0) && (mConfig.hints.extradata != NULL)) {
    auto omx_buffer = mOmxDecoder.GetInputBuffer();
    if (omx_buffer == NULL) {
      cLog::log (LOGERROR, "cOmxVideo::SendDecoderConfig buffer error");
      return false;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = std::min ((OMX_U32)mConfig.hints.extrasize, omx_buffer->nAllocLen);
    memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy ((unsigned char*)omx_buffer->pBuffer, mConfig.hints.extradata, omx_buffer->nFilledLen);

    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mOmxDecoder.EmptyThisBuffer(omx_buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::SendDecoderConfig OMX_EmptyThisBuffer()");
      mOmxDecoder.DecoderEmptyBufferDone (mOmxDecoder.GetComponent(), omx_buffer);
      return false;
      }
    }

  return true;
  }
//}}}
//{{{
bool cOmxVideo::naluFormatStartCodes (enum AVCodecID codec, uint8_t *in_extradata, int in_extrasize) {
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
unsigned int cOmxVideo::getSize() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mOmxDecoder.GetInputBufferSize();
  }
//}}}
//{{{
int cOmxVideo::getInputBufferSize() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mOmxDecoder.GetInputBufferSize();
  }
//}}}
//{{{
unsigned int cOmxVideo::getFreeSpace() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mOmxDecoder.GetInputBufferSpace();
  }
//}}}
//{{{
bool cOmxVideo::isEOS() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos && !mOmxrender.IsEOS())
    return false;

  if (mSubmittedEos) {
    cLog::log (LOGINFO, "isEOS");
    mSubmittedEos = false;
    }

  return true;
  }
//}}}

//{{{
void cOmxVideo::setAlpha (int alpha) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxrender.GetInputPort();
  configDisplay.set = OMX_DISPLAY_SET_ALPHA;
  configDisplay.alpha = alpha;
  if (mOmxrender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::SetAlpha");
  }
//}}}
//{{{
void cOmxVideo::setVideoRect() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxrender.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT | OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_SRC_RECT | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_PIXEL);
  configDisplay.noaspect = mConfig.aspectMode == 3 ? OMX_TRUE : OMX_FALSE;
  configDisplay.mode = mConfig.aspectMode == 2 ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;

  configDisplay.src_rect.x_offset = (int)(mConfig.src_rect.x1 + 0.5f);
  configDisplay.src_rect.y_offset = (int)(mConfig.src_rect.y1 + 0.5f);
  configDisplay.src_rect.width = (int)(mConfig.src_rect.Width() + 0.5f);
  configDisplay.src_rect.height = (int)(mConfig.src_rect.Height() + 0.5f);

  if (mConfig.dst_rect.x2 > mConfig.dst_rect.x1 && mConfig.dst_rect.y2 > mConfig.dst_rect.y1) {
    configDisplay.set = (OMX_DISPLAYSETTYPE)(configDisplay.set | OMX_DISPLAY_SET_DEST_RECT);
    configDisplay.fullscreen = OMX_FALSE;
    if (mConfig.aspectMode != 1 && mConfig.aspectMode != 2 && mConfig.aspectMode != 3)
      configDisplay.noaspect = OMX_TRUE;
    configDisplay.dest_rect.x_offset = (int)(mConfig.dst_rect.x1 + 0.5f);
    configDisplay.dest_rect.y_offset = (int)(mConfig.dst_rect.y1 + 0.5f);
    configDisplay.dest_rect.width = (int)(mConfig.dst_rect.Width() + 0.5f);
    configDisplay.dest_rect.height = (int)(mConfig.dst_rect.Height() + 0.5f);
    }
  else
    configDisplay.fullscreen = OMX_TRUE;

  if (configDisplay.noaspect == OMX_FALSE && mPixelAspect != 0.0f) {
    AVRational aspect = av_d2q (mPixelAspect, 100);
    configDisplay.pixel_x = aspect.num;
    configDisplay.pixel_y = aspect.den;
    }
  else {
    configDisplay.pixel_x = 0;
    configDisplay.pixel_y = 0;
    }

  if (mOmxrender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexConfigDisplayRegion");
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (int aspectMode) {

  mConfig.aspectMode = aspectMode;
  setVideoRect();
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (const CRect& SrcRect, const CRect& DestRect) {

  mConfig.src_rect = SrcRect;
  mConfig.dst_rect = DestRect;
  setVideoRect();
  }
//}}}

//{{{
bool cOmxVideo::open (cOmxClock* clock, const cOmxVideoConfig &config) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  bool vflip = false;
  close();

  std::string decoder_name;
  mSettingsChanged = false;
  mSetStartTime = true;
  mConfig = config;
  mVideoCodecName = "";
  mCodingType = OMX_VIDEO_CodingUnused;
  mSubmittedEos = false;
  mFailedEos = false;

  if (!mConfig.hints.width || !mConfig.hints.height)
    return false;

  switch (mConfig.hints.codec) {
    //{{{
    case AV_CODEC_ID_H264: {
      switch(mConfig.hints.profile) {
        case FF_PROFILE_H264_BASELINE:
          // (role name) video_decoder.avc
          // H.264 Baseline profile
          decoder_name = OMX_H264BASE_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_H264_MAIN:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264MAIN_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_H264_HIGH:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_UNKNOWN:
          decoder_name = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        default:
          decoder_name = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        }
      }
    if (mConfig.allow_mvc && mCodingType == OMX_VIDEO_CodingAVC) {
      mCodingType = OMX_VIDEO_CodingMVC;
      mVideoCodecName = "omx-mvc";
      }
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_MPEG4:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      mCodingType = OMX_VIDEO_CodingMPEG4;
      mVideoCodecName = "omx-mpeg4";
      break;
    //}}}
    case AV_CODEC_ID_MPEG1VIDEO:
    //{{{
    case AV_CODEC_ID_MPEG2VIDEO:
      // (role name) video_decoder.mpeg2
      // MPEG-2
      decoder_name = OMX_MPEG2V_DECODER;
      mCodingType = OMX_VIDEO_CodingMPEG2;
      mVideoCodecName = "omx-mpeg2";
      break;
    //}}}
    //{{{
    case AV_CODEC_ID_H263:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      mCodingType = OMX_VIDEO_CodingMPEG4;
      mVideoCodecName = "omx-h263";
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
      mCodingType = OMX_VIDEO_CodingVP6;
      mVideoCodecName = "omx-vp6";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_VP8:
      // (role name) video_decoder.vp8
      // VP8
      decoder_name = OMX_VP8_DECODER;
      mCodingType = OMX_VIDEO_CodingVP8;
      mVideoCodecName = "omx-vp8";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_THEORA:
      // (role name) video_decoder.theora
      // theora
      decoder_name = OMX_THEORA_DECODER;
      mCodingType = OMX_VIDEO_CodingTheora;
      mVideoCodecName = "omx-theora";
    break;
    //}}}
    case AV_CODEC_ID_MJPEG:
    //{{{
    case AV_CODEC_ID_MJPEGB:
      // (role name) video_decoder.mjpg
      // mjpg
      decoder_name = OMX_MJPEG_DECODER;
      mCodingType = OMX_VIDEO_CodingMJPEG;
      mVideoCodecName = "omx-mjpeg";
    break;
    //}}}
    case AV_CODEC_ID_VC1:
    //{{{
    case AV_CODEC_ID_WMV3:
      // (role name) video_decoder.vc1
      // VC-1, WMV9
      decoder_name = OMX_VC1_DECODER;
      mCodingType = OMX_VIDEO_CodingWMV;
      mVideoCodecName = "omx-vc1";
      break;
    //}}}
    //{{{
    default:
      printf ("Vcodec id unknown: %x\n", mConfig.hints.codec);
      return false;
    break;
    //}}}
    }

  if (!mOmxDecoder.Initialize (decoder_name, OMX_IndexParamVideoInit))
    return false;
  if (clock == NULL)
    return false;

  mAvClock = clock;
  mOmxClock = mAvClock->getOmxClock();
  if (mOmxClock->GetComponent() == NULL) {
    //{{{  noc clock return
    mAvClock = NULL;
    mOmxClock = NULL;
    return false;
    }
    //}}}

  if (mOmxDecoder.SetStateForComponent (OMX_StateIdle) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open SetStateForComponent");
    return false;
    }
    //}}}

  OMX_VIDEO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = mOmxDecoder.GetInputPort();
  formatType.eCompressionFormat = mCodingType;
  if (mConfig.hints.fpsscale > 0 && mConfig.hints.fpsrate > 0)
    formatType.xFramerate = (long long)(1<<16)*mConfig.hints.fpsrate / mConfig.hints.fpsscale;
  else
    formatType.xFramerate = 25 * (1<<16);
  if (mOmxDecoder.SetParameter (OMX_IndexParamVideoPortFormat, &formatType) != OMX_ErrorNone)
    return false;

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = mOmxDecoder.GetInputPort();
  if (mOmxDecoder.GetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamPortDefinition");
    return false;
    }
    //}}}

  portParam.nPortIndex = mOmxDecoder.GetInputPort();
  portParam.nBufferCountActual = mConfig.fifo_size ? mConfig.fifo_size * 1024 * 1024 / portParam.nBufferSize : 80;
  portParam.format.video.nFrameWidth  = mConfig.hints.width;
  portParam.format.video.nFrameHeight = mConfig.hints.height;
  if (mOmxDecoder.SetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamPortDefinition");
    return false;
    }
    //}}}

  // request portsettingschanged on aspect ratio change
  OMX_CONFIG_REQUESTCALLBACKTYPE notifications;
  OMX_INIT_STRUCTURE(notifications);
  notifications.nPortIndex = mOmxDecoder.GetOutputPort();
  notifications.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
  notifications.bEnable = OMX_TRUE;
  if (mOmxDecoder.SetParameter ((OMX_INDEXTYPE)OMX_IndexConfigRequestCallback, &notifications) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexConfigRequestCallback");
    return false;
    }
    //}}}

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
  OMX_INIT_STRUCTURE(concanParam);
  concanParam.bStartWithValidFrame = OMX_TRUE;
  if (mOmxDecoder.SetParameter (OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamBrcmVideoDecodeErrorConcealment");
    return false;
    }
    //}}}

  if (naluFormatStartCodes (mConfig.hints.codec, (uint8_t *)mConfig.hints.extradata, mConfig.hints.extrasize)) {
    OMX_NALSTREAMFORMATTYPE nalStreamFormat;
    OMX_INIT_STRUCTURE(nalStreamFormat);
    nalStreamFormat.nPortIndex = mOmxDecoder.GetInputPort();
    nalStreamFormat.eNaluFormat = OMX_NaluFormatStartCodes;
    if (mOmxDecoder.SetParameter ((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStreamFormat) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::Open OMX_IndexParamNalStreamFormatSelect");
      return false;
      }
      //}}}
    }

  // Alloc buffers for the omx intput port.
  if (mOmxDecoder.AllocInputBuffers() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open AllocInputBuffers");
    return false;
    }
    //}}}
  if (mOmxDecoder.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::Open SetStateForComponent");
    return false;
    }
    //}}}

  sendDecoderConfig();

  mDropState = false;
  mSetStartTime = true;
  switch (mConfig.hints.orientation) {
    //{{{
    case 90:
      mTransform = OMX_DISPLAY_ROT90;
      break;
    //}}}
    //{{{
    case 180:
      mTransform = OMX_DISPLAY_ROT180;
      break;
    //}}}
    //{{{
    case 270:
      mTransform = OMX_DISPLAY_ROT270;
      break;
    //}}}
    //{{{
    case 1:
      mTransform = OMX_DISPLAY_MIRROR_ROT0;
      break;
    //}}}
    //{{{
    case 91:
      mTransform = OMX_DISPLAY_MIRROR_ROT90;
      break;
    //}}}
    //{{{
    case 181:
      mTransform = OMX_DISPLAY_MIRROR_ROT180;
      break;
    //}}}
    //{{{
    case 271:
      mTransform = OMX_DISPLAY_MIRROR_ROT270;
      break;
    //}}}
    //{{{
    default:
      mTransform = OMX_DISPLAY_ROT0;
      break;
    //}}}
    }
  if (vflip)
     mTransform = OMX_DISPLAY_MIRROR_ROT180;

  if (mOmxDecoder.BadState())
    return false;

  cLog::log (LOGINFO1, "cOmxVideo::Open %p in:%x out:%x deint:%d hdmi:%d",
             mOmxDecoder.GetComponent(), mOmxDecoder.GetInputPort(), mOmxDecoder.GetOutputPort(),
             mConfig.deinterlace, mConfig.hdmi_clock_sync);

  float aspect = mConfig.hints.aspect ?
    (float)mConfig.hints.aspect / (float)mConfig.hints.width * (float)mConfig.hints.height : 1.0f;
  mPixelAspect = aspect / mConfig.display_aspect;

  return true;
  }
//}}}
//{{{
bool cOmxVideo::portSettingsChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mSettingsChanged)
    mOmxDecoder.DisablePort (mOmxDecoder.GetOutputPort(), true);

  OMX_PARAM_PORTDEFINITIONTYPE port_image;
  OMX_INIT_STRUCTURE(port_image);
  port_image.nPortIndex = mOmxDecoder.GetOutputPort();
  if (mOmxDecoder.GetParameter (OMX_IndexParamPortDefinition, &port_image) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamPortDefinition");

  OMX_CONFIG_POINTTYPE pixel_aspect;
  OMX_INIT_STRUCTURE(pixel_aspect);
  pixel_aspect.nPortIndex = mOmxDecoder.GetOutputPort();
  if (mOmxDecoder.GetParameter (OMX_IndexParamBrcmPixelAspectRatio, &pixel_aspect) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamBrcmPixelAspectRatio");

  if (pixel_aspect.nX && pixel_aspect.nY && !mConfig.hints.forced_aspect) {
    //{{{  aspect changed
    float fAspect = (float)pixel_aspect.nX / (float)pixel_aspect.nY;
    mPixelAspect = fAspect / mConfig.display_aspect;
    }
    //}}}
  if (mSettingsChanged) {
    //{{{  settings changed
    portSettingsChangedLogger (port_image, -1);
    setVideoRect();
    mOmxDecoder.EnablePort (mOmxDecoder.GetOutputPort(), true);
    return true;
    }
    //}}}

  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);
  interlace.nPortIndex = mOmxDecoder.GetOutputPort();
  mOmxDecoder.GetConfig (OMX_IndexConfigCommonInterlace, &interlace);

  if (mConfig.deinterlace == VS_DEINTERLACEMODE_FORCE)
    mDeinterlace = true;
  else if (mConfig.deinterlace == VS_DEINTERLACEMODE_OFF)
    mDeinterlace = false;
  else
    mDeinterlace = interlace.eMode != OMX_InterlaceProgressive;

  if (!mOmxrender.Initialize ("OMX.broadcom.video_render", OMX_IndexParamVideoInit))
    return false;

  mOmxrender.ResetEos();
  portSettingsChangedLogger (port_image, interlace.eMode);

  if (!mOmxsched.Initialize ("OMX.broadcom.video_scheduler", OMX_IndexParamVideoInit))
    return false;

  if (mDeinterlace)
    if (!mOmximage_fx.Initialize ("OMX.broadcom.image_fx", OMX_IndexParamImageInit))
      return false;

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxrender.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_ALPHA | OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
  configDisplay.alpha = mConfig.alpha;
  configDisplay.num = mConfig.display;
  configDisplay.layer = mConfig.layer;
  configDisplay.transform = mTransform;
  if (mOmxrender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGINFO1, "cOmxVideo::PortSettingsChanged OMX_IndexConfigDisplayRegion %d", mTransform);
    return false;
    }
    //}}}

  setVideoRect();

  if (mConfig.hdmi_clock_sync) {
    //{{{  config latency
    OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
    OMX_INIT_STRUCTURE(latencyTarget);
    latencyTarget.nPortIndex = mOmxrender.GetInputPort();
    latencyTarget.bEnabled = OMX_TRUE;
    latencyTarget.nFilter = 2;
    latencyTarget.nTarget = 4000;
    latencyTarget.nShift = 3;
    latencyTarget.nSpeedFactor = -135;
    latencyTarget.nInterFactor = 500;
    latencyTarget.nAdjCap = 20;

    if (mOmxrender.SetConfig (OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexConfigLatencyTarget");
      return false;
      }
      //}}}
    }
    //}}}
  if (mDeinterlace) {
    //{{{  setup deinterlace
    bool advanced_deinterlace = mConfig.advanced_hd_deinterlace ||
               (port_image.format.video.nFrameWidth * port_image.format.video.nFrameHeight <= (576 * 720));
    if (!advanced_deinterlace) {
      // Image_fx assumed 3 frames of context. and simple deinterlace don't require this
      OMX_PARAM_U32TYPE extra_buffers;
      OMX_INIT_STRUCTURE(extra_buffers);
      extra_buffers.nU32 = -2;
      if (mOmximage_fx.SetParameter (OMX_IndexParamBrcmExtraBuffers, &extra_buffers) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexParamBrcmExtraBuffers");
        return false;
        }
        //}}}
      }

    OMX_CONFIG_IMAGEFILTERPARAMSTYPE image_filter;
    OMX_INIT_STRUCTURE(image_filter);
    image_filter.nPortIndex = mOmximage_fx.GetOutputPort();
    image_filter.nNumParams = 4;
    image_filter.nParams[0] = 3;
    image_filter.nParams[1] = 0; // default frame interval
    image_filter.nParams[2] = 0; // half framerate
    image_filter.nParams[3] = 1; // use qpus

    if (advanced_deinterlace)
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;
    else
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceFast;

    if (mOmximage_fx.SetConfig (OMX_IndexConfigCommonImageFilterParameters, &image_filter) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged OMX_IndexConfigCommonImageFilterParameters");
      return false;
      }
      //}}}

    mOmxTunneldecoder.Initialize (&mOmxDecoder, mOmxDecoder.GetOutputPort(), &mOmximage_fx, mOmximage_fx.GetInputPort());
    mOmxTunnelimage_fx.Initialize (&mOmximage_fx, mOmximage_fx.GetOutputPort(), &mOmxsched, mOmxsched.GetInputPort());
    }
    //}}}
  else
    mOmxTunneldecoder.Initialize (&mOmxDecoder, mOmxDecoder.GetOutputPort(), &mOmxsched, mOmxsched.GetInputPort());

  mOmxTunnelsched.Initialize (&mOmxsched, mOmxsched.GetOutputPort(), &mOmxrender, mOmxrender.GetInputPort());
  mOmxTunnelclock.Initialize (mOmxClock, mOmxClock->GetInputPort() + 1, &mOmxsched, mOmxsched.GetOutputPort() + 1);
  if (mOmxTunnelclock.Establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged mOmxTunnelclock.Establish");
    return false;
    }
    //}}}
  if (mOmxTunneldecoder.Establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged mOmxTunneldecoder.Establish");
    return false;
    }
    //}}}
  if (mDeinterlace) {
    if (mOmxTunnelimage_fx.Establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged mOmxTunnelimage_fx.Establish");
      return false;
      }
      //}}}
    if (mOmximage_fx.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged mOmximage_fx.SetStateForComponent");
      return false;
      }
      //}}}
    }
  if (mOmxTunnelsched.Establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged mOmxTunnelsched.Establish");
    return false;
    }
    //}}}
  if (mOmxsched.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged - mOmxsched.SetStateForComponent");
    return false;
    }
    //}}}
  if (mOmxrender.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::PortSettingsChanged - mOmxrender.SetStateForComponent");
    return false;
    }
    //}}}

  mSettingsChanged = true;
  return true;
  }
//}}}
//{{{
bool cOmxVideo::decode (uint8_t* data, int size, double dts, double pts) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  auto demuxer_content = data;
  unsigned int demuxer_bytes = (unsigned int)size;
  OMX_U32 nFlags = 0;
  if (mSetStartTime) {
    nFlags |= OMX_BUFFERFLAG_STARTTIME;
    cLog::log (LOGINFO1, "cOmxVideo::Decode setStartTime:%f",
                         (pts == DVD_NOPTS_VALUE ? 0.0 : pts) / 1000000.f);
    mSetStartTime = false;
    }

  if ((pts == DVD_NOPTS_VALUE) && (dts == DVD_NOPTS_VALUE))
    nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  else if (pts == DVD_NOPTS_VALUE)
    nFlags |= OMX_BUFFERFLAG_TIME_IS_DTS;

  while (demuxer_bytes) {
    // 500ms timeout
    auto omx_buffer = mOmxDecoder.GetInputBuffer (500);
    if (omx_buffer == NULL) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::Decode timeout");
      return false;
      }
      //}}}

    omx_buffer->nFlags = nFlags;
    omx_buffer->nOffset = 0;
    omx_buffer->nTimeStamp = toOmxTime ((uint64_t)(pts != DVD_NOPTS_VALUE ? pts : dts != DVD_NOPTS_VALUE ? dts : 0));
    omx_buffer->nFilledLen = std::min ((OMX_U32)demuxer_bytes, omx_buffer->nAllocLen);
    memcpy (omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);
    demuxer_bytes -= omx_buffer->nFilledLen;
    demuxer_content += omx_buffer->nFilledLen;
    if (demuxer_bytes == 0)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    if (mOmxDecoder.EmptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::Decode OMX_EmptyThisBuffer");
      mOmxDecoder.DecoderEmptyBufferDone (mOmxDecoder.GetComponent(), omx_buffer);
      return false;
      }
      //}}}

    if (mOmxDecoder.WaitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone) {
      if (!portSettingsChanged()) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxVideo::Decode PortSettingsChanged");
        return false;
        }
        //}}}
      }
    if (mOmxDecoder.WaitForEvent (OMX_EventParamOrConfigChanged, 0) == OMX_ErrorNone)
      if (!portSettingsChanged())
        cLog::log (LOGERROR, "OMXVideo::Decode PortSettingsChanged (EventParamOrConfigChanged)");
    }

  return true;
  }
//}}}
//{{{
void cOmxVideo::submitEOS() {

  cLog::log (LOGINFO, "submitEOS");
  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSubmittedEos = true;
  mFailedEos = false;

  auto omxBuffer = mOmxDecoder.GetInputBuffer (1000);
  if (omxBuffer == NULL) {
    //{{{  error return
    cLog::log(LOGERROR, "cOmxVideo::SubmitEOS GetInputBuffer");
    mFailedEos = true;
    return;
    }
    //}}}

  omxBuffer->nOffset = 0;
  omxBuffer->nFilledLen = 0;
  omxBuffer->nTimeStamp = toOmxTime (0LL);
  omxBuffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  if (mOmxDecoder.EmptyThisBuffer (omxBuffer) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::SubmitEOS OMX_EmptyThisBuffer");
    mOmxDecoder.DecoderEmptyBufferDone(mOmxDecoder.GetComponent(), omxBuffer);
    return;
    }
    //}}}
  }
//}}}
//{{{
void cOmxVideo::reset() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSetStartTime = true;
  mOmxDecoder.FlushInput();
  if (mDeinterlace)
    mOmximage_fx.FlushInput();

  mOmxrender.ResetEos();
  }
//}}}
//{{{
void cOmxVideo::close() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mOmxTunnelclock.Deestablish();
  mOmxTunneldecoder.Deestablish();
  if (mDeinterlace)
    mOmxTunnelimage_fx.Deestablish();
  mOmxTunnelsched.Deestablish();

  mOmxDecoder.FlushInput();

  mOmxsched.Deinitialize();
  mOmxDecoder.Deinitialize();
  if (mDeinterlace)
    mOmximage_fx.Deinitialize();
  mOmxrender.Deinitialize();

  mVideoCodecName = "";
  mDeinterlace = false;
  mAvClock = NULL;
  }
//}}}

// private
//{{{
void cOmxVideo::portSettingsChangedLogger (OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode) {

  cLog::log (LOGINFO, "portSettings %dx%d@%.2f int:%d deint:%d par:%.2f disp:%d lay:%d alpha:%d aspect:%d",
             port_image.format.video.nFrameWidth, port_image.format.video.nFrameHeight,
             port_image.format.video.xFramerate / (float)(1<<16),
             interlaceEMode, mDeinterlace,  mPixelAspect, mConfig.display,
             mConfig.layer, mConfig.alpha, mConfig.aspectMode);
  }
//}}}
