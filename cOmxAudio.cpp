// cOmxAudio.cpp
//{{{  includes
#include <algorithm>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}

#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};
//{{{
unsigned countBits (uint64_t value) {

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
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode - AV_CODEC_ID_VORBIS");
      return true;

    case AV_CODEC_ID_AAC:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode - AV_CODEC_ID_AAC");
      return true;

    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode - AV_CODEC_ID_MP2 / AV_CODEC_ID_MP3");
      return true;

    case AV_CODEC_ID_DTS:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode - AV_CODEC_ID_DTS");
      return true;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::hwDecode - AV_CODEC_ID_AC3 / AV_CODEC_ID_EAC3");
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
    unsigned int used = mDecoder.getInputBufferSize() - mDecoder.getInputBufferSpace();
    return mInputBytesPerSec ? (float)used / (float)mInputBytesPerSec : 0.f;
    }
  }
//}}}
//{{{
float cOmxAudio::getCacheTotal() {

  float audioPlusBuffer = mConfig.mHints.samplerate ? (32.f * 512.f / mConfig.mHints.samplerate) : 0.f;
  float inputBuffer = mInputBytesPerSec ?
    (float)mDecoder.getInputBufferSize() / (float)mInputBytesPerSec : 0;

  return AUDIO_BUFFER_SECONDS + inputBuffer + audioPlusBuffer;
  }
//}}}
//{{{
unsigned int cOmxAudio::getAudioRenderingLatency() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);
  if (mRenderAnalog.isInit()) {
    param.nPortIndex = mRenderAnalog.getInputPort();
    if (mRenderAnalog.getConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " get latency");
      return 0;
      }
    }

  else if (mRenderHdmi.isInit()) {
    param.nPortIndex = mRenderHdmi.getInputPort();
    if (mRenderHdmi.getConfig (OMX_IndexConfigAudioRenderingLatency, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " get hdmi latency");
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
  if (mDecoder.isInit()) {
    param.nPortIndex = mDecoder.getInputPort();
    if (mDecoder.getConfig(OMX_IndexConfigBrcmAudioMaxSample, &param) != OMX_ErrorNone) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " getMaxLevel");
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

  if (!mFailedEos && !(mDecoder.isEOS() && latency == 0))
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
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType - OMX_AUDIO_CodingDTS");
      mEncoding = OMX_AUDIO_CodingDTS;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType - OMX_AUDIO_CodingDDP");
      mEncoding = OMX_AUDIO_CodingDDP;
      break;

    default:
      cLog::log (LOGINFO1, "cOmxAudio::SetCodingType - OMX_AUDIO_CodingPCM");
      mEncoding = OMX_AUDIO_CodingPCM;
      break;
    }
  }
//}}}

// actions
//{{{
bool cOmxAudio::init (cOmxClock* clock, const cOmxAudioConfig& config,
                      uint64_t channelMap, unsigned int uiBitsPerSample) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  deInit();

  mAvClock = clock;
  mClock = mAvClock->getOmxClock();
  mDrc = 0;

  mConfig = config;
  if (mConfig.mPassthrough) // passthrough overwrites hw decode
    mConfig.mHwDecode = false;
  else if (mConfig.mHwDecode) // check again if we are capable to hw decode the format
    mConfig.mHwDecode = canHwDecode (mConfig.mHints.codec);
  if (mConfig.mPassthrough || mConfig.mHwDecode)
    setCodingType (mConfig.mHints.codec);
  else
    setCodingType (AV_CODEC_ID_PCM_S16LE);

  mNumInputChannels = countBits (channelMap);
  memset (mInputChannels, 0, sizeof(mInputChannels));
  memset (mOutputChannels, 0, sizeof(mOutputChannels));

  memset (&mWaveHeader, 0, sizeof(mWaveHeader));
  mWaveHeader.Format.nChannels = 2;
  mWaveHeader.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

  if (!mConfig.mPassthrough && channelMap) {
    //{{{  set input format, get channelLayout
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];

    // force out layout stereo if input not multichannel, gives the receiver chance to upmix
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

  if (!mDecoder.init ("OMX.broadcom.audio_decode", OMX_IndexParamAudioInit))
    return false;

  //{{{  set passthru
  OMX_CONFIG_BOOLEANTYPE boolType;
  OMX_INIT_STRUCTURE(boolType);

  // set passthru
  boolType.bEnabled = mConfig.mPassthrough ? OMX_TRUE : OMX_FALSE;
  if (mDecoder.setParameter (OMX_IndexParamBrcmDecoderPassThrough, &boolType) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set PassThru");
    return false;
    }
  //}}}
  //{{{  set number/size of buffers for decoder input
  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so 48K input buffer will fit the output buffer
  mChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (mNumInputChannels * mBitsPerSample)
                 >> (rounded_up_channels_shift[mNumInputChannels] + 4);

  OMX_PARAM_PORTDEFINITIONTYPE port;
  OMX_INIT_STRUCTURE(port);

  // get port param
  port.nPortIndex = mDecoder.getInputPort();
  if (mDecoder.getParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " get port");
    return false;
    }

  // set port param
  port.format.audio.eEncoding = mEncoding;
  port.nBufferSize = mChunkLen;
  port.nBufferCountActual = max (port.nBufferCountMin, 16U);
  if (mDecoder.setParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set port");
    return false;
    }
  //}}}
  //{{{  set number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port);

  // get port param
  port.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " get port param");
    return false;
    }

  // set port param
  port.nBufferCountActual = max ((unsigned int)port.nBufferCountMin, mBufferLen / port.nBufferSize);
  if (mDecoder.setParameter (OMX_IndexParamPortDefinition, &port) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set port param");
    return false;
    }
  //}}}
  //{{{  set input port format
  OMX_AUDIO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);

  format.nPortIndex = mDecoder.getInputPort();
  format.eEncoding = mEncoding;
  if (mDecoder.setParameter (OMX_IndexParamAudioPortFormat, &format) != OMX_ErrorNone) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set format");
    return false;
    }
  //}}}
  if (mDecoder.allocInputBuffers() != OMX_ErrorNone) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " allocInputBuffers");
    return false;
    }
    //}}}
  if (mDecoder.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
    //{{{  return error
    cLog::log (LOGERROR, string(__func__) + " set state");
    return false;
    }
    //}}}

  if (mEncoding == OMX_AUDIO_CodingPCM) {
    //{{{  declare buffer
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

    auto buffer = mDecoder.getInputBuffer();
    if (buffer == NULL) {
       //  error, return
      cLog::log (LOGERROR, string(__func__) + " getInputBuffer");
      return false;
      }

    buffer->nOffset = 0;
    buffer->nFilledLen = min (sizeof(mWaveHeader), buffer->nAllocLen);
    memset (buffer->pBuffer, 0, buffer->nAllocLen);
    memcpy (buffer->pBuffer, &mWaveHeader, buffer->nFilledLen);
    buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    if (mDecoder.emptyThisBuffer (buffer) != OMX_ErrorNone) {
      //  error, return
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), buffer);
      return false;
      }
    }
    //}}}
  else if (mConfig.mHwDecode) {
    //{{{  send decoder config
    if (mConfig.mHints.extrasize > 0 && mConfig.mHints.extradata != NULL) {
      auto buffer = mDecoder.getInputBuffer();
      if (buffer == NULL) {
        // error, return
        cLog::log (LOGERROR, string(__func__) + " extra getInputBuffer");
        return false;
        }

      buffer->nOffset = 0;
      buffer->nFilledLen = min((OMX_U32)mConfig.mHints.extrasize, buffer->nAllocLen);
      memset (buffer->pBuffer, 0, buffer->nAllocLen);
      memcpy (buffer->pBuffer, mConfig.mHints.extradata, buffer->nFilledLen);
      buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
      if (mDecoder.emptyThisBuffer (buffer) != OMX_ErrorNone) {
        // error, return
        cLog::log (LOGERROR, string(__func__) + " extra emptyThisBuffer");
        mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), buffer);
        return false;
        }
      }
    }
    //}}}
  if (mDecoder.badState())
    return false;

  //{{{  set mPcmInput
  OMX_INIT_STRUCTURE(mPcmInput);
  mPcmInput.nPortIndex = mDecoder.getInputPort();
  memcpy (mPcmInput.eChannelMapping, mInputChannels, sizeof(mInputChannels));
  mPcmInput.eNumData = OMX_NumericalDataSigned;
  mPcmInput.eEndian = OMX_EndianLittle;
  mPcmInput.bInterleaved = OMX_TRUE;
  mPcmInput.nBitPerSample = mBitsPerSample;
  mPcmInput.ePCMMode = OMX_AUDIO_PCMModeLinear;
  mPcmInput.nChannels = mNumInputChannels;
  mPcmInput.nSamplingRate = mConfig.mHints.samplerate;

  cLog::log (LOGINFO1, "cOmxAudio::init - bitsPer:%d rate:%d ch:%d buffer size:%d bytesPer:%d",
                       (int)mPcmInput.nBitPerSample, (int)mPcmInput.nSamplingRate,
                       (int)mPcmInput.nChannels, mBufferLen, mInputBytesPerSec);

  printPCM (&mPcmInput, "input");
  //}}}
  cLog::log (LOGINFO1, "cOmxAudio::init - dev:%s pass:%d hw:%d",
                       mConfig.mDevice.c_str(), mConfig.mPassthrough, mConfig.mHwDecode);

  mSettingsChanged = false;
  mSetStartTime  = true;
  mSubmittedEos = false;
  mFailedEos = false;
  mLastPts = DVD_NOPTS_VALUE;
  mSubmitted = 0.f;
  mMaxLevel = 0.f;

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
bool cOmxAudio::portChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mSettingsChanged) {
    mDecoder.disablePort (mDecoder.getOutputPort(), true);
    mDecoder.enablePort (mDecoder.getOutputPort(), true);
    return true;
    }

  if (!mConfig.mPassthrough)
    if (!mMixer.init ("OMX.broadcom.audio_mixer", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both")
    if (!mSplitter.init ("OMX.broadcom.audio_splitter", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:local")
    if (!mRenderAnalog.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:hdmi")
    if (!mRenderHdmi.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;

  updateAttenuation();

  if (mMixer.isInit()) {
    // setup mixer output
    OMX_INIT_STRUCTURE(mPcmOutput);
    mPcmOutput.nPortIndex = mDecoder.getOutputPort();
    if (mDecoder.getParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + "gGetParameter");
      return false;
      }
      //}}}

    memcpy (mPcmOutput.eChannelMapping, mOutputChannels, sizeof(mOutputChannels));

    // round up to power of 2
    mPcmOutput.nChannels = mNumOutputChannels > 4 ? 8 : mNumOutputChannels > 2 ? 4 : mNumOutputChannels;

    // limit samplerate (through resampling) if requested
    mPcmOutput.nSamplingRate = min (max ((int)mPcmOutput.nSamplingRate, 8000), 192000);

    mPcmOutput.nPortIndex = mMixer.getOutputPort();
    if (mMixer.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR,  string(__func__) + " setParameter");
      return false;
      }
      //}}}

    cLog::log (LOGINFO1, "cOmxAudio::portChanged Output bps:%d rate:%d ch:%d buffer size:%d bps:%d",
                         (int)mPcmOutput.nBitPerSample, (int)mPcmOutput.nSamplingRate,
                         (int)mPcmOutput.nChannels, mBufferLen, mBytesPerSec);
    printPCM (&mPcmOutput, "output");

    if (mSplitter.isInit() ) {
      mPcmOutput.nPortIndex = mSplitter.getInputPort();
      if (mSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " mSplitter setParameter");
        return false;
        }
        //}}}
      mPcmOutput.nPortIndex = mSplitter.getOutputPort();
      if (mSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " mSplitter SetParameter");
        return false;
        }
        //}}}
      mPcmOutput.nPortIndex = mSplitter.getOutputPort() + 1;
      if (mSplitter.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " SetParameter");
        return false;
        }
        //}}}
      }

    if (mRenderAnalog.isInit() ) {
      mPcmOutput.nPortIndex = mRenderAnalog.getInputPort();
      if (mRenderAnalog.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " mRenderAnalog SetParameter");
        return false;
        }
        //}}}
      }
    if (mRenderHdmi.isInit() ) {
      mPcmOutput.nPortIndex = mRenderHdmi.getInputPort();
      if (mRenderHdmi.setParameter (OMX_IndexParamAudioPcm, &mPcmOutput) != OMX_ErrorNone) {
        //{{{  error return
        cLog::log (LOGERROR, string(__func__) + " mRenderhdmi SetParameter");
        return false;
        }
        //}}}
      }
    }

  if (mRenderAnalog.isInit()) {
    mTunnelClockAnalog.init (mClock, mClock->getInputPort(),
                                &mRenderAnalog, mRenderAnalog.getInputPort()+1);

    if (mTunnelClockAnalog.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mTunnel_clock_Analog.Establish");
      return false;
      }
      //}}}
    mRenderAnalog.resetEos();
    }

  if (mRenderHdmi.isInit() ) {
    mTunnelClockHdmi.init (
      mClock, mClock->getInputPort() + (mRenderAnalog.isInit() ? 2 : 0),
      &mRenderHdmi, mRenderHdmi.getInputPort()+1);

    if (mTunnelClockHdmi.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log(LOGERROR, string(__func__) + " mTunnel_clock_hdmi.establish");
      return false;
      }
      //}}}
    mRenderHdmi.resetEos();
    }

  if (mRenderAnalog.isInit() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps,
    // it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches,
    // but can affect video/display sync when in dual audio mode, make Analogue the slave
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = mConfig.mIsLive || mConfig.mDevice == "omx:both" ? OMX_FALSE : OMX_TRUE;
    if (mRenderAnalog.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char*)audioDest.sName, "local", sizeof(audioDest.sName));
    if (mRenderAnalog.setConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderAnalog.setConfig");
      return false;
      }
      //}}}
    }

  if (mRenderHdmi.isInit() ) {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps,
    // it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches,
    // but can affect video/display sync
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = mConfig.mIsLive ? OMX_FALSE:OMX_TRUE;
    if (mRenderHdmi.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool) != OMX_ErrorNone)
      return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char *)audioDest.sName, "hdmi", strlen("hdmi"));
    if (mRenderHdmi.setConfig (OMX_IndexConfigBrcmAudioDestination, &audioDest) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderhdmi.setConfig");
      return false;
      }
      //}}}
    }

  if (mSplitter.isInit() ) {
    mTunnelSplitterAnalog.init (&mSplitter, mSplitter.getOutputPort(),
                                   &mRenderAnalog, mRenderAnalog.getInputPort());
    if (mTunnelSplitterAnalog.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mTunnelSplitterAnalog.establish");
      return false;
      }
      //}}}
    mTunnelSplitterHdmi.init (&mSplitter, mSplitter.getOutputPort() + 1,
                                 &mRenderHdmi, mRenderHdmi.getInputPort());
    if (mTunnelSplitterHdmi.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mTunnelSplitterhdmi.establish");
      return false;
      }
      //}}}
    }

  if (mMixer.isInit()) {
    mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(),
                            &mMixer, mMixer.getInputPort());
    if (mSplitter.isInit())
      mTunnelMixer.init (&mMixer, mMixer.getOutputPort(),
                            &mSplitter, mSplitter.getInputPort());
    else {
      if (mRenderAnalog.isInit())
        mTunnelMixer.init (&mMixer, mMixer.getOutputPort(),
                              &mRenderAnalog, mRenderAnalog.getInputPort());
      if (mRenderHdmi.isInit())
        mTunnelMixer.init (&mMixer, mMixer.getOutputPort(),
                              &mRenderHdmi, mRenderHdmi.getInputPort());
      }
    cLog::log (LOGINFO1, "cOmxAudio::portChanged bits:%d mode:%d ch:%d srate:%d nopassthrough",
               (int)mPcmInput.nBitPerSample, mPcmInput.ePCMMode,
               (int)mPcmInput.nChannels, (int)mPcmInput.nSamplingRate);
    }
  else {
    if (mRenderAnalog.isInit())
      mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(),
                              &mRenderAnalog, mRenderAnalog.getInputPort());
    else if (mRenderHdmi.isInit())
      mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(),
                              &mRenderHdmi, mRenderHdmi.getInputPort());
     cLog::log (LOGINFO1, "cOmxAudio::portChanged bits:%d mode:%d ch:%d srate:%d passthrough", 0, 0, 0, 0);
     }

  if (mTunnelDecoder.establish() != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mTunnel_decoder.establish");
    return false;
    }
    //}}}
  if (mMixer.isInit()) {
    if (mMixer.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mMixer OMX_StateExecuting");
      return false;
      }
      //}}}
    }
  if (mMixer.isInit())
    if (mTunnelMixer.establish() != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mTunnel_decoder.establish");
      return false;
      }
      //}}}
  if (mSplitter.isInit())
    if (mSplitter.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mSplitter OMX_StateExecuting");
      return false;
      }
      //}}}
  if (mRenderAnalog.isInit())
    if (mRenderAnalog.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderAnalog OMX_StateExecuting");
      return false;
      }
      //}}}
  if (mRenderHdmi.isInit())
    if (mRenderHdmi.setStateForComponent (OMX_StateExecuting) != OMX_ErrorNone) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderhdmi OMX_StateExecuting");
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
    (mConfig.mPassthrough || mConfig.mHwDecode) ? 1 : (mBitsPerSample >> 3) * mNumInputChannels;
  unsigned int demuxSamples = len / pitch;
  unsigned int demuxSamples_sent = 0;
  auto demuxer_content = (uint8_t *)data;

  OMX_BUFFERHEADERTYPE* buffer = NULL;
  while (demuxSamples_sent < demuxSamples) {
    buffer = mDecoder.getInputBuffer (200); // 200ms timeout
    if (!buffer) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " timeout");
      return len;
      }
      //}}}

    buffer->nOffset = 0;
    buffer->nFlags  = 0;

    // we want audio_decode output buffer size to be no more than AUDIO_DECODE_OUTPUT_BUFFER.
    // it will be 16-bit and rounded up to next power of 2 in channels
    unsigned int max_buffer = AUDIO_DECODE_OUTPUT_BUFFER *
      (mNumInputChannels * mBitsPerSample) >> (rounded_up_channels_shift[mNumInputChannels] + 4);
    unsigned int remaining = demuxSamples-demuxSamples_sent;
    unsigned int samples_space = min(max_buffer, buffer->nAllocLen)/pitch;
    unsigned int samples = min(remaining, samples_space);

    buffer->nFilledLen = samples * pitch;

    unsigned int frames = frame_size ? len/frame_size:0;
    if ((samples < demuxSamples || frames > 1) &&
        (mBitsPerSample == 32) &&
        !(mConfig.mPassthrough || mConfig.mHwDecode)) {
      const unsigned int sample_pitch = mBitsPerSample >> 3;
      const unsigned int frame_samples = frame_size / pitch;
      const unsigned int plane_size = frame_samples * sample_pitch;
      const unsigned int out_plane_size = samples * sample_pitch;
      for (unsigned int sample = 0; sample < samples;) {
        unsigned int frame = (demuxSamples_sent + sample) / frame_samples;
        unsigned int sample_in_frame = (demuxSamples_sent + sample) - frame * frame_samples;
        int out_remaining = min(min(frame_samples - sample_in_frame, samples), samples-sample);
        auto src = demuxer_content + frame*frame_size + sample_in_frame * sample_pitch;
        auto dst = (uint8_t*)buffer->pBuffer + sample * sample_pitch;
        for (unsigned int channel = 0; channel < mNumInputChannels; channel++) {
          memcpy (dst, src, out_remaining * sample_pitch);
          src += plane_size;
          dst += out_plane_size;
          }
        sample += out_remaining;
        }
      }
    else
      memcpy (buffer->pBuffer, demuxer_content + demuxSamples_sent * pitch, buffer->nFilledLen);

    auto val = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;
    if (mSetStartTime) {
      buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
      mLastPts = pts;
      cLog::log (LOGINFO1, "cOmxAudio::addPackets - setStartTime:%f", (float)val / DVD_TIME_BASE);
      mSetStartTime = false;
      }
    else {
      if (pts == DVD_NOPTS_VALUE) {
        buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        mLastPts = pts;
        }
      else if (mLastPts != pts) {
        if (pts > mLastPts)
          mLastPts = pts;
        else
          buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        }
      else if (mLastPts == pts)
        buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }

    buffer->nTimeStamp = toOmxTime (val);
    demuxSamples_sent += samples;
    if (demuxSamples_sent == demuxSamples)
      buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    if (mDecoder.emptyThisBuffer (buffer) != OMX_ErrorNone) {
      cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
      mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), buffer);
      return 0;
      }

    if (mDecoder.waitForEvent (OMX_EventPortSettingsChanged, 0) == OMX_ErrorNone)
      if (!portChanged())
        cLog::log (LOGERROR, string(__func__) + "  portChanged");
    }

  mSubmitted += (float)demuxSamples / mConfig.mHints.samplerate;
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

  auto* omx_buffer = mDecoder.getInputBuffer(1000);
  if (omx_buffer == NULL) {
    cLog::log (LOGERROR, string(__func__) + " buffer");
    mFailedEos = true;
    return;
    }
  omx_buffer->nOffset = 0;
  omx_buffer->nFilledLen = 0;
  omx_buffer->nTimeStamp = toOmxTime (0LL);
  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  if (mDecoder.emptyThisBuffer (omx_buffer) != OMX_ErrorNone) {
    cLog::log (LOGERROR, string(__func__) + " OMX_EmptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getComponent(), omx_buffer);
    return;
    }
  }
//}}}
//{{{
void cOmxAudio::flush() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mDecoder.flushAll();
  if (mMixer.isInit() )
    mMixer.flushAll();
  if (mSplitter.isInit() )
    mSplitter.flushAll();

  if (mRenderAnalog.isInit() )
    mRenderAnalog.flushAll();
  if (mRenderHdmi.isInit() )
    mRenderHdmi.flushAll();

  while (!mAmpQueue.empty())
    mAmpQueue.pop_front();

  if (mRenderAnalog.isInit() )
    mRenderAnalog.resetEos();
  if (mRenderHdmi.isInit() )
    mRenderHdmi.resetEos();

  mLastPts = DVD_NOPTS_VALUE;
  mSubmitted = 0.f;
  mMaxLevel = 0.f;
  mSetStartTime  = true;
  }
//}}}
//{{{
bool cOmxAudio::deInit() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mTunnelClockAnalog.isInit() )
    mTunnelClockAnalog.deEstablish();
  if (mTunnelClockHdmi.isInit() )
    mTunnelClockHdmi.deEstablish();

  // ignore expected errors on teardown
  if (mMixer.isInit() )
    mMixer.ignoreNextError (OMX_ErrorPortUnpopulated);
  else {
    if (mRenderHdmi.isInit() )
      mRenderHdmi.ignoreNextError (OMX_ErrorPortUnpopulated);
    if (mRenderAnalog.isInit() )
      mRenderAnalog.ignoreNextError (OMX_ErrorPortUnpopulated);
    }

  mTunnelDecoder.deEstablish();
  if (mTunnelMixer.isInit() )
    mTunnelMixer.deEstablish();
  if (mTunnelSplitterHdmi.isInit() )
    mTunnelSplitterHdmi.deEstablish();
  if (mTunnelSplitterAnalog.isInit() )
    mTunnelSplitterAnalog.deEstablish();

  mDecoder.flushInput();

  mDecoder.deInit();
  if (mMixer.isInit())
    mMixer.deInit();
  if (mSplitter.isInit())
    mSplitter.deInit();
  if (mRenderHdmi.isInit())
    mRenderHdmi.deInit();
  if (mRenderAnalog.isInit())
    mRenderAnalog.deInit();

  mBytesPerSec = 0;
  mBufferLen = 0;

  mClock = NULL;
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
      cLog::log (LOGINFO1, "cOmxAudio::canHWDecode - OMX_AUDIO_CodingMP3");
      mEncoding = OMX_AUDIO_CodingMP3;
      mConfig.mHwDecode = true;
      break;

    case AV_CODEC_ID_DTS:
      cLog::log (LOGINFO1, "cOmxAudio::canHWDecode - OMX_AUDIO_CodingDTS");
      mEncoding = OMX_AUDIO_CodingDTS;
      mConfig.mHwDecode = true;
      break;

    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      cLog::log (LOGINFO1, "cOmxAudio::canHWDecode - OMX_AUDIO_CodingDDP");
      mEncoding = OMX_AUDIO_CodingDDP;
      mConfig.mHwDecode = true;
      break;

    default:
      cLog::log (LOGINFO1, "cOmxAudio::canHWDecode - OMX_AUDIO_CodingPCM");
      mEncoding = OMX_AUDIO_CodingPCM;
      mConfig.mHwDecode = false;
      break;
    }

  return mConfig.mHwDecode;
  }
//}}}

//{{{
bool cOmxAudio::applyVolume() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mConfig.mPassthrough)
    return false;

  // the Analogue volume is too quiet for some. Allow use of an advancedsetting to boost this (at risk of distortion) (deprecated)
  float volume = mMute ? 0.f : mCurrentVolume;
  float ac3Gain = 12.f;
  double gain = pow(10, (ac3Gain - 12.f) / 20.0);
  const float* coeff = mDownmixMatrix;

  OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
  OMX_INIT_STRUCTURE(mix);
  assert (sizeof(mix.coeff) / sizeof(mix.coeff[0]) == 64);
  if (mDrc != 1.0) {
    // reduce scaling so overflow can be seen
    for (size_t i = 0; i < 8*8; ++i)
      mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * 0.01f));
    mix.nPortIndex = mDecoder.getInputPort();
    if (mDecoder.setConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
      cLog::log (LOGERROR, string(__func__) + " OMX_IndexConfigBrcmAudioDownmixCoefficients");
      return false;
      }
    }

  for (size_t i = 0; i < 8*8; ++i)
    mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * volume * mDrc * mAttenuation));

  mix.nPortIndex = mMixer.getInputPort();
  if (mMixer.setConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix) != OMX_ErrorNone) {
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
      case OMX_AUDIO_ChannelLF: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLF"); break;
      case OMX_AUDIO_ChannelRF: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRF"); break;
      case OMX_AUDIO_ChannelCF: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelCF"); break;
      case OMX_AUDIO_ChannelLS: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLS"); break;
      case OMX_AUDIO_ChannelRS: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRS"); break;
      case OMX_AUDIO_ChannelLFE: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLFE"); break;
      case OMX_AUDIO_ChannelCS: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelCS"); break;
      case OMX_AUDIO_ChannelLR: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelLR"); break;
      case OMX_AUDIO_ChannelRR: cLog::log(LOGINFO1, "OMX_AUDIO_ChannelRR"); break;

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
