// cOmxAudio.cpp
//{{{  includes
#include <algorithm>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}

#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};
//{{{
static unsigned countBits (uint64_t value) {

  unsigned bits = 0;
  for (; value; ++bits)
    value &= value - 1;
  return bits;
  }
//}}}

cOmxAudio::~cOmxAudio() { deInit(); }

// gets
//{{{
bool cOmxAudio::hwDecode (AVCodecID codec) {

  switch (codec) {
    case AV_CODEC_ID_VORBIS:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode AV_CODEC_ID_VORBIS");
      return true;

    case AV_CODEC_ID_AAC:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode AV_CODEC_ID_AAC");
      return true;

    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode AV_CODEC_ID_MP2 / AV_CODEC_ID_MP3");
      return true;

    case AV_CODEC_ID_DTS:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode AV_CODEC_ID_DTS");
      return true;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode AV_CODEC_ID_AC3 / AV_CODEC_ID_EAC3");
      return true;

    default:
      return false;
    }
  }
//}}}
//{{{
float cOmxAudio::getDelay() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  double stamp = DVD_NOPTS_VALUE;
  if (mLastPts != DVD_NOPTS_VALUE && mAvClock)
    stamp = mAvClock->getMediaTime();

  // if possible the delay is current media time - time of last submitted packet
  if (stamp != DVD_NOPTS_VALUE)
    return (mLastPts - stamp) * (1.0 / DVD_TIME_BASE);
  else { // just measure the input fifo
    unsigned int used = mOmxDecoder.getInputBufferSize() - mOmxDecoder.getInputBufferSpace();
    return mInputBytesPerSec ? (float)used / (float)mInputBytesPerSec : 0.f;
    }
  }
//}}}
//{{{
float cOmxAudio::getCacheTotal() {

  float audioPlusBuffer = mConfig.mHints.samplerate ? (32.f * 512.f / mConfig.mHints.samplerate) : 0.f;
  float inputBuffer = mInputBytesPerSec ?
    (float)mOmxDecoder.getInputBufferSize() / (float)mInputBytesPerSec : 0;

  return AUDIO_BUFFER_SECONDS + inputBuffer + audioPlusBuffer;
  }
//}}}
//{{{
unsigned int cOmxAudio::getAudioRenderingLatency() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);
  if (mOmxRenderAnalog.isInit()) {
    param.nPortIndex = mOmxRenderAnalog.getInputPort();
    if (mOmxRenderAnalog.getConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, "cOmxAudio::getAudioRenderingLatency Analog OMX_IndexConfigAudioRenderingLatency");
      return 0;
      }
    }

  else if (mOmxRenderHdmi.isInit()) {
    param.nPortIndex = mOmxRenderHdmi.getInputPort();
    if (mOmxRenderHdmi.getConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, "cOmxAudio::getAudioRenderingLatency hdmi OMX_IndexConfigAudioRenderingLatency");
      return 0;
      }
    }

  return param.nU32;
  }
//}}}
//{{{
float cOmxAudio::getMaxLevel (double& pts) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_CONFIG_BRCMAUDIOMAXSAMPLE param;
  OMX_INIT_STRUCTURE(param);
  if (mOmxDecoder.isInit()) {
    param.nPortIndex = mOmxDecoder.getInputPort();
    if (mOmxDecoder.getConfig(OMX_IndexConfigBrcmAudioMaxSample, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, "cOmxAudio::GetMaxLevel OMX_IndexConfigBrcmAudioMaxSample");
      return 0;
      }
    }

  pts = fromOmxTime (param.nTimeStamp);
  return (float)param.nMaxSample * (100.f / (1<<15));
  }
//}}}
//{{{
uint64_t cOmxAudio::getChannelLayout (enum PCMLayout layout) {

  uint64_t layouts[] = {
    /* 2.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT,
    /* 2.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 3.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER,
    /* 3.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_LOW_FREQUENCY,
    /* 4.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 4.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 5.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 5.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT |
              1<<PCM_LOW_FREQUENCY,
    /* 7.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT |
              1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 7.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT |
              1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY
    };

  return (int)layout < 10 ? layouts[(int)layout] : 0;
  }
//}}}
//{{{
bool cOmxAudio::isEOS() {

  unsigned int latency = getAudioRenderingLatency();

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos && !(mOmxDecoder.isEOS() && latency == 0))
    return false;

  if (mSubmittedEos) {
    cLog::log (LOGINFO, "isEOS");
    mSubmittedEos = false;
    }

  return true;
  }
//}}}

// sets
//{{{
void cOmxAudio::setMute (bool mute) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mMute = mute;
  if (mSettingsChanged)
    updateAttenuation();
  }
//}}}
//{{{
void cOmxAudio::setVolume (float volume) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mCurrentVolume = volume;
  if (mSettingsChanged)
    updateAttenuation();
  }
//}}}
//{{{
void cOmxAudio::setDynamicRangeCompression (float drc) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mDrc = powf (10.f, drc);
  if (mSettingsChanged)
    updateAttenuation();
  }
//}}}
//{{{
void cOmxAudio::setCodingType (AVCodecID codec) {

  switch (codec) {
    case AV_CODEC_ID_DTS:
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType OMX_AUDIO_CodingDTS");
      mEncoding = OMX_AUDIO_CodingDTS;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType OMX_AUDIO_CodingDDP");
      mEncoding = OMX_AUDIO_CodingDDP;
      break;

    default:
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType OMX_AUDIO_CodingPCM");
      mEncoding = OMX_AUDIO_CodingPCM;
      break;
    }
  }
//}}}

// actions
//{{{
bool cOmxAudio::init (cOmxClock* clock, const cOmxAudioConfig &config,
                            uint64_t channelMap, unsigned int uiBitsPerSample) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  deInit();

  mAvClock = clock;
  mConfig = config;
  if (mConfig.mPassthrough) // passthrough overwrites hw decode
    mConfig.mHwDecode = false;
  else if (mConfig.mHwDecode) // check again if we are capable to hw decode the format
    mConfig.mHwDecode = canHwDecode (mConfig.mHints.codec);
  if (mConfig.mPassthrough || mConfig.mHwDecode)
    setCodingType (mConfig.mHints.codec);
  else
    setCodingType (AV_CODEC_ID_PCM_S16LE);

  mOmxClock = mAvClock->getOmxClock();
  mDrc = 0;

  mNumInputChannels = countBits (channelMap);
  memset (mInputChannels, 0x0, sizeof(mInputChannels));
  memset (mOutputChannels, 0x0, sizeof(mOutputChannels));
  memset (&mWaveHeader, 0x0, sizeof(mWaveHeader));
  mWaveHeader.Format.nChannels = 2;
  mWaveHeader.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

  if (!mConfig.mPassthrough && channelMap) {
    //{{{  set the input format, and get the channel layout so we know what we need to open
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];
    // force out layout to stereo if input is not multichannel - it gives the receiver a chance to upmix
    if (channelMap == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT) || channelMap == AV_CH_FRONT_CENTER)
      mConfig.mLayout = PCM_LAYOUT_2_0;
    buildChannelMap (inLayout, channelMap);
    mNumOutputChannels = buildChannelMapCEA (outLayout, getChannelLayout(mConfig.mLayout));

    cPcmRemap remap;
    remap.reset();
    remap.setInputFormat (mNumInputChannels, inLayout, uiBitsPerSample / 8,
                          mConfig.mHints.samplerate, mConfig.mLayout, mConfig.mBoostOnDownmix);
    remap.setOutputFormat (mNumOutputChannels, outLayout, false);
    remap.getDownmixMatrix (mDownmixMatrix);
    mWaveHeader.dwChannelMask = channelMap;

    buildChannelMapOMX (mInputChannels, channelMap);
    buildChannelMapOMX (mOutputChannels, getChannelLayout (mConfig.mLayout));
    }
    //}}}

  mBitsPerSample = uiBitsPerSample;
  mBytesPerSec = mConfig.mHints.samplerate * 2 << rounded_up_channels_shift[mNumInputChannels];
  mBufferLen = mBytesPerSec * AUDIO_BUFFER_SECONDS;
  mInputBytesPerSec = mConfig.mHints.samplerate * mBitsPerSample * mNumInputChannels >> 3;

  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so 48K input buffer will fit the output buffer
  mChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (mNumInputChannels * mBitsPerSample)
                 >> (rounded_up_channels_shift[mNumInputChannels] + 4);

  mWaveHeader.Samples.wSamplesPerBlock = 0;
  mWaveHeader.Format.nChannels = mNumInputChannels;
  mWaveHeader.Format.nBlockAlign = mNumInputChannels * (mBitsPerSample >> 3);
  // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
  mWaveHeader.Format.wFormatTag = mBitsPerSample == 32 ? 0x8000 : WAVE_FORMAT_PCM;
  mWaveHeader.Format.nSamplesPerSec = mConfig.mHints.samplerate;
  mWaveHeader.Format.nAvgBytesPerSec = mBytesPerSec;
  mWaveHeader.Format.wBitsPerSample = mBitsPerSample;
  mWaveHeader.Samples.wValidBitsPerSample = mBitsPerSample;
  mWaveHeader.Format.cbSize = 0;
  mWaveHeader.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

  if (!mOmxDecoder.init ("OMX.broadcom.audio_decode", OMX_IndexParamAudioInit))
    return false;

  OMX_CONFIG_BOOLEANTYPE boolType;
  OMX_INIT_STRUCTURE(boolType);
  if (mConfig.mPassthrough)
    boolType.bEnabled = OMX_TRUE;
  else
    boolType.bEnabled = OMX_FALSE;
  if (mOmxDecoder.setParameter (OMX_IndexParamBrcmDecoderPassThrough, &boolType) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " OMX_IndexParamBrcmDecoderPassThrough");
    return false;
    }
    //}}}

  // set up the number/size of buffers for decoder input
  OMX_PARAM_PORTDEFINITIONTYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = mOmxDecoder.getInputPort();
  if (mOmxDecoder.getParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " OMX_IndexParamPortDefinition");
    return false;
    }
    //}}}

  port_param.format.audio.eEncoding = mEncoding;
  port_param.nBufferSize = mChunkLen;
  port_param.nBufferCountActual = max (port_param.nBufferCountMin, 16U);
  if (mOmxDecoder.setParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " OMX_IndexParamPortDefinition");
    return false;
    }
    //}}}

  // set up the number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = mOmxDecoder.getOutputPort();
  if (mOmxDecoder.getParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " get OMX_IndexParamPortDefinition out");
    return false;
    }
    //}}}

  port_param.nBufferCountActual = max ((unsigned int)port_param.nBufferCountMin, mBufferLen / port_param.nBufferSize);
  if (mOmxDecoder.setParameter (OMX_IndexParamPortDefinition, &port_param) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " set OMX_IndexParamPortDefinition out");
    return false;
    }
    //}}}

  OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = mOmxDecoder.getInputPort();
  formatType.eEncoding = mEncoding;
  if (mOmxDecoder.setParameter (OMX_IndexParamAudioPortFormat, &formatType) != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " OMX_IndexParamAudioPortFormat");
    return false;
    }
    //}}}
  if (mOmxDecoder.allocInputBuffers() != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " alloc buffers");
    return false;
    }
    //}}}
  if (mOmxDecoder.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  return error
    cLog::log (LOGERROR, string(__func__) + " OMX_StateExecuting");
    return false;
    }
    //}}}
  if (mEncoding == OMX_AUDIO_CodingPCM) {
    //{{{  declare buffer
    auto omx_buffer = mOmxDecoder.getInputBuffer();
    if (omx_buffer == NULL) {
       //  error, return
      cLog::log (LOGERROR, string(__func__) + " error");
      return false;
      }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = min (sizeof(mWaveHeader), omx_buffer->nAllocLen);
    memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy ((unsigned char*)omx_buffer->pBuffer, &mWaveHeader, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mOmxDecoder.emptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
      //  error, return
      cLog::log (LOGERROR, string(__func__) + " OMX_EmptyThisBuffer");
      mOmxDecoder.decoderEmptyBufferDone (mOmxDecoder.getComponent(), omx_buffer);
      return false;
      }
    }
    //}}}
  else if (mConfig.mHwDecode) {
    //{{{  send decoder config
    if (mConfig.mHints.extrasize > 0 && mConfig.mHints.extradata != NULL) {
      auto omx_buffer = mOmxDecoder.getInputBuffer();
      if (omx_buffer == NULL) {
        // error, return
        cLog::log (LOGERROR, string(__func__) + " buffer error");
        return false;
        }

      omx_buffer->nOffset = 0;
      omx_buffer->nFilledLen = min((OMX_U32)mConfig.mHints.extrasize, omx_buffer->nAllocLen);
      memset ((unsigned char*)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy ((unsigned char*)omx_buffer->pBuffer, mConfig.mHints.extradata, omx_buffer->nFilledLen);
      omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
      if (mOmxDecoder.emptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
        // error, return
        cLog::log (LOGERROR, string(__func__) + " OMX_EmptyThisBuffer");
        mOmxDecoder.decoderEmptyBufferDone (mOmxDecoder.getComponent(), omx_buffer);
        return false;
        }
      }
    }
    //}}}
  if (mOmxDecoder.badState())
    return false;

  OMX_INIT_STRUCTURE(mPcmInput);
  mPcmInput.nPortIndex = mOmxDecoder.getInputPort();
  memcpy (mPcmInput.eChannelMapping, mInputChannels, sizeof(mInputChannels));
  mPcmInput.eNumData = OMX_NumericalDataSigned;
  mPcmInput.eEndian = OMX_EndianLittle;
  mPcmInput.bInterleaved = OMX_TRUE;
  mPcmInput.nBitPerSample = mBitsPerSample;
  mPcmInput.ePCMMode = OMX_AUDIO_PCMModeLinear;
  mPcmInput.nChannels = mNumInputChannels;
  mPcmInput.nSamplingRate = mConfig.mHints.samplerate;

  mSettingsChanged = false;
  mSetStartTime  = true;
  mSubmittedEos = false;
  mFailedEos = false;
  mLastPts = DVD_NOPTS_VALUE;
  mSubmitted = 0.f;
  mMaxLevel = 0.f;

  cLog::log (LOGINFO1, "cOmxAudio::Initialize Input bitsPer:%d rate:%d ch:%d buffer size:%d bytesPer:%d",
                       (int)mPcmInput.nBitPerSample, (int)mPcmInput.nSamplingRate,
                       (int)mPcmInput.nChannels, mBufferLen, mInputBytesPerSec);

  printPCM (&mPcmInput, "input");
  cLog::log (LOGINFO1, "cOmxAudio::Initialize dev:%s pass:%d hw:%d",
                       mConfig.mDevice.c_str(), mConfig.mPassthrough, mConfig.mHwDecode);

  return true;
  }
//}}}
//{{{
void cOmxAudio::buildChannelMap (enum PCMChannels* channelMap, uint64_t layout) {

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

  while (index < OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;
  }
//}}}
//{{{
// See CEA spec: Table 20, Audio InfoFrame data byte 4 for the ordering here
int cOmxAudio::buildChannelMapCEA (enum PCMChannels* channelMap, uint64_t layout) {

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT   ) channelMap[index++] = PCM_FRONT_LEFT;
  if (layout & AV_CH_FRONT_RIGHT  ) channelMap[index++] = PCM_FRONT_RIGHT;
  if (layout & AV_CH_LOW_FREQUENCY) channelMap[index++] = PCM_LOW_FREQUENCY;
  if (layout & AV_CH_FRONT_CENTER ) channelMap[index++] = PCM_FRONT_CENTER;
  if (layout & AV_CH_BACK_LEFT    ) channelMap[index++] = PCM_BACK_LEFT;
  if (layout & AV_CH_BACK_RIGHT   ) channelMap[index++] = PCM_BACK_RIGHT;
  if (layout & AV_CH_SIDE_LEFT    ) channelMap[index++] = PCM_SIDE_LEFT;
  if (layout & AV_CH_SIDE_RIGHT   ) channelMap[index++] = PCM_SIDE_RIGHT;

  while (index < OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;

  int num_channels = 0;
  for (index = 0; index < OMX_AUDIO_MAXCHANNELS; index++)
    if (channelMap[index] != PCM_INVALID)
       num_channels = index+1;

  // round up to power of 2
  num_channels = num_channels > 4 ? 8 : num_channels > 2 ? 4 : num_channels;
  return num_channels;
  }
//}}}
//{{{
void cOmxAudio::buildChannelMapOMX (enum OMX_AUDIO_CHANNELTYPE* channelMap, uint64_t layout) {

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

  while (index < OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = OMX_AUDIO_ChannelNone;
  }
//}}}
//{{{
bool cOmxAudio::portSettingsChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mSettingsChanged) {
    mOmxDecoder.disablePort (mOmxDecoder.getOutputPort(), true);
    mOmxDecoder.enablePort (mOmxDecoder.getOutputPort(), true);
    return true;
    }

  if (!mConfig.mPassthrough)
    if (!mOmxMixer.init ("OMX.broadcom.audio_mixer", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both")
    if (!mOmxSplitter.init ("OMX.broadcom.audio_splitter", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:local")
    if (!mOmxRenderAnalog.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:hdmi")
    if (!mOmxRenderHdmi.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;

  updateAttenuation();

  if (mOmxMixer.isInit()) {
    // setup mixer output
    OMX_INIT_STRUCTURE(mPcmOutput);
    mPcmOutput.nPortIndex = mOmxDecoder.getOutputPort();
    if (mOmxDecoder.getParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::PortSettingsChanged  GetParameter");
      return false;
      }
      //}}}

    memcpy (mPcmOutput.eChannelMapping, mOutputChannels, sizeof(mOutputChannels));

    // round up to power of 2
    mPcmOutput.nChannels = mNumOutputChannels > 4 ? 8 : mNumOutputChannels > 2 ? 4 : mNumOutputChannels;

    // limit samplerate (through resampling) if requested
    mPcmOutput.nSamplingRate = min (max ((int)mPcmOutput.nSamplingRate, 8000), 192000);

    mPcmOutput.nPortIndex = mOmxMixer.getOutputPort();
    if (mOmxMixer.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::PortSettingsChanged SetParameter");
      return false;
      }
      //}}}

    cLog::log (LOGINFO1, "cOmxAudio::portSettingsChanged Output bps:%d rate:%d ch:%d buffer size:%d bps:%d",
                         (int)mPcmOutput.nBitPerSample, (int)mPcmOutput.nSamplingRate,
                         (int)mPcmOutput.nChannels, mBufferLen, mBytesPerSec);
    printPCM (&mPcmOutput, "output");

    if (mOmxSplitter.isInit() ) {
      mPcmOutput.nPortIndex = mOmxSplitter.getInputPort();
      if (mOmxSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxSplitter SetParameter");
        return false;
        }
        //}}}
      mPcmOutput.nPortIndex = mOmxSplitter.getOutputPort();
      if (mOmxSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxSplitter SetParameter");
        return false;
        }
        //}}}
      mPcmOutput.nPortIndex = mOmxSplitter.getOutputPort() + 1;
      if (mOmxSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxSplitter SetParameter");
        return false;
        }
        //}}}
      }

    if (mOmxRenderAnalog.isInit() ) {
      mPcmOutput.nPortIndex = mOmxRenderAnalog.getInputPort();
      if (mOmxRenderAnalog.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxRenderAnalog SetParameter");
        return false;
        }
        //}}}
      }
    if (mOmxRenderHdmi.isInit() ) {
      mPcmOutput.nPortIndex = mOmxRenderHdmi.getInputPort();
      if (mOmxRenderHdmi.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxRenderhdmi SetParameter");
        return false;
        }
        //}}}
      }
    }

  if (mOmxRenderAnalog.isInit()) {
    mOmxTunnelClockAnalog.init (mOmxClock, mOmxClock->getInputPort(),
                                &mOmxRenderAnalog, mOmxRenderAnalog.getInputPort()+1);

    if (mOmxTunnelClockAnalog.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxTunnel_clock_Analog.Establish");
      return false;
      }
      //}}}
    mOmxRenderAnalog.resetEos();
    }

  if (mOmxRenderHdmi.isInit() ) {
    mOmxTunnelClockHdmi.init (
      mOmxClock, mOmxClock->getInputPort() + (mOmxRenderAnalog.isInit() ? 2 : 0),
      &mOmxRenderHdmi, mOmxRenderHdmi.getInputPort()+1);

    if (mOmxTunnelClockHdmi.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log(LOGERROR, "cOmxAudio::portSettingsChanged mOmxTunnel_clock_hdmi.establish");
      return false;
      }
      //}}}
    mOmxRenderHdmi.resetEos();
    }

  if (mOmxRenderAnalog.isInit() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps,
    // it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches,
    // but can affect video/display sync when in dual audio mode, make Analogue the slave
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = mConfig.mIsLive || mConfig.mDevice == "omx:both" ? OMX_FALSE : OMX_TRUE;
    if (mOmxRenderAnalog.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char*)audioDest.sName, "local", sizeof(audioDest.sName));
    if (mOmxRenderAnalog.setConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxRenderAnalog.setConfig");
      return false;
      }
      //}}}
    }

  if (mOmxRenderHdmi.isInit() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps,
    // it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches,
    // but can affect video/display sync
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = mConfig.mIsLive ? OMX_FALSE:OMX_TRUE;
    if (mOmxRenderHdmi.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
      return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char *)audioDest.sName, "hdmi", strlen("hdmi"));
    if (mOmxRenderHdmi.setConfig (OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::PortSettingsChanged mOmxRenderhdmi.setConfig");
      return false;
      }
      //}}}
    }

  if (mOmxSplitter.isInit() ) {
    mOmxTunnelSplitterAnalog.init (&mOmxSplitter, mOmxSplitter.getOutputPort(),
                                   &mOmxRenderAnalog, mOmxRenderAnalog.getInputPort());
    if (mOmxTunnelSplitterAnalog.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::PortSettingsChanged  mOmxTunnelSplitterAnalog.establish");
      return false;
      }
      //}}}
    mOmxTunnelSplitterHdmi.init (&mOmxSplitter, mOmxSplitter.getOutputPort() + 1,
                                 &mOmxRenderHdmi, mOmxRenderHdmi.getInputPort());
    if (mOmxTunnelSplitterHdmi.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::PortSettingsChanged mOmxTunnelSplitterhdmi.establish");
      return false;
      }
      //}}}
    }

  if (mOmxMixer.isInit()) {
    mOmxTunnelDecoder.init (&mOmxDecoder, mOmxDecoder.getOutputPort(),
                            &mOmxMixer, mOmxMixer.getInputPort());
    if (mOmxSplitter.isInit())
      mOmxTunnelMixer.init (&mOmxMixer, mOmxMixer.getOutputPort(),
                            &mOmxSplitter, mOmxSplitter.getInputPort());
    else {
      if (mOmxRenderAnalog.isInit())
        mOmxTunnelMixer.init (&mOmxMixer, mOmxMixer.getOutputPort(),
                              &mOmxRenderAnalog, mOmxRenderAnalog.getInputPort());
      if (mOmxRenderHdmi.isInit())
        mOmxTunnelMixer.init (&mOmxMixer, mOmxMixer.getOutputPort(),
                              &mOmxRenderHdmi, mOmxRenderHdmi.getInputPort());
      }
    cLog::log (LOGINFO1, "cOmxAudio::portSettingsChanged bits:%d mode:%d ch:%d srate:%d nopassthrough",
               (int)mPcmInput.nBitPerSample, mPcmInput.ePCMMode,
               (int)mPcmInput.nChannels, (int)mPcmInput.nSamplingRate);
    }
  else {
    if (mOmxRenderAnalog.isInit())
      mOmxTunnelDecoder.init (&mOmxDecoder, mOmxDecoder.getOutputPort(),
                              &mOmxRenderAnalog, mOmxRenderAnalog.getInputPort());
    else if (mOmxRenderHdmi.isInit())
      mOmxTunnelDecoder.init (&mOmxDecoder, mOmxDecoder.getOutputPort(),
                              &mOmxRenderHdmi, mOmxRenderHdmi.getInputPort());
     cLog::log (LOGINFO1, "cOmxAudio::portSettingsChanged bits:%d mode:%d ch:%d srate:%d passthrough", 0, 0, 0, 0);
     }

  if (mOmxTunnelDecoder.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxTunnel_decoder.establish");
    return false;
    }
    //}}}
  if (mOmxMixer.isInit()) {
    if (mOmxMixer.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxMixer OMX_StateExecuting");
      return false;
      }
      //}}}
    }
  if (mOmxMixer.isInit())
    if (mOmxTunnelMixer.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxTunnel_decoder.establish");
      return false;
      }
      //}}}
  if (mOmxSplitter.isInit())
    if (mOmxSplitter.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxSplitter OMX_StateExecuting");
      return false;
      }
      //}}}
  if (mOmxRenderAnalog.isInit())
    if (mOmxRenderAnalog.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxRenderAnalog OMX_StateExecuting");
      return false;
      }
      //}}}
  if (mOmxRenderHdmi.isInit())
    if (mOmxRenderHdmi.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::portSettingsChanged mOmxRenderhdmi OMX_StateExecuting");
      return false;
      }
      //}}}

  mSettingsChanged = true;
  return true;
  }
//}}}

//{{{
unsigned int cOmxAudio::addPackets (const void* data, unsigned int len,
                                    double dts, double pts, unsigned int frame_size) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  unsigned pitch =
    (mConfig.mPassthrough || mConfig.mHwDecode) ? 1:(mBitsPerSample >> 3) * mNumInputChannels;
  unsigned int demuxer_samples = len / pitch;
  unsigned int demuxer_samples_sent = 0;
  auto demuxer_content = (uint8_t *)data;

  OMX_BUFFERHEADERTYPE* omxBuffer = NULL;
  while (demuxer_samples_sent < demuxer_samples) {
    omxBuffer = mOmxDecoder.getInputBuffer (200); // 200ms timeout
    if (omxBuffer == NULL) {
      //{{{  error return
      cLog::log (LOGERROR, "cOmxAudio::addPackets timeout");
      return len;
      }
      //}}}

    omxBuffer->nOffset = 0;
    omxBuffer->nFlags  = 0;

    // we want audio_decode output buffer size to be no more than AUDIO_DECODE_OUTPUT_BUFFER.
    // it will be 16-bit and rounded up to next power of 2 in channels
    unsigned int max_buffer = AUDIO_DECODE_OUTPUT_BUFFER *
      (mNumInputChannels * mBitsPerSample) >> (rounded_up_channels_shift[mNumInputChannels] + 4);
    unsigned int remaining = demuxer_samples-demuxer_samples_sent;
    unsigned int samples_space = min(max_buffer, omxBuffer->nAllocLen)/pitch;
    unsigned int samples = min(remaining, samples_space);

    omxBuffer->nFilledLen = samples * pitch;

    unsigned int frames = frame_size ? len/frame_size:0;
    if ((samples < demuxer_samples || frames > 1) &&
        (mBitsPerSample == 32) &&
        !(mConfig.mPassthrough || mConfig.mHwDecode)) {
      const unsigned int sample_pitch = mBitsPerSample >> 3;
      const unsigned int frame_samples = frame_size / pitch;
      const unsigned int plane_size = frame_samples * sample_pitch;
      const unsigned int out_plane_size = samples * sample_pitch;
      for (unsigned int sample = 0; sample < samples;) {
        unsigned int frame = (demuxer_samples_sent + sample) / frame_samples;
        unsigned int sample_in_frame = (demuxer_samples_sent + sample) - frame * frame_samples;
        int out_remaining = min(min(frame_samples - sample_in_frame, samples), samples-sample);
        auto src = demuxer_content + frame*frame_size + sample_in_frame * sample_pitch;
        auto dst = (uint8_t*)omxBuffer->pBuffer + sample * sample_pitch;
        for (unsigned int channel = 0; channel < mNumInputChannels; channel++) {
          memcpy (dst, src, out_remaining * sample_pitch);
          src += plane_size;
          dst += out_plane_size;
          }
        sample += out_remaining;
        }
      }
    else
      memcpy (omxBuffer->pBuffer, demuxer_content + demuxer_samples_sent * pitch, omxBuffer->nFilledLen);

    uint64_t val = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;
    if (mSetStartTime) {
      omxBuffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
      mLastPts = pts;
      cLog::log (LOGINFO1, "cOmxAudio::addPackets setStartTime:%f", (float)val / DVD_TIME_BASE);
      mSetStartTime = false;
      }
    else {
      if (pts == DVD_NOPTS_VALUE) {
        omxBuffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        mLastPts = pts;
        }
      else if (mLastPts != pts) {
        if (pts > mLastPts)
          mLastPts = pts;
        else
          omxBuffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        }
      else if (mLastPts == pts)
        omxBuffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }

    omxBuffer->nTimeStamp = toOmxTime (val);
    demuxer_samples_sent += samples;
    if (demuxer_samples_sent == demuxer_samples)
      omxBuffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    if (mOmxDecoder.emptyThisBuffer (omxBuffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, string(__func__) + " OMX_EmptyThisBuffer");
      mOmxDecoder.decoderEmptyBufferDone (mOmxDecoder.getComponent(), omxBuffer);
      return 0;
      }

    if (mOmxDecoder.waitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone)
      if (!portSettingsChanged())
        cLog::log (LOGERROR, "cOmxAudio::AddPackets PortSettingsChanged");
    }

  mSubmitted += (float)demuxer_samples / mConfig.mHints.samplerate;
  updateAttenuation();
  return len;
  }
//}}}
//{{{
void cOmxAudio::submitEOS() {

  cLog::log (LOGINFO, "submitEOS");

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSubmittedEos = true;
  mFailedEos = false;

  auto* omx_buffer = mOmxDecoder.getInputBuffer(1000);
  if (omx_buffer == NULL) {
    cLog::log (LOGERROR, string(__func__) + " buffer");
    mFailedEos = true;
    return;
    }
  omx_buffer->nOffset = 0;
  omx_buffer->nFilledLen = 0;
  omx_buffer->nTimeStamp = toOmxTime (0LL);
  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  if (mOmxDecoder.emptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
    cLog::log (LOGERROR, string(__func__) + " OMX_EmptyThisBuffer");
    mOmxDecoder.decoderEmptyBufferDone (mOmxDecoder.getComponent(), omx_buffer);
    return;
    }
  }
//}}}
//{{{
void cOmxAudio::flush() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mOmxDecoder.flushAll();
  if (mOmxMixer.isInit() )
    mOmxMixer.flushAll();
  if (mOmxSplitter.isInit() )
    mOmxSplitter.flushAll();

  if (mOmxRenderAnalog.isInit() )
    mOmxRenderAnalog.flushAll();
  if (mOmxRenderHdmi.isInit() )
    mOmxRenderHdmi.flushAll();

  while (!mAmpQueue.empty())
    mAmpQueue.pop_front();

  if (mOmxRenderAnalog.isInit() )
    mOmxRenderAnalog.resetEos();
  if (mOmxRenderHdmi.isInit() )
    mOmxRenderHdmi.resetEos();

  mLastPts = DVD_NOPTS_VALUE;
  mSubmitted = 0.f;
  mMaxLevel = 0.f;
  mSetStartTime  = true;
  }
//}}}
//{{{
bool cOmxAudio::deInit() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mOmxTunnelClockAnalog.isInit() )
    mOmxTunnelClockAnalog.deEstablish();
  if (mOmxTunnelClockHdmi.isInit() )
    mOmxTunnelClockHdmi.deEstablish();

  // ignore expected errors on teardown
  if (mOmxMixer.isInit() )
    mOmxMixer.ignoreNextError (OMX_ErrorPortUnpopulated);
  else {
    if (mOmxRenderHdmi.isInit() )
      mOmxRenderHdmi.ignoreNextError (OMX_ErrorPortUnpopulated);
    if (mOmxRenderAnalog.isInit() )
      mOmxRenderAnalog.ignoreNextError (OMX_ErrorPortUnpopulated);
    }

  mOmxTunnelDecoder.deEstablish();
  if (mOmxTunnelMixer.isInit() )
    mOmxTunnelMixer.deEstablish();
  if (mOmxTunnelSplitterHdmi.isInit() )
    mOmxTunnelSplitterHdmi.deEstablish();
  if (mOmxTunnelSplitterAnalog.isInit() )
    mOmxTunnelSplitterAnalog.deEstablish();

  mOmxDecoder.flushInput();

  mOmxDecoder.deInit();
  if (mOmxMixer.isInit())
    mOmxMixer.deInit();
  if (mOmxSplitter.isInit())
    mOmxSplitter.deInit();
  if (mOmxRenderHdmi.isInit())
    mOmxRenderHdmi.deInit();
  if (mOmxRenderAnalog.isInit())
    mOmxRenderAnalog.deInit();

  mBytesPerSec = 0;
  mBufferLen = 0;

  mOmxClock = NULL;
  mAvClock = NULL;

  while (!mAmpQueue.empty())
    mAmpQueue.pop_front();

  mLastPts = DVD_NOPTS_VALUE;
  mSubmitted = 0.f;
  mMaxLevel = 0.f;

  return true;
  }
//}}}

// private
//{{{
bool cOmxAudio::canHwDecode (AVCodecID codec) {

  switch(codec) {
    //case AV_CODEC_ID_VORBIS:
    //  cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingVORBIS");
    //  m_eEncoding = OMX_AUDIO_CodingVORBIS;
    //  mConfig.mHwDecode = true;
    //  break;

    //case AV_CODEC_ID_AAC:
    //  cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingAAC");
    //  m_eEncoding = OMX_AUDIO_CodingAAC;
    //  mConfig.mHwDecode = true;
    //  break;

    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingMP3");
      mEncoding = OMX_AUDIO_CodingMP3;
      mConfig.mHwDecode = true;
      break;

    case AV_CODEC_ID_DTS:
      cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingDTS");
      mEncoding = OMX_AUDIO_CodingDTS;
      mConfig.mHwDecode = true;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingDDP");
      mEncoding = OMX_AUDIO_CodingDDP;
      mConfig.mHwDecode = true;
      break;

    default:
      cLog::log (LOGINFO1, "cOmxAudio::CanHWDecode OMX_AUDIO_CodingPCM");
      mEncoding = OMX_AUDIO_CodingPCM;
      mConfig.mHwDecode = false;
      break;
    }

  return mConfig.mHwDecode;
  }
//}}}

//{{{
bool cOmxAudio::applyVolume() {

  float ac3Gain = 12.f;

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mConfig.mPassthrough)
    return false;

  // the Analogue volume is too quiet for some. Allow use of an advancedsetting to boost this (at risk of distortion) (deprecated)
  float volume = mMute ? 0.f : mCurrentVolume;
  double gain = pow(10, (ac3Gain - 12.f) / 20.0);
  const float* coeff = mDownmixMatrix;

  OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
  OMX_INIT_STRUCTURE(mix);
  assert (sizeof(mix.coeff) / sizeof(mix.coeff[0]) == 64);
  if (mDrc != 1.0) {
    // reduce scaling so overflow can be seen
    for (size_t i = 0; i < 8*8; ++i)
      mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * 0.01f));
    mix.nPortIndex = mOmxDecoder.getInputPort();
    if (mOmxDecoder.setConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
      cLog::log (LOGERROR, string(__func__) + " OMX_IndexConfigBrcmAudioDownmixCoefficients");
      return false;
      }
    }

  for (size_t i = 0; i < 8*8; ++i)
    mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * volume * mDrc * mAttenuation));

  mix.nPortIndex = mOmxMixer.getInputPort();
  if (mOmxMixer.setConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
    cLog::log (LOGERROR, string(__func__) + " OMX_IndexConfigBrcmAudioDownmixCoefficients");
    return false;
    }

  cLog::log (LOGINFO2, "cOmxAudio::applyVolume vol:%.2f drc:%.2f att:%.2f",
                       volume, mDrc, mAttenuation);
  return true;
  }
//}}}
//{{{
void cOmxAudio::updateAttenuation() {

  if (mDrc == 1.f) {
    applyVolume();
    return;
    }

  double level_pts = 0.0;
  float level = getMaxLevel(level_pts);
  if (level_pts != 0.0) {
    amplitudes_t v;
    v.level = level;
    v.pts = level_pts;
    mAmpQueue.push_back(v);
    }

  double stamp = mAvClock->getMediaTime();
  // discard too old data
  while (!mAmpQueue.empty()) {
    amplitudes_t &v = mAmpQueue.front();
    /* we'll also consume if queue gets unexpectedly long to avoid filling memory */
    if (v.pts == DVD_NOPTS_VALUE || v.pts < stamp || v.pts - stamp > DVD_SEC_TO_TIME(15.0))
      mAmpQueue.pop_front();
    else
      break;
    }

  float maxlevel = 0.f, imminent_maxlevel = 0.f;
  for (int i=0; i < (int)mAmpQueue.size(); i++) {
    amplitudes_t &v = mAmpQueue[i];
    maxlevel = max(maxlevel, v.level);
    // check for maximum volume in next 200ms
    if (v.pts != DVD_NOPTS_VALUE && v.pts < stamp + DVD_SEC_TO_TIME(0.2))
      imminent_maxlevel = max (imminent_maxlevel, v.level);
    }

  if (maxlevel != 0.0) {
    float mLimiterHold = 0.025f;
    float mLimiterRelease = 0.100f;
    float alpha_h = -1.f / (0.025f*log10f(0.999f));
    float alpha_r = -1.f / (0.100f*log10f(0.900f));
    float decay  = powf (10.f, -1.f / (alpha_h * mLimiterHold));
    float attack = powf (10.f, -1.f / (alpha_r * mLimiterRelease));
    // if we are going to clip imminently then deal with it now
    if (imminent_maxlevel > mMaxLevel)
      mMaxLevel = imminent_maxlevel;
    // clip but not imminently can ramp up more slowly
    else if (maxlevel > mMaxLevel)
      mMaxLevel = attack * mMaxLevel + (1.f - attack) * maxlevel;
    // not clipping, decay more slowly
    else
      mMaxLevel = decay  * mMaxLevel + (1.f - decay ) * maxlevel;

    // want mMaxLevel * drc -> 1.0
    float amp = mDrc * mAttenuation;

    // We fade in the attenuation over first couple of seconds
    float start = min (max ((mSubmitted-1.f), 0.f), 1.f);
    float attenuation = min( 1.f, max(mAttenuation / (amp * mMaxLevel), 1.f / mDrc));
    mAttenuation = (1.f - start) * 1.f / mDrc + start * attenuation;
    }
  else
    mAttenuation = 1.f / mDrc;

  applyVolume();
  }
//}}}

//{{{
void cOmxAudio::printChannels (OMX_AUDIO_CHANNELTYPE eChannelMapping[]) {

  for (int i = 0; i < OMX_AUDIO_MAXCHANNELS; i++) {
    switch (eChannelMapping[i]) {
      case OMX_AUDIO_ChannelLF:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLF");
        break;

      case OMX_AUDIO_ChannelRF:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRF");
        break;

      case OMX_AUDIO_ChannelCF:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelCF");
        break;

      case OMX_AUDIO_ChannelLS:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLS");
        break;

      case OMX_AUDIO_ChannelRS:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRS");
        break;

      case OMX_AUDIO_ChannelLFE:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLFE");
        break;

      case OMX_AUDIO_ChannelCS:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelCS");
        break;

      case OMX_AUDIO_ChannelLR:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLR");
        break;

      case OMX_AUDIO_ChannelRR:
        cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRR");
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
void cOmxAudio::printPCM (OMX_AUDIO_PARAM_PCMMODETYPE* pcm, const string& direction) {

  cLog::log (LOGINFO1, "pcm->direction    : %s", direction.c_str());
  cLog::log (LOGINFO1, "pcm->nPortIndex   : %d", (int)pcm->nPortIndex);
  cLog::log (LOGINFO1, "pcm->eNumData     : %d", pcm->eNumData);
  cLog::log (LOGINFO1, "pcm->eEndian      : %d", pcm->eEndian);
  cLog::log (LOGINFO1, "pcm->bInterleaved : %d", (int)pcm->bInterleaved);
  cLog::log (LOGINFO1, "pcm->nBitPerSample: %d", (int)pcm->nBitPerSample);
  cLog::log (LOGINFO1, "pcm->ePCMMode     : %d", pcm->ePCMMode);
  cLog::log (LOGINFO1, "pcm->nChannels    : %d", (int)pcm->nChannels);
  cLog::log (LOGINFO1, "pcm->nSamplingRate: %d", (int)pcm->nSamplingRate);

  printChannels (pcm->eChannelMapping);
  }
//}}}
