// OMXVideo.cpp
//{{{  includes
#include <sys/time.h>
#include <inttypes.h>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

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
bool cOmxVideo::isEOS() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos && !mRender.isEOS())
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
  return mDecoder.getInputBufferSize();
  }
//}}}
//{{{
unsigned int cOmxVideo::getInputBufferSpace() {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return mDecoder.getInputBufferSpace();
  }
//}}}

//{{{
void cOmxVideo::setAlpha (int alpha) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE display;
  OMX_INIT_STRUCTURE(display);

  display.nPortIndex = mRender.getInputPort();
  display.set = OMX_DISPLAY_SET_ALPHA;
  display.alpha = alpha;
  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &display) != OMX_ErrorNone)
    cLog::log (LOGERROR, __func__);
  }
//}}}
//{{{
void cOmxVideo::setVideoRect() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE display;
  OMX_INIT_STRUCTURE(display);
  display.nPortIndex = mRender.getInputPort();
  display.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT |
                                     OMX_DISPLAY_SET_MODE |
                                     OMX_DISPLAY_SET_SRC_RECT |
                                     OMX_DISPLAY_SET_FULLSCREEN |
                                     OMX_DISPLAY_SET_PIXEL);
  display.mode = (mConfig.mAspectMode == 2) ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;
  display.noaspect = (mConfig.mAspectMode == 3) ? OMX_TRUE : OMX_FALSE;

  display.src_rect.x_offset = (int)(mConfig.mSrcRect.x1 + 0.5f);
  display.src_rect.y_offset = (int)(mConfig.mSrcRect.y1 + 0.5f);
  display.src_rect.width = (int)(mConfig.mSrcRect.getWidth() + 0.5f);
  display.src_rect.height = (int)(mConfig.mSrcRect.getHeight() + 0.5f);

  if ((mConfig.mDstRect.x2 > mConfig.mDstRect.x1) &&
      (mConfig.mDstRect.y2 > mConfig.mDstRect.y1)) {
    display.set = (OMX_DISPLAYSETTYPE)(display.set | OMX_DISPLAY_SET_DEST_RECT);
    display.fullscreen = OMX_FALSE;
    if ((mConfig.mAspectMode != 1) &&
        (mConfig.mAspectMode != 2) &&
        (mConfig.mAspectMode != 3))
      display.noaspect = OMX_TRUE;
    display.dest_rect.x_offset = (int)(mConfig.mDstRect.x1 + 0.5f);
    display.dest_rect.y_offset = (int)(mConfig.mDstRect.y1 + 0.5f);
    display.dest_rect.width = (int)(mConfig.mDstRect.getWidth() + 0.5f);
    display.dest_rect.height = (int)(mConfig.mDstRect.getHeight() + 0.5f);
    }
  else
    display.fullscreen = OMX_TRUE;

  if ((display.noaspect == OMX_FALSE) && (mPixelAspect != 0.f)) {
    AVRational aspect = av_d2q (mPixelAspect, 100);
    display.pixel_x = aspect.num;
    display.pixel_y = aspect.den;
    }
  else {
    display.pixel_x = 0;
    display.pixel_y = 0;
    }

  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &display) != OMX_ErrorNone)
    cLog::log (LOGERROR, string(__func__) + " set display");
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (int aspectMode) {

  mConfig.mAspectMode = aspectMode;
  setVideoRect();
  }
//}}}
//{{{
void cOmxVideo::setVideoRect (const cRect& srcRect, const cRect& dstRect) {

  mConfig.mSrcRect = srcRect;
  mConfig.mDstRect = dstRect;
  setVideoRect();
  }
//}}}

//{{{
bool cOmxVideo::open (cOmxClock* avClock, const cOmxVideoConfig &config) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  close();

  mConfig = config;
  mPortChanged = false;
  mSetStartTime = true;
  mSubmittedEos = false;
  mFailedEos = false;
  mDropState = false;

  //{{{  init decoder
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

  if (!mDecoder.init (decoderName, OMX_IndexParamVideoInit))
    return false;
  //}}}
  mAvClock = avClock;
  mClock = mAvClock->getOmxClock();
  if (!mClock->getComponent()) {
    //{{{  no clock. return
    mAvClock = nullptr;
    mClock = nullptr;
    return false;
    }
    //}}}
  if (mDecoder.setStateForComponent (OMX_StateIdle) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " setStateForComponent");
    return false;
    }
    //}}}

  //{{{  set portFormat fps
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = mDecoder.getInputPort();
  format.eCompressionFormat = mCodingType;
  if (mConfig.mHints.fpsscale > 0 && mConfig.mHints.fpsrate > 0)
    format.xFramerate = (long long)(1<<16)*mConfig.mHints.fpsrate / mConfig.mHints.fpsscale;
  else
    format.xFramerate = 25 * (1<<16);
  if (mDecoder.setParameter (OMX_IndexParamVideoPortFormat, &format) != OMX_ErrorNone) {
    cLog::log (LOGERROR, string(__func__) + " set fps");
    return false;
    }
  //}}}
  //{{{  set portParam width,height,buffers
  OMX_PARAM_PORTDEFINITIONTYPE port;
  OMX_INIT_STRUCTURE(port);
  port.nPortIndex = mDecoder.getInputPort();
  if (mDecoder.getParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    //  error return
    cLog::log (LOGERROR, string(__func__) + " get inputPort");
    return false;
    }

  port.nPortIndex = mDecoder.getInputPort();
  port.nBufferCountActual = mConfig.mFifoSize / port.nBufferSize;
  port.format.video.nFrameWidth = mConfig.mHints.width;
  port.format.video.nFrameHeight = mConfig.mHints.height;
  if (mDecoder.setParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    //  error return
    cLog::log (LOGERROR, string(__func__) + " set inputPort");
    return false;
    }
  //}}}
  //{{{  request portChanged callback on aspect change
  OMX_CONFIG_REQUESTCALLBACKTYPE request;
  OMX_INIT_STRUCTURE(request);
  request.nPortIndex = mDecoder.getOutputPort();
  request.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
  request.bEnable = OMX_TRUE;
  if (mDecoder.setParameter ((OMX_INDEXTYPE)OMX_IndexConfigRequestCallback, &request) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " request portChanged callback");
    return false;
    }
    //}}}
  //}}}
  //{{{  error conceal
  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE conceal;
  OMX_INIT_STRUCTURE(conceal);
  conceal.bStartWithValidFrame = OMX_FALSE; // OMX_TRUE;
  if (mDecoder.setParameter (OMX_IndexParamBrcmVideoDecodeErrorConcealment, &conceal) != OMX_ErrorNone) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " set conceal");
    return false;
    }
  //}}}
  //{{{  set nalFormat
  if (naluFormatStartCodes (mConfig.mHints.codec,
                            (uint8_t*)mConfig.mHints.extradata, mConfig.mHints.extrasize)) {
    OMX_NALSTREAMFORMATTYPE nalStream;
    OMX_INIT_STRUCTURE(nalStream);
    nalStream.nPortIndex = mDecoder.getInputPort();
    nalStream.eNaluFormat = OMX_NaluFormatStartCodes;
    if (mDecoder.setParameter ((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStream) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " set NAL");
      return false;
      }
    }
  //}}}

  // alloc bufers for omx input port.
  if (mDecoder.allocInputBuffers() != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " allocInputBuffers");
    return false;
    }
    //}}}
  if (mDecoder.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " setStateForComponent");
    return false;
    }
    //}}}
  sendDecoderExtraConfig();

  //{{{  set transform
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
  if (mDecoder.badState())
    return false;

  cLog::log (LOGINFO1, "cOmxVideo::open %p in:%x out:%x deint:%d hdmi:%d",
             mDecoder.getComponent(), mDecoder.getInputPort(), mDecoder.getOutputPort(),
             mConfig.mDeinterlace, mConfig.mHdmiClockSync);

  float aspect = mConfig.mHints.aspect ?
    (float)mConfig.mHints.aspect / mConfig.mHints.width * mConfig.mHints.height : 1.f;
  mPixelAspect = aspect / mConfig.mDisplayAspect;

  return true;
  }
//}}}
//{{{
bool cOmxVideo::portChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mPortChanged)
    mDecoder.disablePort (mDecoder.getOutputPort(), true);

  //{{{  get port
  OMX_PARAM_PORTDEFINITIONTYPE port;
  OMX_INIT_STRUCTURE(port);

  port.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone)
    cLog::log (LOGERROR, string(__func__) + " get param");
  //}}}
  //{{{  get aspect
  OMX_CONFIG_POINTTYPE aspect;
  OMX_INIT_STRUCTURE(aspect);

  aspect.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParameter (OMX_IndexParamBrcmPixelAspectRatio, &aspect) != OMX_ErrorNone)
    cLog::log (LOGERROR, string(__func__) + " get aspectRatio");

  if ((aspect.nX && aspect.nY) && !mConfig.mHints.forced_aspect)
    mPixelAspect = ((float)aspect.nX / (float)aspect.nY) / mConfig.mDisplayAspect;
  //}}}
  if (mPortChanged) {
    //{{{  settings changed
    logPortChanged (port, -1);
    setVideoRect();

    mDecoder.enablePort (mDecoder.getOutputPort(), true);
    return true;
    }
    //}}}

  if (!mRender.init ("OMX.broadcom.video_render", OMX_IndexParamVideoInit))
    return false;
  mRender.resetEos();

  if (!mScheduler.init ("OMX.broadcom.video_scheduler", OMX_IndexParamVideoInit))
    return false;

  //{{{  set interlace
  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);

  interlace.nPortIndex = mDecoder.getOutputPort();
  mDecoder.getConfig (OMX_IndexConfigCommonInterlace, &interlace);
  if (mConfig.mDeinterlace == eInterlaceForce)
    mDeinterlace = true;
  else if (mConfig.mDeinterlace == eInterlaceOff)
    mDeinterlace = false;
  else
    mDeinterlace = (interlace.eMode != OMX_InterlaceProgressive);
  logPortChanged (port, interlace.eMode);

  if (mDeinterlace && !mImageFx.init ("OMX.broadcom.image_fx", OMX_IndexParamImageInit))
    return false;
  //}}}
  //{{{  set display
  OMX_CONFIG_DISPLAYREGIONTYPE display;
  OMX_INIT_STRUCTURE(display);
  display.nPortIndex = mRender.getInputPort();
  display.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_ALPHA |
                                     OMX_DISPLAY_SET_TRANSFORM |
                                     OMX_DISPLAY_SET_LAYER |
                                     OMX_DISPLAY_SET_NUM);
  display.alpha = mConfig.mAlpha;
  display.transform = mTransform;
  display.layer = mConfig.mLayer;
  display.num = mConfig.mDisplay;
  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &display) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGINFO1, string(__func__) + " set display");
    return false;
    }
    //}}}
  setVideoRect();
  //}}}
  if (mConfig.mHdmiClockSync) {
    //{{{  set latency
    OMX_CONFIG_LATENCYTARGETTYPE latency;
    OMX_INIT_STRUCTURE(latency);

    latency.nPortIndex = mRender.getInputPort();
    latency.bEnabled = OMX_TRUE;
    latency.nFilter = 2;
    latency.nTarget = 4000;
    latency.nShift = 3;
    latency.nSpeedFactor = -135;
    latency.nInterFactor = 500;
    latency.nAdjCap = 20;

    if (mRender.setConfig (OMX_IndexConfigLatencyTarget, &latency) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " set latency");
      return false;
      }
    }
    //}}}
  if (mDeinterlace) {
    //{{{  set deinterlace
    bool advancedDeint = mConfig.mAdvancedHdDeinterlace ||
                        (port.format.video.nFrameWidth * port.format.video.nFrameHeight <= (576*720));
    if (!advancedDeint) {
      // image_fx assumed 3 frames of context, not needed for simple deinterlace
      OMX_PARAM_U32TYPE extraBuffers;
      OMX_INIT_STRUCTURE(extraBuffers);

      extraBuffers.nU32 = -2;
      if (mImageFx.setParameter (OMX_IndexParamBrcmExtraBuffers, &extraBuffers) != OMX_ErrorNone) {
        // error return
        cLog::log (LOGERROR, string(__func__) + " set extraBuffers");
        return false;
        }
      }

    OMX_CONFIG_IMAGEFILTERPARAMSTYPE filter;
    OMX_INIT_STRUCTURE(filter);

    filter.nPortIndex = mImageFx.getOutputPort();
    filter.nNumParams = 4;
    filter.nParams[0] = 3;
    filter.nParams[1] = 0; // default frame interval
    filter.nParams[2] = 0; // half framerate
    filter.nParams[3] = 1; // use qpus

    if (advancedDeint)
      filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;
    else
      filter.eImageFilter = OMX_ImageFilterDeInterlaceFast;

    if (mImageFx.setConfig (OMX_IndexConfigCommonImageFilterParameters, &filter) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " set image filters");
      return false;
      }

    mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(), &mImageFx, mImageFx.getInputPort());
    mTunnelImageFx.init (&mImageFx, mImageFx.getOutputPort(), &mScheduler, mScheduler.getInputPort());
    }
    //}}}
  else
    mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(), &mScheduler, mScheduler.getInputPort());

  mTunnelSched.init (&mScheduler, mScheduler.getOutputPort(), &mRender, mRender.getInputPort());
  mTunnelClock.init (mClock, mClock->getInputPort() + 1, &mScheduler, mScheduler.getOutputPort() + 1);
  if (mTunnelClock.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR,  string(__func__) + " mTunnelClock.establish");
    return false;
    }
    //}}}
  if (mTunnelDecoder.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR,  string(__func__) + " mTunnelDecoder.establish");
    return false;
    }
    //}}}
  if (mDeinterlace) {
    if (mTunnelImageFx.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR,  string(__func__) + " mTunnelImageFx.establish");
      return false;
      }
      //}}}
    if (mImageFx.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR,  string(__func__) + " mImageFx.setStateForComponent");
      return false;
      }
      //}}}
    }
  if (mTunnelSched.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mTunnelSched.establish");
    return false;
    }
    //}}}
  if (mScheduler.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + "mScheduler.setStateForComponent");
    return false;
    }
    //}}}
  if (mRender.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + "mRender.setStateForComponent");
    return false;
    }
    //}}}

  mPortChanged = true;
  return true;
  }
//}}}
//{{{
bool cOmxVideo::decode (uint8_t* data, int size, double dts, double pts) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  unsigned int bytesLeft = (unsigned int)size;
  OMX_U32 nFlags = 0;
  if (mSetStartTime) {
    nFlags |= OMX_BUFFERFLAG_STARTTIME;
    cLog::log (LOGINFO1, "cOmxVideo::Decode - startTime:%f",
                         ((pts == DVD_NOPTS_VALUE) ? 0.0 : pts) / 1000000.f);
    mSetStartTime = false;
    }

  if ((pts == DVD_NOPTS_VALUE) && (dts == DVD_NOPTS_VALUE))
    nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  else if (pts == DVD_NOPTS_VALUE)
    nFlags |= OMX_BUFFERFLAG_TIME_IS_DTS;

  while (bytesLeft) {
    // 500ms timeout
    auto buffer = mDecoder.getInputBuffer (500);
    if (!buffer) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " timeout");
      return false;
      }
      //}}}

    buffer->nFlags = nFlags;
    buffer->nOffset = 0;
    buffer->nTimeStamp = toOmxTime ((uint64_t)((pts != DVD_NOPTS_VALUE) ?
                                                 pts : (dts != DVD_NOPTS_VALUE) ? dts : 0.0));
    buffer->nFilledLen = min ((OMX_U32)bytesLeft, buffer->nAllocLen);
    memcpy (buffer->pBuffer, data, buffer->nFilledLen);
    bytesLeft -= buffer->nFilledLen;
    data += buffer->nFilledLen;
    if (bytesLeft == 0)
      buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    if (mDecoder.emptyThisBuffer (buffer) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), buffer);
      return false;
      }
      //}}}
    if (mDecoder.waitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone) {
      if (!portChanged()) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " port changed");
        return false;
        }
        //}}}
      }
    if (mDecoder.waitForEvent (OMX_EventParamOrConfigChanged, 0) == OMX_ErrorNone)
      if (!portChanged())
        cLog::log (LOGERROR, string(__func__) + " param changed");
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

  auto omxBuffer = mDecoder.getInputBuffer (1000);
  if (omxBuffer == NULL) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " getInputBuffer");
    mFailedEos = true;
    return;
    }

  omxBuffer->nOffset = 0;
  omxBuffer->nFilledLen = 0;
  omxBuffer->nTimeStamp = toOmxTime (0LL);
  omxBuffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  if (mDecoder.emptyThisBuffer (omxBuffer) != OMX_ErrorNone) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), omxBuffer);
    return;
    }
  }
//}}}
//{{{
void cOmxVideo::reset() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSetStartTime = true;

  mDecoder.flushInput();
  if (mDeinterlace)
    mImageFx.flushInput();
  mRender.resetEos();
  }
//}}}
//{{{
void cOmxVideo::close() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mTunnelClock.deEstablish();
  mTunnelDecoder.deEstablish();
  if (mDeinterlace)
    mTunnelImageFx.deEstablish();
  mTunnelSched.deEstablish();

  mDecoder.flushInput();

  mScheduler.deInit();
  mDecoder.deInit();
  if (mDeinterlace)
    mImageFx.deInit();
  mRender.deInit();

  mVideoCodecName = "";
  mDeinterlace = false;
  mAvClock = NULL;
  }
//}}}

// private
//{{{
bool cOmxVideo::sendDecoderExtraConfig() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if ((mConfig.mHints.extrasize > 0) && (mConfig.mHints.extradata != NULL)) {
    auto buffer = mDecoder.getInputBuffer();
    if (buffer == NULL) {
      cLog::log (LOGERROR, string(__func__) + " buffer error");
      return false;
      }

    buffer->nOffset = 0;
    buffer->nFilledLen = min ((OMX_U32)mConfig.mHints.extrasize, buffer->nAllocLen);
    memset (buffer->pBuffer, 0, buffer->nAllocLen);
    memcpy (buffer->pBuffer, mConfig.mHints.extradata, buffer->nFilledLen);
    buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mDecoder.emptyThisBuffer (buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), buffer);
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
void cOmxVideo::logPortChanged (OMX_PARAM_PORTDEFINITIONTYPE port, int interlaceMode) {

  cLog::log (LOGINFO, "port %dx%d %.2f intMode:%d deint:%d par:%.2f dis:%d lay:%d alp:%d asp:%d",
                      port.format.video.nFrameWidth, port.format.video.nFrameHeight,
                      port.format.video.xFramerate / (float)(1<<16),
                      interlaceMode, mDeinterlace,
                      mPixelAspect,
                      mConfig.mDisplay, mConfig.mLayer, mConfig.mAlpha, mConfig.mAspectMode);
  }
//}}}
