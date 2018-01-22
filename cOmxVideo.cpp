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
cOmxVideo::~cOmxVideo() {
  close();
  }
//}}}

//{{{
bool cOmxVideo::isEOS() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos && !mRender.isEOS())
    return false;
  if (mSubmittedEos) {
    mSubmittedEos = false;
    cLog::log (LOGINFO, __func__);
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
  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &display))
    cLog::log (LOGERROR, __func__);
  }
//}}}
//{{{
void cOmxVideo::setVideoRect() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_DISPLAYREGIONTYPE displayRegion;
  OMX_INIT_STRUCTURE(displayRegion);
  displayRegion.nPortIndex = mRender.getInputPort();
  displayRegion.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_NOASPECT |
                                           OMX_DISPLAY_SET_MODE |
                                           OMX_DISPLAY_SET_SRC_RECT |
                                           OMX_DISPLAY_SET_FULLSCREEN |
                                           OMX_DISPLAY_SET_PIXEL);
  displayRegion.mode = (mConfig.mAspectMode == 2) ? OMX_DISPLAY_MODE_FILL : OMX_DISPLAY_MODE_LETTERBOX;
  displayRegion.noaspect = (mConfig.mAspectMode == 3) ? OMX_TRUE : OMX_FALSE;

  displayRegion.src_rect.x_offset = (int)(mConfig.mSrcRect.x1 + 0.5f);
  displayRegion.src_rect.y_offset = (int)(mConfig.mSrcRect.y1 + 0.5f);
  displayRegion.src_rect.width = (int)(mConfig.mSrcRect.getWidth() + 0.5f);
  displayRegion.src_rect.height = (int)(mConfig.mSrcRect.getHeight() + 0.5f);

  if ((mConfig.mDstRect.x2 > mConfig.mDstRect.x1) &&
      (mConfig.mDstRect.y2 > mConfig.mDstRect.y1)) {
    displayRegion.set = (OMX_DISPLAYSETTYPE)(displayRegion.set | OMX_DISPLAY_SET_DEST_RECT);
    displayRegion.fullscreen = OMX_FALSE;
    if ((mConfig.mAspectMode != 1) &&
        (mConfig.mAspectMode != 2) &&
        (mConfig.mAspectMode != 3))
      displayRegion.noaspect = OMX_TRUE;
    displayRegion.dest_rect.x_offset = (int)(mConfig.mDstRect.x1 + 0.5f);
    displayRegion.dest_rect.y_offset = (int)(mConfig.mDstRect.y1 + 0.5f);
    displayRegion.dest_rect.width = (int)(mConfig.mDstRect.getWidth() + 0.5f);
    displayRegion.dest_rect.height = (int)(mConfig.mDstRect.getHeight() + 0.5f);
    }
  else
    displayRegion.fullscreen = OMX_TRUE;

  if ((displayRegion.noaspect == OMX_FALSE) && (mPixelAspect != 0.f)) {
    AVRational aspect = av_d2q (mPixelAspect, 100);
    displayRegion.pixel_x = aspect.num;
    displayRegion.pixel_y = aspect.den;
    }
  else {
    displayRegion.pixel_x = 0;
    displayRegion.pixel_y = 0;
    }

  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &displayRegion))
    cLog::log (LOGERROR, string(__func__) + " setDisplayRegion");
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
bool cOmxVideo::open (cOmxClock* clock, const cOmxVideoConfig &config) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mClock = clock;
  mConfig = config;

  //{{{  init decoder
  string decoderName;
  mCodingType = OMX_VIDEO_CodingUnused;
  mVideoCodecName = "";

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
  if (mDecoder.setState (OMX_StateIdle)) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " setState");
    return false;
    }
    //}}}

  cLog::log (LOGINFO, string (__func__) + " " + mDecoder.getName() +
             " " + dec(mDecoder.getInputPort()) + "->" + dec(mDecoder.getOutputPort()));

  //{{{  set port format codingType,fps
  OMX_VIDEO_PARAM_PORTFORMATTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);

  portFormat.nPortIndex = mDecoder.getInputPort();
  portFormat.eCompressionFormat = mCodingType;
  if (mConfig.mHints.fpsscale > 0 && mConfig.mHints.fpsrate > 0)
    portFormat.xFramerate = (long long)(1<<16)*mConfig.mHints.fpsrate / mConfig.mHints.fpsscale;
  else
    portFormat.xFramerate = 25 * (1<<16);
  if (mDecoder.setParam (OMX_IndexParamVideoPortFormat, &portFormat)) {
    cLog::log (LOGERROR, string(__func__) + " setPortFormat");
    return false;
    }
  //}}}
  //{{{  set portParam width,height,buffers
  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);

  portParam.nPortIndex = mDecoder.getInputPort();
  if (mDecoder.getParam (OMX_IndexParamPortDefinition, &portParam)) {
    //  error return
    cLog::log (LOGERROR, string(__func__) + " getInputPortParam");
    return false;
    }

  portParam.nPortIndex = mDecoder.getInputPort();
  portParam.nBufferCountActual = mConfig.mFifoSize / portParam.nBufferSize;
  portParam.format.video.nFrameWidth = mConfig.mHints.width;
  portParam.format.video.nFrameHeight = mConfig.mHints.height;
  if (mDecoder.setParam (OMX_IndexParamPortDefinition, &portParam)) {
    //  error return
    cLog::log (LOGERROR, string(__func__) + " setInputPortParam");
    return false;
    }
  //}}}
  //{{{  request aspChange callback
  OMX_CONFIG_REQUESTCALLBACKTYPE requestCallback;
  OMX_INIT_STRUCTURE(requestCallback);

  requestCallback.nPortIndex = mDecoder.getOutputPort();
  requestCallback.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
  requestCallback.bEnable = OMX_TRUE;
  if (mDecoder.setParam ((OMX_INDEXTYPE)OMX_IndexConfigRequestCallback, &requestCallback)) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " request callback");
    return false;
    }
  //}}}
  //{{{  set conceal
  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE conceal;
  OMX_INIT_STRUCTURE(conceal);

  conceal.bStartWithValidFrame = OMX_FALSE; // OMX_TRUE;
  if (mDecoder.setParam (OMX_IndexParamBrcmVideoDecodeErrorConcealment, &conceal)) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " setConceal");
    return false;
    }
  //}}}
  setNaluFormat (mConfig.mHints.codec, (uint8_t*)mConfig.mHints.extradata, mConfig.mHints.extrasize);

  // alloc bufers for omx input port.
  if (mDecoder.allocInputBuffers()) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " allocInputBuffers");
    return false;
    }
    //}}}
  if (mDecoder.setState (OMX_StateExecuting)) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " setState");
    return false;
    }
    //}}}
  sendDecoderExtraConfig();

  if (mDecoder.badState())
    return false;

  float aspect = mConfig.mHints.aspect ?
    (float)mConfig.mHints.aspect / mConfig.mHints.width * mConfig.mHints.height : 1.f;
  mPixelAspect = aspect / mConfig.mDisplayAspect;

  mSetStartTime = true;
  return true;
  }
//}}}
//{{{
bool cOmxVideo::decode (uint8_t* data, int size, double dts, double pts, std::atomic<bool>& flushRequested) {

  while (size > (int)getInputBufferSpace()) {
    mClock->msSleep (10);
    if (flushRequested)
      return true;
    }

  cLog::log (LOGINFO1, "decode " + frac(pts/1000000.0,6,2,' ') + " " + dec(size));

  lock_guard<recursive_mutex> lockGuard (mMutex);

  unsigned int bytesLeft = (unsigned int)size;
  OMX_U32 nFlags = 0;
  if (mSetStartTime) {
    nFlags |= OMX_BUFFERFLAG_STARTTIME;
    cLog::log (LOGINFO1, "decode - startTime " + frac (pts/kPtsScale,6,2,' '));
    mSetStartTime = false;
    }
  if ((pts == kNoPts) && (dts == kNoPts))
    nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  else if (pts == kNoPts)
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
    buffer->nTimeStamp = toOmxTime ((uint64_t)((pts != kNoPts) ? pts : (dts != kNoPts) ? dts : 0.0));
    buffer->nFilledLen = min ((OMX_U32)bytesLeft, buffer->nAllocLen);
    memcpy (buffer->pBuffer, data, buffer->nFilledLen);
    bytesLeft -= buffer->nFilledLen;
    data += buffer->nFilledLen;
    if (bytesLeft == 0)
      buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    if (mDecoder.emptyThisBuffer (buffer)) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), buffer);
      return false;
      }
      //}}}
    if (mDecoder.waitEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone) {
      if (!srcChanged()) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " srcChanged");
        return false;
        }
        //}}}
      }
    if (mDecoder.waitEvent (OMX_EventParamOrConfigChanged, 0) == OMX_ErrorNone)
      if (!srcChanged())
        cLog::log (LOGERROR, string(__func__) + " paramChanged");
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
  if (mDecoder.emptyThisBuffer (omxBuffer)) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), omxBuffer);
    return;
    }
  }
//}}}
//{{{
void cOmxVideo::reset() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSetStartTime = true;

  mDecoder.flushInput();
  if (mDeInterlace)
    mImageFx.flushInput();
  mRender.resetEos();
  }
//}}}
//{{{
void cOmxVideo::close() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mTunnelClock.deEstablish();
  mTunnelDecoder.deEstablish();
  if (mDeInterlace)
    mTunnelImageFx.deEstablish();
  mTunnelSched.deEstablish();

  mDecoder.flushInput();

  mScheduler.deInit();
  mDecoder.deInit();
  if (mDeInterlace)
    mImageFx.deInit();
  mRender.deInit();

  mDeInterlace = false;
  mClock = NULL;
  }
//}}}

// private
//{{{
string cOmxVideo::getInterlaceModeString (enum OMX_INTERLACETYPE interlaceMode) {

  switch (interlaceMode) {
    case OMX_InterlaceProgressive:                 return "progressive";
    case OMX_InterlaceFieldSingleUpperFirst:       return "singleUpper";
    case OMX_InterlaceFieldSingleLowerFirst:       return "singleLower";
    case OMX_InterlaceFieldsInterleavedUpperFirst: return "interleaveUpper";
    case OMX_InterlaceFieldsInterleavedLowerFirst: return "interleavdLower";
    case OMX_InterlaceMixed:                       return "interlaceMixed";
    default:;
    }

  return "error";
  }
//}}}
//{{{
string cOmxVideo::getDeInterlaceModeString (eDeInterlaceMode deInterlaceMode) {

  switch (deInterlaceMode) {
    case eDeInterlaceOff:   return "deInterlaceOff";
    case eDeInterlaceAuto:  return "deInterlaceAuto";
    case eDeInterlaceForce: return "deInterlaceForce";
    case eDeInterlaceAutoAdv:  return "deInterlaceAutoAdv";
    case eDeInterlaceForceAdv: return "deInterlaceForceAdv";
    }
  return "error";
  }
//}}}

//{{{
bool cOmxVideo::sendDecoderExtraConfig() {

  cLog::log (LOGINFO, string(__func__) + " size:" + dec(mConfig.mHints.extrasize));

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if ((mConfig.mHints.extrasize > 0) && (mConfig.mHints.extradata != NULL)) {
    auto buffer = mDecoder.getInputBuffer();
    if (buffer == NULL) {
      cLog::log (LOGERROR, string(__func__) + " buffer");
      return false;
      }

    buffer->nOffset = 0;
    buffer->nFilledLen = min ((OMX_U32)mConfig.mHints.extrasize, buffer->nAllocLen);
    memset (buffer->pBuffer, 0, buffer->nAllocLen);
    memcpy (buffer->pBuffer, mConfig.mHints.extradata, buffer->nFilledLen);
    buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mDecoder.emptyThisBuffer (buffer)) {
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), buffer);
      return false;
      }
    }

  return true;
  }
//}}}
//{{{
bool cOmxVideo::setNaluFormat (enum AVCodecID codec, uint8_t* in_extradata, int in_extrasize) {
// valid avcC atom data always starts with the value 1 (version), otherwise annexb

  bool naluFormat = false;

  switch (codec) {
    case AV_CODEC_ID_H264:
      if (in_extrasize < 7 || in_extradata == NULL)
        naluFormat = true;
      else if (*in_extradata != 1)
        naluFormat = true;
    default: break;
    }

  if (naluFormat) {
    cLog::log (LOGINFO, string(__func__));

    OMX_NALSTREAMFORMATTYPE nalStreamFormat;
    OMX_INIT_STRUCTURE(nalStreamFormat);

    nalStreamFormat.nPortIndex = mDecoder.getInputPort();
    nalStreamFormat.eNaluFormat = OMX_NaluFormatStartCodes;
    if (mDecoder.setParam ((OMX_INDEXTYPE)OMX_IndexParamNalStreamFormatSelect, &nalStreamFormat)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " setNaluFormat");
      return false;
      }
    }

  return false;
  }
//}}}

//{{{
bool cOmxVideo::srcChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mSrcChanged)
    mDecoder.disablePort (mDecoder.getOutputPort(), true);
  //{{{  get port param
  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);

  portParam.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParam (OMX_IndexParamPortDefinition, &portParam))
    cLog::log (LOGERROR, string(__func__) + " getParam");
  //}}}
  //{{{  get src aspect
  OMX_CONFIG_POINTTYPE aspectParam;
  OMX_INIT_STRUCTURE(aspectParam);

  aspectParam.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParam (OMX_IndexParamBrcmPixelAspectRatio, &aspectParam))
    cLog::log (LOGERROR, string(__func__) + " getAspectRatio");

  if ((aspectParam.nX && aspectParam.nY) && !mConfig.mHints.forced_aspect)
    mPixelAspect = ((float)aspectParam.nX / (float)aspectParam.nY) / mConfig.mDisplayAspect;
  //}}}
  //{{{  get src interlace
  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);

  interlace.nPortIndex = mDecoder.getOutputPort();
  mDecoder.getConfig (OMX_IndexConfigCommonInterlace, &interlace);
  //}}}
  if (mSrcChanged) {
    //{{{  change aspect, re enablePort, return
    cLog::log (LOGINFO, "srcChanged again, change aspect only");
    logSrcChanged (portParam, interlace.eMode);

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

  //{{{  set mDeInterlace,mDeInterlaceAdv from src interlace and config.mDeInterlaceMode
  if (mConfig.mDeInterlaceMode == eDeInterlaceForce)
    mDeInterlace = true;
  else if (mConfig.mDeInterlaceMode == eDeInterlaceOff)
    mDeInterlace = false;
  else
    mDeInterlace = (interlace.eMode != OMX_InterlaceProgressive);

  mDeInterlaceAdv = mDeInterlace &&
                    ((mConfig.mDeInterlaceMode == eDeInterlaceAutoAdv) ||
                     (mConfig.mDeInterlaceMode == eDeInterlaceForceAdv));

  //}}}
  logSrcChanged (portParam, interlace.eMode);

  if (mDeInterlace) {
    //{{{  set deInterlace
    if (!mImageFx.init ("OMX.broadcom.image_fx", OMX_IndexParamImageInit))
      return false;

    if (!mDeInterlaceAdv) {
      // imageFx assumed 3 frames of context, release not needed for simple deinterlace
      OMX_PARAM_U32TYPE brcmExtraBuffers;
      OMX_INIT_STRUCTURE(brcmExtraBuffers);

      brcmExtraBuffers.nU32 = -2;
      if (mImageFx.setParam (OMX_IndexParamBrcmExtraBuffers, &brcmExtraBuffers)) {
        // error return
        cLog::log (LOGERROR, string(__func__) + " setExtraBuffers");
        return false;
        }
      }

    // configure deInterlace
    OMX_CONFIG_IMAGEFILTERPARAMSTYPE filterParams;
    OMX_INIT_STRUCTURE(filterParams);

    filterParams.nPortIndex = mImageFx.getOutputPort();
    filterParams.nNumParams = 4;
    filterParams.nParams[0] = 3;
    filterParams.nParams[1] = 0; // default frame interval
    filterParams.nParams[2] = 0; // half framerate
    filterParams.nParams[3] = 1; // use qpus
    filterParams.eImageFilter = mDeInterlaceAdv ?
                                  OMX_ImageFilterDeInterlaceAdvanced : OMX_ImageFilterDeInterlaceFast;
    if (mImageFx.setConfig (OMX_IndexConfigCommonImageFilterParameters, &filterParams)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " setImageFilters");
      return false;
      }

    mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(), &mImageFx, mImageFx.getInputPort());
    mTunnelImageFx.init (&mImageFx, mImageFx.getOutputPort(), &mScheduler, mScheduler.getInputPort());
    }
    //}}}
  else
    mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(), &mScheduler, mScheduler.getInputPort());

  //{{{  set displayRegion
  switch (mConfig.mHints.orientation) {
    case 1:   mTransform = OMX_DISPLAY_MIRROR_ROT0; break;
    case 90:  mTransform = OMX_DISPLAY_ROT90; break;
    case 91:  mTransform = OMX_DISPLAY_MIRROR_ROT90; break;
    case 180: mTransform = OMX_DISPLAY_ROT180; break;
    case 181: mTransform = OMX_DISPLAY_MIRROR_ROT180; break;
    case 270: mTransform = OMX_DISPLAY_ROT270; break;
    case 271: mTransform = OMX_DISPLAY_MIRROR_ROT270; break;
    default:  mTransform = OMX_DISPLAY_ROT0; break;
    }

  OMX_CONFIG_DISPLAYREGIONTYPE displayRegion;
  OMX_INIT_STRUCTURE(displayRegion);
  displayRegion.nPortIndex = mRender.getInputPort();
  displayRegion.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_ALPHA |
                                     OMX_DISPLAY_SET_TRANSFORM |
                                     OMX_DISPLAY_SET_LAYER |
                                     OMX_DISPLAY_SET_NUM);
  displayRegion.alpha = 255;
  displayRegion.transform = mTransform;
  displayRegion.layer = 0;
  displayRegion.num = mConfig.mDisplay;
  if (mRender.setConfig (OMX_IndexConfigDisplayRegion, &displayRegion)) {
    // error return
    cLog::log (LOGINFO1, string(__func__) + " setDisplayRegion");
    return false;
    }
  //}}}
  setVideoRect();
  if (mConfig.mHdmiClockSync) {
    //{{{  set latency
    OMX_CONFIG_LATENCYTARGETTYPE latencyConfig;
    OMX_INIT_STRUCTURE(latencyConfig);

    latencyConfig.nPortIndex = mRender.getInputPort();
    latencyConfig.bEnabled = OMX_TRUE;
    latencyConfig.nFilter = 2;
    latencyConfig.nTarget = 4000;
    latencyConfig.nShift = 3;
    latencyConfig.nSpeedFactor = -135;
    latencyConfig.nInterFactor = 500;
    latencyConfig.nAdjCap = 20;

    if (mRender.setConfig (OMX_IndexConfigLatencyTarget, &latencyConfig)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " setLatencyConfig");
      return false;
      }
    }
    //}}}

  // wire up components and startup
  mTunnelSched.init (&mScheduler, mScheduler.getOutputPort(), &mRender, mRender.getInputPort());
  mTunnelClock.init (mClock->getOmxCore(), mClock->getOmxCore()->getInputPort() + 1,
                     &mScheduler, mScheduler.getOutputPort() + 1);
  if (mTunnelClock.establish()) {
    //{{{  error return
    cLog::log (LOGERROR,  string(__func__) + " mTunnelClock.establish");
    return false;
    }
    //}}}
  if (mTunnelDecoder.establish()) {
    //{{{  error return
    cLog::log (LOGERROR,  string(__func__) + " mTunnelDecoder.establish");
    return false;
    }
    //}}}
  if (mDeInterlace) {
    if (mTunnelImageFx.establish()) {
      //{{{  error return
      cLog::log (LOGERROR,  string(__func__) + " mTunnelImageFx.establish");
      return false;
      }
      //}}}
    if (mImageFx.setState (OMX_StateExecuting)) {
      //{{{  error return
      cLog::log (LOGERROR,  string(__func__) + " mImageFx.setState");
      return false;
      }
      //}}}
    }
  if (mTunnelSched.establish()) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mTunnelSched.establish");
    return false;
    }
    //}}}
  if (mScheduler.setState (OMX_StateExecuting)) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + "mScheduler.setState");
    return false;
    }
    //}}}
  if (mRender.setState (OMX_StateExecuting)) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + "mRender.setState");
    return false;
    }
    //}}}

  mSrcChanged = true;
  return true;
  }
//}}}
//{{{
void cOmxVideo::logSrcChanged (OMX_PARAM_PORTDEFINITIONTYPE port,
                               enum OMX_INTERLACETYPE interlaceMode) {

  cLog::log (LOGINFO, "srcChanged - %dx%d@%.2f %s>%s>%s display:%d aspMode:%d pixAsp:%.2f",
                      port.format.video.nFrameWidth, port.format.video.nFrameHeight,
                      port.format.video.xFramerate / (float)(1<<16),
                      getInterlaceModeString (interlaceMode).c_str(),
                      getDeInterlaceModeString (mConfig.mDeInterlaceMode).c_str(),
                      mDeInterlace ? (mDeInterlaceAdv ? "deIntAdv" : "deInt") : "noDeInt",
                      mConfig.mDisplay,
                      mConfig.mAspectMode,
                      mPixelAspect);
  }
//}}}
