// OMXVideo.cpp
//{{{  includes
#include <sys/time.h>
#include <inttypes.h>

#include "cVideo.h"

#include "cOmxStreamInfo.h"
#include "../shared/utils/utils.h"
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
bool cOmxVideo::sendDecoderConfig() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if ((mConfig.mHints.extrasize > 0) && (mConfig.mHints.extradata != NULL)) {
    auto omx_buffer = mOmxDecoder.GetInputBuffer();
    if (omx_buffer == NULL) {
      cLog::log (LOGERROR, "cOmxVideo::SendDecoderConfig buffer error");
      return false;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = std::min ((OMX_U32)mConfig.mHints.extrasize, omx_buffer->nAllocLen);
    memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy ((unsigned char*)omx_buffer->pBuffer, mConfig.mHints.extradata, omx_buffer->nFilledLen);

    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mOmxDecoder.EmptyThisBuffer(omx_buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxVideo::sendDecoderConfig OMX_EmptyThisBuffer()");
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
bool cOmxVideo::isEOS() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos && !mOmxRender.IsEOS())
    return false;

  if (mSubmittedEos) {
    cLog::log (LOGINFO, "isEOS");
    mSubmittedEos = false;
    }

  return true;
  }
//}}}
//{{{
int cOmxVideo::getInputBufferSize() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mOmxDecoder.GetInputBufferSize();
  }
//}}}
//{{{
unsigned int cOmxVideo::GetInputBufferSpace() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mOmxDecoder.GetInputBufferSpace();
  }
//}}}

//{{{
void cOmxVideo::setAlpha (int alpha) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxRender.GetInputPort();
  configDisplay.set = OMX_DISPLAY_SET_ALPHA;
  configDisplay.alpha = alpha;
  if (mOmxRender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::setAlpha");
  }
//}}}
//{{{
void cOmxVideo::setVideoRect() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxRender.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT | OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_SRC_RECT | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_PIXEL);
  configDisplay.noaspect = mConfig.mAspectMode == 3 ? OMX_TRUE : OMX_FALSE;
  configDisplay.mode = mConfig.mAspectMode == 2 ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;

  configDisplay.src_rect.x_offset = (int)(mConfig.mSrcRect.x1 + 0.5f);
  configDisplay.src_rect.y_offset = (int)(mConfig.mSrcRect.y1 + 0.5f);
  configDisplay.src_rect.width = (int)(mConfig.mSrcRect.Width() + 0.5f);
  configDisplay.src_rect.height = (int)(mConfig.mSrcRect.Height() + 0.5f);

  if (mConfig.mDstRect.x2 > mConfig.mDstRect.x1 && mConfig.mDstRect.y2 > mConfig.mDstRect.y1) {
    configDisplay.set = (OMX_DISPLAYSETTYPE)(configDisplay.set | OMX_DISPLAY_SET_DEST_RECT);
    configDisplay.fullscreen = OMX_FALSE;
    if (mConfig.mAspectMode != 1 && mConfig.mAspectMode != 2 && mConfig.mAspectMode != 3)
      configDisplay.noaspect = OMX_TRUE;
    configDisplay.dest_rect.x_offset = (int)(mConfig.mDstRect.x1 + 0.5f);
    configDisplay.dest_rect.y_offset = (int)(mConfig.mDstRect.y1 + 0.5f);
    configDisplay.dest_rect.width = (int)(mConfig.mDstRect.Width() + 0.5f);
    configDisplay.dest_rect.height = (int)(mConfig.mDstRect.Height() + 0.5f);
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

  if (mOmxRender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::setVideoRect");
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (int aspectMode) {

  mConfig.mAspectMode = aspectMode;
  setVideoRect();
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (const CRect& srcRect, const CRect& dstRect) {

  mConfig.mSrcRect = srcRect;
  mConfig.mDstRect = dstRect;
  setVideoRect();
  }
//}}}

//{{{
bool cOmxVideo::open (cOmxClock* avClock, const cOmxVideoConfig &config) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  close();

  mSettingsChanged = false;
  mSetStartTime = true;
  mConfig = config;
  mSubmittedEos = false;
  mFailedEos = false;

  string decoderName;
  mCodingType = OMX_VIDEO_CodingUnused;
  mVideoCodecName = "";
  bool vflip = false;
  switch (mConfig.mHints.codec) {
    //{{{
    case AV_CODEC_ID_H264: {
      switch(mConfig.mHints.profile) {
        case FF_PROFILE_H264_BASELINE:
          // (role name) video_decoder.avc
          // H.264 Baseline profile
          decoderName = OMX_H264BASE_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_H264_MAIN:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoderName = OMX_H264MAIN_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_H264_HIGH:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoderName = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        case FF_PROFILE_UNKNOWN:
          decoderName = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        default:
          decoderName = OMX_H264HIGH_DECODER;
          mCodingType = OMX_VIDEO_CodingAVC;
          mVideoCodecName = "omx-h264";
          break;
        }
      }
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_MPEG4:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoderName = OMX_MPEG4_DECODER;
      mCodingType = OMX_VIDEO_CodingMPEG4;
      mVideoCodecName = "omx-mpeg4";
      break;
    //}}}
    case AV_CODEC_ID_MPEG1VIDEO:
    //{{{
    case AV_CODEC_ID_MPEG2VIDEO:
      // (role name) video_decoder.mpeg2
      // MPEG-2
      decoderName = OMX_MPEG2V_DECODER;
      mCodingType = OMX_VIDEO_CodingMPEG2;
      mVideoCodecName = "omx-mpeg2";
      break;
    //}}}
    //{{{
    case AV_CODEC_ID_H263:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoderName = OMX_MPEG4_DECODER;
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
      decoderName = OMX_VP6_DECODER;
      mCodingType = OMX_VIDEO_CodingVP6;
      mVideoCodecName = "omx-vp6";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_VP8:
      // (role name) video_decoder.vp8
      // VP8
      decoderName = OMX_VP8_DECODER;
      mCodingType = OMX_VIDEO_CodingVP8;
      mVideoCodecName = "omx-vp8";
    break;
    //}}}
    //{{{
    case AV_CODEC_ID_THEORA:
      // (role name) video_decoder.theora
      // theora
      decoderName = OMX_THEORA_DECODER;
      mCodingType = OMX_VIDEO_CodingTheora;
      mVideoCodecName = "omx-theora";
    break;
    //}}}
    case AV_CODEC_ID_MJPEG:
    //{{{
    case AV_CODEC_ID_MJPEGB:
      // (role name) video_decoder.mjpg
      // mjpg
      decoderName = OMX_MJPEG_DECODER;
      mCodingType = OMX_VIDEO_CodingMJPEG;
      mVideoCodecName = "omx-mjpeg";
    break;
    //}}}
    case AV_CODEC_ID_VC1:
    //{{{
    case AV_CODEC_ID_WMV3:
      // (role name) video_decoder.vc1
      // VC-1, WMV9
      decoderName = OMX_VC1_DECODER;
      mCodingType = OMX_VIDEO_CodingWMV;
      mVideoCodecName = "omx-vc1";
      break;
    //}}}
    //{{{
    default:
      printf ("Vcodec id unknown: %x\n", mConfig.mHints.codec);
      return false;
    break;
    //}}}
    }

  if (!mOmxDecoder.init (decoderName, OMX_IndexParamVideoInit))
    return false;
  if (avClock == NULL)
    return false;

  mAvClock = avClock;
  mOmxClock = mAvClock->getOmxClock();
  if (mOmxClock->GetComponent() == NULL) {
    //{{{  no clock return
    mAvClock = NULL;
    mOmxClock = NULL;
    return false;
    }
    //}}}

  if (mOmxDecoder.SetStateForComponent (OMX_StateIdle) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open SetStateForComponent");
    return false;
    }
    //}}}

  OMX_VIDEO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = mOmxDecoder.GetInputPort();
  formatType.eCompressionFormat = mCodingType;
  if (mConfig.mHints.fpsscale > 0 && mConfig.mHints.fpsrate > 0)
    formatType.xFramerate = (long long)(1<<16)*mConfig.mHints.fpsrate / mConfig.mHints.fpsscale;
  else
    formatType.xFramerate = 25 * (1<<16);
  if (mOmxDecoder.SetParameter (OMX_IndexParamVideoPortFormat, &formatType) != OMX_ErrorNone)
    return false;

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = mOmxDecoder.GetInputPort();
  if (mOmxDecoder.GetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open OMX_IndexParamPortDefinition");
    return false;
    }
    //}}}

  portParam.nPortIndex = mOmxDecoder.GetInputPort();
  portParam.nBufferCountActual = mConfig.mFifoSize / portParam.nBufferSize;
  portParam.format.video.nFrameWidth  = mConfig.mHints.width;
  portParam.format.video.nFrameHeight = mConfig.mHints.height;
  if (mOmxDecoder.SetParameter (OMX_IndexParamPortDefinition, &portParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open OMX_IndexParamPortDefinition");
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
    cLog::log (LOGERROR, "cOmxVideo::open OMX_IndexConfigRequestCallback");
    return false;
    }
    //}}}

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
  OMX_INIT_STRUCTURE(concanParam);
  concanParam.bStartWithValidFrame = OMX_TRUE;
  if (mOmxDecoder.SetParameter (OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open OMX_IndexParamBrcmVideoDecodeErrorConcealment");
    return false;
    }
    //}}}

  if (naluFormatStartCodes (mConfig.mHints.codec,
                            (uint8_t*)mConfig.mHints.extradata, mConfig.mHints.extrasize)) {
    OMX_NALSTREAMFORMATTYPE nalStreamFormat;
    OMX_INIT_STRUCTURE(nalStreamFormat);
    nalStreamFormat.nPortIndex = mOmxDecoder.GetInputPort();
    nalStreamFormat.eNaluFormat = OMX_NaluFormatStartCodes;
    if (mOmxDecoder.SetParameter ((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStreamFormat) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::open OMX_IndexParamNalStreamFormatSelect");
      return false;
      }
      //}}}
    }

  // Alloc buffers for the omx intput port.
  if (mOmxDecoder.AllocInputBuffers() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open AllocInputBuffers");
    return false;
    }
    //}}}
  if (mOmxDecoder.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::open SetStateForComponent");
    return false;
    }
    //}}}
  sendDecoderConfig();

  mDropState = false;
  mSetStartTime = true;
  //{{{  set mTransform
  switch (mConfig.mHints.orientation) {
    case 90:  mTransform = OMX_DISPLAY_ROT90; break;
    case 180: mTransform = OMX_DISPLAY_ROT180; break;
    case 270: mTransform = OMX_DISPLAY_ROT270; break;
    case 1:   mTransform = OMX_DISPLAY_MIRROR_ROT0; break;
    case 91:  mTransform = OMX_DISPLAY_MIRROR_ROT90; break;
    case 181: mTransform = OMX_DISPLAY_MIRROR_ROT180; break;
    case 271: mTransform = OMX_DISPLAY_MIRROR_ROT270; break;
    default:  mTransform = OMX_DISPLAY_ROT0; break;
    }

  if (vflip)
     mTransform = OMX_DISPLAY_MIRROR_ROT180;
  //}}}
  if (mOmxDecoder.BadState())
    return false;

  cLog::log (LOGINFO1, "cOmxVideo::open %p in:%x out:%x deint:%d hdmi:%d",
             mOmxDecoder.GetComponent(), mOmxDecoder.GetInputPort(), mOmxDecoder.GetOutputPort(),
             mConfig.mDeinterlace, mConfig.mHdmiClockSync);

  float aspect = mConfig.mHints.aspect ?
    (float)mConfig.mHints.aspect / mConfig.mHints.width * mConfig.mHints.height : 1.f;
  mPixelAspect = aspect / mConfig.mDisplayAspect;

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
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged OMX_IndexParamPortDefinition");

  OMX_CONFIG_POINTTYPE pixel_aspect;
  OMX_INIT_STRUCTURE(pixel_aspect);
  pixel_aspect.nPortIndex = mOmxDecoder.GetOutputPort();
  if (mOmxDecoder.GetParameter (OMX_IndexParamBrcmPixelAspectRatio, &pixel_aspect) != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged OMX_IndexParamBrcmPixelAspectRatio");

  if (pixel_aspect.nX && pixel_aspect.nY && !mConfig.mHints.forced_aspect) {
    //{{{  aspect changed
    float aspect = (float)pixel_aspect.nX / (float)pixel_aspect.nY;
    mPixelAspect = aspect / mConfig.mDisplayAspect;
    }
    //}}}
  if (mSettingsChanged) {
    //{{{  settings changed
    portSettingsChangedLog (port_image, -1);
    setVideoRect();
    mOmxDecoder.EnablePort (mOmxDecoder.GetOutputPort(), true);
    return true;
    }
    //}}}

  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);
  interlace.nPortIndex = mOmxDecoder.GetOutputPort();
  mOmxDecoder.GetConfig (OMX_IndexConfigCommonInterlace, &interlace);

  if (mConfig.mDeinterlace == VS_DEINTERLACEMODE_FORCE)
    mDeinterlace = true;
  else if (mConfig.mDeinterlace == VS_DEINTERLACEMODE_OFF)
    mDeinterlace = false;
  else
    mDeinterlace = interlace.eMode != OMX_InterlaceProgressive;

  if (!mOmxRender.init ("OMX.broadcom.video_render", OMX_IndexParamVideoInit))
    return false;

  mOmxRender.ResetEos();
  portSettingsChangedLog (port_image, interlace.eMode);

  if (!mOmxSched.init ("OMX.broadcom.video_scheduler", OMX_IndexParamVideoInit))
    return false;

  if (mDeinterlace)
    if (!mOmxImageFx.init ("OMX.broadcom.image_fx", OMX_IndexParamImageInit))
      return false;

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = mOmxRender.GetInputPort();
  configDisplay.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_ALPHA | OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_LAYER | OMX_DISPLAY_SET_NUM);
  configDisplay.alpha = mConfig.mAlpha;
  configDisplay.num = mConfig.mDisplay;
  configDisplay.layer = mConfig.mLayer;
  configDisplay.transform = mTransform;
  if (mOmxRender.SetConfig (OMX_IndexConfigDisplayRegion, &configDisplay) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGINFO1, "cOmxVideo::portSettingsChanged OMX_IndexConfigDisplayRegion %d", mTransform);
    return false;
    }
    //}}}

  setVideoRect();

  if (mConfig.mHdmiClockSync) {
    //{{{  config latency
    OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
    OMX_INIT_STRUCTURE(latencyTarget);
    latencyTarget.nPortIndex = mOmxRender.GetInputPort();
    latencyTarget.bEnabled = OMX_TRUE;
    latencyTarget.nFilter = 2;
    latencyTarget.nTarget = 4000;
    latencyTarget.nShift = 3;
    latencyTarget.nSpeedFactor = -135;
    latencyTarget.nInterFactor = 500;
    latencyTarget.nAdjCap = 20;

    if (mOmxRender.SetConfig (OMX_IndexConfigLatencyTarget, &latencyTarget) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged OMX_IndexConfigLatencyTarget");
      return false;
      }
      //}}}
    }
    //}}}
  if (mDeinterlace) {
    //{{{  setup deinterlace
    bool advancedDeint = mConfig.mAdvancedHdDeinterlace ||
          (port_image.format.video.nFrameWidth * port_image.format.video.nFrameHeight <= (576 * 720));
    if (!advancedDeint) {
      // image_fx assumed 3 frames of context, not needed for simple deinterlace
      OMX_PARAM_U32TYPE extra_buffers;
      OMX_INIT_STRUCTURE(extra_buffers);
      extra_buffers.nU32 = -2;
      if (mOmxImageFx.SetParameter (OMX_IndexParamBrcmExtraBuffers, &extra_buffers) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged OMX_IndexParamBrcmExtraBuffers");
        return false;
        }
        //}}}
      }

    OMX_CONFIG_IMAGEFILTERPARAMSTYPE image_filter;
    OMX_INIT_STRUCTURE(image_filter);
    image_filter.nPortIndex = mOmxImageFx.GetOutputPort();
    image_filter.nNumParams = 4;
    image_filter.nParams[0] = 3;
    image_filter.nParams[1] = 0; // default frame interval
    image_filter.nParams[2] = 0; // half framerate
    image_filter.nParams[3] = 1; // use qpus

    if (advancedDeint)
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;
    else
      image_filter.eImageFilter = OMX_ImageFilterDeInterlaceFast;

    if (mOmxImageFx.SetConfig (OMX_IndexConfigCommonImageFilterParameters, &image_filter) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged OMX_IndexConfigCommonImageFilterParameters");
      return false;
      }
      //}}}

    mOmxTunnelDecoder.init (&mOmxDecoder, mOmxDecoder.GetOutputPort(), &mOmxImageFx, mOmxImageFx.GetInputPort());
    mOmxTunnelImageFx.init (&mOmxImageFx, mOmxImageFx.GetOutputPort(), &mOmxSched, mOmxSched.GetInputPort());
    }
    //}}}
  else
    mOmxTunnelDecoder.init (&mOmxDecoder, mOmxDecoder.GetOutputPort(), &mOmxSched, mOmxSched.GetInputPort());

  mOmxTunnelSched.init (&mOmxSched, mOmxSched.GetOutputPort(), &mOmxRender, mOmxRender.GetInputPort());
  mOmxTunnelClock.init (mOmxClock, mOmxClock->GetInputPort() + 1, &mOmxSched, mOmxSched.GetOutputPort() + 1);
  if (mOmxTunnelClock.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged mOmxTunnelClock.Establish");
    return false;
    }
    //}}}
  if (mOmxTunnelDecoder.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged mOmxTunnelDecoder.Establish");
    return false;
    }
    //}}}
  if (mDeinterlace) {
    if (mOmxTunnelImageFx.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged mOmxTunnelImageFx.Establish");
      return false;
      }
      //}}}
    if (mOmxImageFx.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged mOmxImageFx.SetStateForComponent");
      return false;
      }
      //}}}
    }
  if (mOmxTunnelSched.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged mOmxTunnelSched.Establish");
    return false;
    }
    //}}}
  if (mOmxSched.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged - mOmxSched.SetStateForComponent");
    return false;
    }
    //}}}
  if (mOmxRender.SetStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxVideo::portSettingsChanged - mOmxRender.SetStateForComponent");
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
      cLog::log (LOGERROR, "cOmxVideo::decode timeout");
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
      cLog::log (LOGERROR, "cOmxVideo::decode OMX_EmptyThisBuffer");
      mOmxDecoder.DecoderEmptyBufferDone (mOmxDecoder.GetComponent(), omx_buffer);
      return false;
      }
      //}}}

    if (mOmxDecoder.WaitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone) {
      if (!portSettingsChanged()) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxVideo::decode");
        return false;
        }
        //}}}
      }
    if (mOmxDecoder.WaitForEvent (OMX_EventParamOrConfigChanged, 0) == OMX_ErrorNone)
      if (!portSettingsChanged())
        cLog::log (LOGERROR, "OMXVideo::decode EventParamOrConfigChanged");
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
    cLog::log(LOGERROR, "cOmxVideo::submitEOS GetInputBuffer");
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
    cLog::log (LOGERROR, "cOmxVideo::submitEOS OMX_EmptyThisBuffer");
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
    mOmxImageFx.FlushInput();

  mOmxRender.ResetEos();
  }
//}}}
//{{{
void cOmxVideo::close() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mOmxTunnelClock.deEstablish();
  mOmxTunnelDecoder.deEstablish();
  if (mDeinterlace)
    mOmxTunnelImageFx.deEstablish();
  mOmxTunnelSched.deEstablish();

  mOmxDecoder.FlushInput();

  mOmxSched.deInit();
  mOmxDecoder.deInit();
  if (mDeinterlace)
    mOmxImageFx.deInit();
  mOmxRender.deInit();

  mVideoCodecName = "";
  mDeinterlace = false;
  mAvClock = NULL;
  }
//}}}

// private
//{{{
void cOmxVideo::portSettingsChangedLog (OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode) {

  cLog::log (LOGINFO, "portSettings %dx%d@%.2f int:%d deint:%d par:%.2f disp:%d lay:%d alpha:%d aspect:%d",
             port_image.format.video.nFrameWidth, port_image.format.video.nFrameHeight,
             port_image.format.video.xFramerate / (float)(1<<16),
             interlaceEMode, mDeinterlace,  mPixelAspect,
             mConfig.mDisplay, mConfig.mLayer, mConfig.mAlpha, mConfig.mAspectMode);
  }
//}}}
