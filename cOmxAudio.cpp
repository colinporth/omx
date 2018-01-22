// cOmxAudio.cpp
//{{{  includes
#include <algorithm>

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "cOmxAv.h"

using namespace std;
//}}}
#define AUDIO_BUFFER_SECONDS 3
const char kRoundedUpChansShift[] = {0,0,1,2,2,3,3,3,3};
//{{{
int getCountBits (uint64_t value) {

  int bits = 0;
  for (; value; ++bits)
    value &= value - 1;
  return bits;
  }
//}}}

//{{{
cOmxAudio::~cOmxAudio() {

  // deallocate OMX wiring
  if (mTunnelClockAnalog.isInit() )
    mTunnelClockAnalog.deEstablish();
  if (mTunnelClockHdmi.isInit() )
    mTunnelClockHdmi.deEstablish();

  // ignore expected errors on teardown
  if (mMixer.isInit())
    mMixer.ignoreNextError (OMX_ErrorPortUnpopulated);
  else {
    if (mRenderHdmi.isInit() )
      mRenderHdmi.ignoreNextError (OMX_ErrorPortUnpopulated);
    if (mRenderAnal.isInit() )
      mRenderAnal.ignoreNextError (OMX_ErrorPortUnpopulated);
    }

  mTunnelDecoder.deEstablish();
  if (mTunnelMixer.isInit() )
    mTunnelMixer.deEstablish();
  if (mTunnelSplitterHdmi.isInit() )
    mTunnelSplitterHdmi.deEstablish();
  if (mTunnelSplitterAnalog.isInit() )
    mTunnelSplitterAnalog.deEstablish();

  // deallocate OMX resources
  mDecoder.flushInput();
  mDecoder.deInit();
  if (mMixer.isInit())
    mMixer.deInit();
  if (mSplitter.isInit())
    mSplitter.deInit();
  if (mRenderHdmi.isInit())
    mRenderHdmi.deInit();
  if (mRenderAnal.isInit())
    mRenderAnal.deInit();

  // deallocate ffmpeg resources
  mAvUtil.av_free (mOutput);
  if (mFrame)
    mAvUtil.av_free (mFrame);
  if (mConvert)
    mSwResample.swr_free (&mConvert);
  if (mCodecContext) {
    if (mCodecContext->extradata)
      mAvUtil.av_free (mCodecContext->extradata);
    mCodecContext->extradata = NULL;
    mAvCodec.avcodec_close (mCodecContext);
    mAvUtil.av_free (mCodecContext);
    }
  }
//}}}

// gets
//{{{
bool cOmxAudio::isEOS() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (!mFailedEos &&
      !(mDecoder.isEOS() && (getAudioRenderingLatency() == 0)))
    return false;

  if (mSubmittedEos) {
    mSubmittedEos = false;
    cLog::log (LOGINFO, __func__);
    }

  return true;
  }
//}}}
//{{{
double cOmxAudio::getDelay() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  double stamp = kNoPts;
  if ((mLastPts != kNoPts) && mClock)
    stamp = mClock->getMediaTime();

  // if possible the delay is current media time - time of last submitted packet
  if (stamp != kNoPts)
    return (mLastPts - stamp) / kPtsScale;
  else { // just measure the input fifo
    unsigned int used = mDecoder.getInputBufferSize() - mDecoder.getInputBufferSpace();
    return mInputBytesPerSec ? (float)used / mInputBytesPerSec : 0.f;
    }
  }
//}}}
//{{{
float cOmxAudio::getCacheTotal() {

  float inputBuffer = mInputBytesPerSec ? (float)mDecoder.getInputBufferSize() / mInputBytesPerSec : 0.f;
  float moreBuffer = mConfig.mHints.samplerate ? (32.f * 512.f / mConfig.mHints.samplerate) : 0.f;
  return AUDIO_BUFFER_SECONDS + inputBuffer + moreBuffer;
  }
//}}}
//{{{
int cOmxAudio::getChunkLen (int chans) {
// we want audio_decode output buffer size to be no more than AUDIO_DECODE_OUTPUT_BUFFER.
// it will be 16-bit and rounded up to next power of 2 chans

  return 32*1024 * (chans * mBitsPerSample) >> (kRoundedUpChansShift[chans] + 4);
  }
//}}}
//{{{
unsigned int cOmxAudio::getAudioRenderingLatency() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if (mRenderAnal.isInit()) {
    param.nPortIndex = mRenderAnal.getInputPort();
    if (mRenderAnal.getConfig (OMX_IndexConfigAudioRenderingLatency, &param)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " getAnalLatency");
      return 0;
      }
    }
  else if (mRenderHdmi.isInit()) {
    param.nPortIndex = mRenderHdmi.getInputPort();
    if (mRenderHdmi.getConfig (OMX_IndexConfigAudioRenderingLatency, &param)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " getHdmiLatency");
      return 0;
      }
    }

  return param.nU32;
  }
//}}}
//{{{
uint64_t cOmxAudio::getChanLayout (enum PCMLayout layout) {

  uint64_t layouts[] = {
    /* 2.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT,
    /* 2.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 3.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER,
    /* 3.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_LOW_FREQUENCY,
    /* 4.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT    | 1<<PCM_BACK_RIGHT,
    /* 4.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT    | 1<<PCM_BACK_RIGHT |
              1<<PCM_LOW_FREQUENCY,
    /* 5.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT  |
              1<<PCM_BACK_RIGHT,
    /* 5.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT  |
              1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 7.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT  |
              1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT   | 1<<PCM_BACK_RIGHT,
    /* 7.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT  |
              1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT   | 1<<PCM_BACK_RIGHT   | 1<<PCM_LOW_FREQUENCY
    };

  return (int)layout < 10 ? layouts[(int)layout] : 0;
  }
//}}}

// sets
//{{{
void cOmxAudio::setMute (bool mute) {

  mMute = mute;
  applyVolume();
  }
//}}}
//{{{
void cOmxAudio::setVolume (float volume) {

  mCurVolume = volume;
  applyVolume();
  }
//}}}

// actions
//{{{
bool cOmxAudio::open (cOmxClock* clock, const cOmxAudioConfig& config) {

  cLog::log (LOGINFO, "cOmxAudio::open " + mConfig.mDevice);

  mClock = clock;
  mConfig = config;
  mChans = 0;
  mGotFirstFrame = false;
  mAvCodec.avcodec_register_all();

  auto codec = mAvCodec.avcodec_find_decoder (config.mHints.codec);
  if (!codec) {
    //{{{  error return
    cLog::log (LOGINFO1, string(__func__) + " no codec");
    return false;
    }
    //}}}
  //{{{  codecContext
  mCodecContext = mAvCodec.avcodec_alloc_context3 (codec);
  mCodecContext->debug_mv = 0;
  mCodecContext->debug = 0;
  mCodecContext->workaround_bugs = 1;

  if (codec->capabilities & CODEC_CAP_TRUNCATED)
    mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  mCodecContext->channels = config.mHints.channels;
  mCodecContext->sample_rate = config.mHints.samplerate;
  mCodecContext->block_align = config.mHints.blockalign;
  mCodecContext->bit_rate = config.mHints.bitrate;
  mCodecContext->bits_per_coded_sample = config.mHints.bitspersample;

  if (mCodecContext->request_channel_layout)
    cLog::log (LOGINFO, "cOmxAudio::open - channelLayout %x",
                        (unsigned)mCodecContext->request_channel_layout);

  if (mCodecContext->bits_per_coded_sample == 0)
    mCodecContext->bits_per_coded_sample = 16;

  if (mConfig.mHints.extradata && mConfig.mHints.extrasize > 0 ) {
    // extradata
    mCodecContext->extradata_size = mConfig.mHints.extrasize;
    mCodecContext->extradata = (uint8_t*)mAvUtil.av_mallocz (mConfig.mHints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy (mCodecContext->extradata, mConfig.mHints.extradata, mConfig.mHints.extrasize);
    }

  if (mAvCodec.avcodec_open2 (mCodecContext, codec, NULL) < 0) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " cannot open codec");
    return false;
    }
  //}}}
  mFrame = mAvCodec.av_frame_alloc();

  mSampleFormat = AV_SAMPLE_FMT_NONE;
  mOutFormat = (mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16) ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;

  uint64_t chanMap = getChanMap();
  mNumInputChans = getCountBits (chanMap);
  memset (mInputChans, 0, sizeof(mInputChans));
  memset (mOutputChans, 0, sizeof(mOutputChans));
  if (chanMap) {
    //{{{  set input format, get channelLayout
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];

    // force out layout stereo if input not multichannel, gives the receiver chance to upmix
    if ((chanMap == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT)) || (chanMap == AV_CH_FRONT_CENTER))
      mConfig.mLayout = PCM_LAYOUT_2_0;
    buildChanMap (inLayout, chanMap);
    mNumOutputChans = buildChanMapCEA (outLayout, getChanLayout(mConfig.mLayout));

    cPcmMap pcmMap;
    pcmMap.reset();
    pcmMap.setInputFormat (mNumInputChans, inLayout, getBitsPerSample() / 8,
                           mConfig.mHints.samplerate, mConfig.mLayout, mConfig.mBoostOnDownmix);
    pcmMap.setOutputFormat (mNumOutputChans, outLayout, false);
    pcmMap.getDownmixMatrix (mDownmixMatrix);

    buildChanMapOMX (mInputChans, chanMap);
    buildChanMapOMX (mOutputChans, getChanLayout (mConfig.mLayout));
    }
    //}}}

  mBitsPerSample = getBitsPerSample();
  mBytesPerSec = mConfig.mHints.samplerate * (2 << kRoundedUpChansShift[mNumInputChans]);
  mBufferLen = AUDIO_BUFFER_SECONDS * mBytesPerSec;
  mInputBytesPerSec = mConfig.mHints.samplerate * mBitsPerSample * mNumInputChans >> 3;

  if (!mDecoder.init ("OMX.broadcom.audio_decode", OMX_IndexParamAudioInit))
    return false;
  //{{{  set number/size of buffers for decoder input
  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so 48K input buffer will fit the output buffer
  OMX_PARAM_PORTDEFINITIONTYPE port;
  OMX_INIT_STRUCTURE(port);

  // get port param
  port.nPortIndex = mDecoder.getInputPort();
  if (mDecoder.getParam (OMX_IndexParamPortDefinition, &port)) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " get port");
    return false;
    }

  // set port param
  port.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
  port.nBufferSize = getChunkLen (mNumInputChans);
  port.nBufferCountActual = max (port.nBufferCountMin, 16U);
  if (mDecoder.setParam (OMX_IndexParamPortDefinition, &port)) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set port");
    return false;
    }
  //}}}
  //{{{  set number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port);

  // get port param
  port.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParam (OMX_IndexParamPortDefinition, &port)) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " get port param");
    return false;
    }

  // set port param
  port.nBufferCountActual = max ((unsigned int)port.nBufferCountMin, mBufferLen / port.nBufferSize);
  if (mDecoder.setParam (OMX_IndexParamPortDefinition, &port)) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set port param");
    return false;
    }
  //}}}
  //{{{  set input port format
  OMX_AUDIO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);

  format.nPortIndex = mDecoder.getInputPort();
  format.eEncoding = OMX_AUDIO_CodingPCM;
  if (mDecoder.setParam (OMX_IndexParamAudioPortFormat, &format)) {
    // error, return
    cLog::log (LOGERROR, string(__func__) + " set format");
    return false;
    }
  //}}}
  if (mDecoder.allocInputBuffers()) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " allocInputBuffers");
    return false;
    }
    //}}}
  if (mDecoder.setState (OMX_StateExecuting)) {
    //{{{  return error
    cLog::log (LOGERROR, string(__func__) + " set state");
    return false;
    }
    //}}}

  //{{{  set decoder inputBuffer as waveHeader
  WAVEFORMATEXTENSIBLE waveHeader;
  memset (&waveHeader, 0, sizeof(waveHeader));

  waveHeader.Format.nChannels = 2;
  waveHeader.dwChannelMask = chanMap ? chanMap : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  waveHeader.Samples.wSamplesPerBlock = 0;
  waveHeader.Format.nChannels = mNumInputChans;
  waveHeader.Format.nBlockAlign = mNumInputChans * (mBitsPerSample >> 3);
  waveHeader.Format.nSamplesPerSec = mConfig.mHints.samplerate;
  waveHeader.Format.nAvgBytesPerSec = mBytesPerSec;
  waveHeader.Format.wBitsPerSample = mBitsPerSample;
  waveHeader.Samples.wValidBitsPerSample = mBitsPerSample;
  waveHeader.Format.cbSize = 0;
  waveHeader.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
  waveHeader.Format.wFormatTag = mBitsPerSample == 32 ? 0x8000 : WAVE_FORMAT_PCM;

  auto buffer = mDecoder.getInputBuffer();
  if (!buffer) {
     //  error, return
    cLog::log (LOGERROR, string(__func__) + " getInputBuffer");
    return false;
    }

  buffer->nOffset = 0;
  buffer->nFilledLen = min (sizeof(waveHeader), buffer->nAllocLen);
  memset (buffer->pBuffer, 0, buffer->nAllocLen);
  memcpy (buffer->pBuffer, &waveHeader, buffer->nFilledLen);
  buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

  if (mDecoder.emptyThisBuffer (buffer)) {
    //  error, return
    cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), buffer);
    return false;
    }
  //}}}
  if (mDecoder.badState())
    return false;

  mSetStartTime  = true;
  mLastPts = kNoPts;
  mSubmittedEos = false;
  mFailedEos = false;

  return true;
  }
//}}}
//{{{
bool cOmxAudio::decode (uint8_t* data, int size, double dts, double pts, atomic<bool>& flushRequested) {

  cLog::log (LOGINFO1, "decode " + frac(pts/1000000.0,6,2,' ') + " " + dec(size));

  while (size > 0) {
    if (!mOutputSize) {
      mDts = dts;
      mPts = pts;
      }

    if (!mGotFrame) {
      //{{{  decode frame from packet
      AVPacket avPacket;
      mAvCodec.av_init_packet (&avPacket);
      avPacket.data = data;
      avPacket.size = size;

      int gotFrame;
      int bytesUsed = mAvCodec.avcodec_decode_audio4 (mCodecContext, mFrame, &gotFrame, &avPacket);
      if ((bytesUsed < 0) || (bytesUsed > size)) {
        reset();
        break;
        }

      else if (gotFrame) {
        mGotFrame = true;
        data += bytesUsed;
        size -= bytesUsed;
        }
      }
      //}}}
    if (mGotFrame) {
      //{{{  convert frame, addBuffer
      if (!mGotFirstFrame) {
        cLog::log (LOGINFO, "cOmxAudio::decode - chan:%d format:%d:%d pktSize:%d samples:%d lineSize:%d",
                            mCodecContext->channels, mCodecContext->sample_fmt, mOutFormat,
                            size, mFrame->nb_samples, mFrame->linesize[0]);
        mGotFirstFrame = true;
        }

      int outLineSize;
      mOutputSize = mAvUtil.av_samples_get_buffer_size (
        &outLineSize, mCodecContext->channels, mFrame->nb_samples, mOutFormat, 1);
      // allocate enough outputBuffer
      if (mOutputAllocated < mOutputSize) {
        mOutput = (uint8_t*)mAvUtil.av_realloc (mOutput, mOutputSize + FF_INPUT_BUFFER_PADDING_SIZE);
        mOutputAllocated = mOutputSize;
        }

      // mFrame samples to outputBuffer
      if (mCodecContext->sample_fmt == mOutFormat) {
        //{{{  simple copy to mOutput
        uint8_t* out_planes[mCodecContext->channels];
        if ((mAvUtil.av_samples_fill_arrays (
               out_planes, NULL, mOutput, mCodecContext->channels, mFrame->nb_samples, mOutFormat,1) < 0) ||
             mAvUtil.av_samples_copy (
               out_planes, mFrame->data, 0, 0, mFrame->nb_samples, mCodecContext->channels, mOutFormat) < 0)
          mOutputSize = 0;
        }
        //}}}
      else {
        //{{{  convert format to mOutput
        if (mConvert &&
            ((mCodecContext->sample_fmt != mSampleFormat) ||
             (mChans != mCodecContext->channels))) {
          mSwResample.swr_free (&mConvert);
          mChans = mCodecContext->channels;
          }

        if (!mConvert) {
          mSampleFormat = mCodecContext->sample_fmt;
          mConvert = mSwResample.swr_alloc_set_opts (NULL,
                       mAvUtil.av_get_default_channel_layout(mCodecContext->channels),
                       mOutFormat, mCodecContext->sample_rate,
                       mAvUtil.av_get_default_channel_layout(mCodecContext->channels),
                       mCodecContext->sample_fmt, mCodecContext->sample_rate,
                       0, NULL);
          if (!mConvert || mSwResample.swr_init (mConvert) < 0)
            cLog::log (LOGERROR, "cOmxAudio::getData unable to initialise convert format:%d to %d",
                                 mCodecContext->sample_fmt, mOutFormat);
          }

        // use unaligned flag to keep output packed
        uint8_t* out_planes[mCodecContext->channels];
        if ((mAvUtil.av_samples_fill_arrays (
              out_planes, NULL, mOutput, mCodecContext->channels, mFrame->nb_samples, mOutFormat, 1) < 0) ||
             mSwResample.swr_convert (
               mConvert, out_planes, mFrame->nb_samples, (const uint8_t**)mFrame->data, mFrame->nb_samples) < 0) {
          cLog::log (LOGERROR, "cOmxAudio::getData decode unable to convert format %d to %d",
                               (int)mCodecContext->sample_fmt, mOutFormat);
          mOutputSize = 0;
          }
        }
        //}}}

      // done, wait for buffer and add to output
      if (mOutputSize > 0) {
        while (mOutputSize > (int)mDecoder.getInputBufferSpace()) {
          mClock->msSleep (10);
          if (flushRequested)
            return true;
           }
        addBuffer (mOutput, mOutputSize, mDts, mPts);
        mOutputSize = 0;
        }

      mGotFrame = false;
      }
      //}}}
    }

  applyVolume();
  return true;
  }
//}}}
//{{{
void cOmxAudio::submitEOS() {

  cLog::log (LOGINFO2, __func__);

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mSubmittedEos = true;
  mFailedEos = false;

  auto* buffer = mDecoder.getInputBuffer(1000);
  if (!buffer) {
    // error return
    cLog::log (LOGERROR, string(__func__) + " buffer");
    mFailedEos = true;
    return;
    }

  buffer->nOffset = 0;
  buffer->nFilledLen = 0;
  buffer->nTimeStamp = toOmxTime (0LL);
  buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;

  if (mDecoder.emptyThisBuffer (buffer)) {
    cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), buffer);
    return;
    }
  }
//}}}
//{{{
void cOmxAudio::reset() {

  cLog::log (LOGINFO2, "cOmxAudio::reset " + mConfig.mDevice);

  mAvCodec.avcodec_flush_buffers (mCodecContext);

  mGotFrame = false;
  mOutputSize = 0;
  }
//}}}
//{{{
void cOmxAudio::flush() {

  cLog::log (LOGINFO2, "cOmxAudio::flush " + mConfig.mDevice);

  lock_guard<recursive_mutex> lockGuard (mMutex);

  mDecoder.flushAll();
  if (mMixer.isInit() )
    mMixer.flushAll();
  if (mSplitter.isInit() )
    mSplitter.flushAll();

  if (mRenderAnal.isInit() )
    mRenderAnal.flushAll();
  if (mRenderHdmi.isInit() )
    mRenderHdmi.flushAll();

  if (mRenderAnal.isInit() )
    mRenderAnal.resetEos();
  if (mRenderHdmi.isInit() )
    mRenderHdmi.resetEos();

  mSetStartTime  = true;
  mLastPts = kNoPts;
  }
//}}}

// private
//{{{
uint64_t cOmxAudio::getChanMap() {

  auto bits = getCountBits (mCodecContext->channel_layout);

  uint64_t layout;
  if (bits == mCodecContext->channels)
    layout = mCodecContext->channel_layout;
  else {
    cLog::log (LOGINFO, string (__func__) +
                        " - chans:" + dec(mCodecContext->channels) +
                        " layout:" + hex(bits));
    layout = mAvUtil.av_get_default_channel_layout (mCodecContext->channels);
    }

  return layout;
  }
//}}}

//{{{
void cOmxAudio::buildChanMap (enum PCMChannels* chanMap, uint64_t layout) {

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) chanMap[index++] = PCM_FRONT_LEFT           ;
  if (layout & AV_CH_FRONT_RIGHT          ) chanMap[index++] = PCM_FRONT_RIGHT          ;
  if (layout & AV_CH_FRONT_CENTER         ) chanMap[index++] = PCM_FRONT_CENTER         ;
  if (layout & AV_CH_LOW_FREQUENCY        ) chanMap[index++] = PCM_LOW_FREQUENCY        ;
  if (layout & AV_CH_BACK_LEFT            ) chanMap[index++] = PCM_BACK_LEFT            ;
  if (layout & AV_CH_BACK_RIGHT           ) chanMap[index++] = PCM_BACK_RIGHT           ;
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) chanMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) chanMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
  if (layout & AV_CH_BACK_CENTER          ) chanMap[index++] = PCM_BACK_CENTER          ;
  if (layout & AV_CH_SIDE_LEFT            ) chanMap[index++] = PCM_SIDE_LEFT            ;
  if (layout & AV_CH_SIDE_RIGHT           ) chanMap[index++] = PCM_SIDE_RIGHT           ;
  if (layout & AV_CH_TOP_CENTER           ) chanMap[index++] = PCM_TOP_CENTER           ;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) chanMap[index++] = PCM_TOP_FRONT_LEFT       ;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) chanMap[index++] = PCM_TOP_FRONT_CENTER     ;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) chanMap[index++] = PCM_TOP_FRONT_RIGHT      ;
  if (layout & AV_CH_TOP_BACK_LEFT        ) chanMap[index++] = PCM_TOP_BACK_LEFT        ;
  if (layout & AV_CH_TOP_BACK_CENTER      ) chanMap[index++] = PCM_TOP_BACK_CENTER      ;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) chanMap[index++] = PCM_TOP_BACK_RIGHT       ;

  while (index < OMX_AUDIO_MAXCHANNELS)
    chanMap[index++] = PCM_INVALID;
  }
//}}}
//{{{
// See CEA spec: Table 20, Audio InfoFrame data byte 4 for the ordering here
int cOmxAudio::buildChanMapCEA (enum PCMChannels* chanMap, uint64_t layout) {

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT   ) chanMap[index++] = PCM_FRONT_LEFT;
  if (layout & AV_CH_FRONT_RIGHT  ) chanMap[index++] = PCM_FRONT_RIGHT;
  if (layout & AV_CH_LOW_FREQUENCY) chanMap[index++] = PCM_LOW_FREQUENCY;
  if (layout & AV_CH_FRONT_CENTER ) chanMap[index++] = PCM_FRONT_CENTER;
  if (layout & AV_CH_BACK_LEFT    ) chanMap[index++] = PCM_BACK_LEFT;
  if (layout & AV_CH_BACK_RIGHT   ) chanMap[index++] = PCM_BACK_RIGHT;
  if (layout & AV_CH_SIDE_LEFT    ) chanMap[index++] = PCM_SIDE_LEFT;
  if (layout & AV_CH_SIDE_RIGHT   ) chanMap[index++] = PCM_SIDE_RIGHT;

  while (index < OMX_AUDIO_MAXCHANNELS)
    chanMap[index++] = PCM_INVALID;

  int numChans = 0;
  for (index = 0; index < OMX_AUDIO_MAXCHANNELS; index++)
    if (chanMap[index] != PCM_INVALID)
       numChans = index+1;

  // round up to power of 2
  numChans = numChans > 4 ? 8 : numChans > 2 ? 4 : numChans;
  return numChans;
  }
//}}}
//{{{
void cOmxAudio::buildChanMapOMX (enum OMX_AUDIO_CHANNELTYPE* chanMap, uint64_t layout) {

  int index = 0;

  if (layout & AV_CH_FRONT_LEFT           ) chanMap[index++] = OMX_AUDIO_ChannelLF;
  if (layout & AV_CH_FRONT_RIGHT          ) chanMap[index++] = OMX_AUDIO_ChannelRF;
  if (layout & AV_CH_FRONT_CENTER         ) chanMap[index++] = OMX_AUDIO_ChannelCF;
  if (layout & AV_CH_LOW_FREQUENCY        ) chanMap[index++] = OMX_AUDIO_ChannelLFE;
  if (layout & AV_CH_BACK_LEFT            ) chanMap[index++] = OMX_AUDIO_ChannelLR;
  if (layout & AV_CH_BACK_RIGHT           ) chanMap[index++] = OMX_AUDIO_ChannelRR;
  if (layout & AV_CH_SIDE_LEFT            ) chanMap[index++] = OMX_AUDIO_ChannelLS;
  if (layout & AV_CH_SIDE_RIGHT           ) chanMap[index++] = OMX_AUDIO_ChannelRS;
  if (layout & AV_CH_BACK_CENTER          ) chanMap[index++] = OMX_AUDIO_ChannelCS;

  // following are not in openmax spec, but gpu does accept them
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)10;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)11;
  if (layout & AV_CH_TOP_CENTER           ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)12;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)13;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)14;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)15;
  if (layout & AV_CH_TOP_BACK_LEFT        ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)16;
  if (layout & AV_CH_TOP_BACK_CENTER      ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)17;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) chanMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)18;

  while (index < OMX_AUDIO_MAXCHANNELS)
    chanMap[index++] = OMX_AUDIO_ChannelNone;
  }
//}}}

//{{{
bool cOmxAudio::srcChanged() {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mMixer.isInit()) {
    //{{{  disable, enable, no change, return
    cLog::log (LOGINFO, string(__func__) + " - disable, enable");
    mDecoder.disablePort (mDecoder.getOutputPort(), true);
    mDecoder.enablePort (mDecoder.getOutputPort(), true);
    return true;
    }
    //}}}
  if (!mMixer.init ("OMX.broadcom.audio_mixer", OMX_IndexParamAudioInit))
    return false;
  if (mConfig.mDevice == "omx:both")
    if (!mSplitter.init ("OMX.broadcom.audio_splitter", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:local")
    if (!mRenderAnal.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
  if (mConfig.mDevice == "omx:both" || mConfig.mDevice == "omx:hdmi")
    if (!mRenderHdmi.init ("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;

  //{{{  setup decoder output pcmMode
  OMX_AUDIO_PARAM_PCMMODETYPE pcmMode;
  OMX_INIT_STRUCTURE(pcmMode);
  pcmMode.nPortIndex = mDecoder.getOutputPort();
  if (mDecoder.getParam (OMX_IndexParamAudioPcm, &pcmMode)) {
    // error return
    cLog::log (LOGERROR, string(__func__) + "getParam pcmMode");
    return false;
    }

  memcpy (pcmMode.eChannelMapping, mOutputChans, sizeof(mOutputChans));
  pcmMode.nChannels = mNumOutputChans > 4 ? 8 : mNumOutputChans > 2 ? 4 : mNumOutputChans;
  pcmMode.nSamplingRate = min (max ((int)pcmMode.nSamplingRate, 8000), 192000);
  pcmMode.nPortIndex = mMixer.getOutputPort();
  if (mMixer.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
    // error return
    cLog::log (LOGERROR,  string(__func__) + " setParam pcmMode");
    return false;
    }
  //}}}
  cLog::log (LOGINFO, string(__func__) +
                      " - " + dec(pcmMode.nChannels) + "x" + dec(pcmMode.nBitPerSample) +
                      "@" + dec(pcmMode.nSamplingRate) +
                      " bufferLen:" +  dec(mBufferLen) +
                      " bytesPerSec:" + dec(mBytesPerSec));
  if (mSplitter.isInit()) {
    //{{{  wireup splitter to pcm output
    // setup splitter
    pcmMode.nPortIndex = mSplitter.getInputPort();
    if (mSplitter.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mSplitter setParam");
      return false;
      }

    pcmMode.nPortIndex = mSplitter.getOutputPort();
    if (mSplitter.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mSplitter setParam");
      return false;
      }

    pcmMode.nPortIndex = mSplitter.getOutputPort() + 1;
    if (mSplitter.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " setParam");
      return false;
      }
    }
    //}}}
  if (mRenderAnal.isInit()) {
    //{{{  wireup analRender and pcm output
    pcmMode.nPortIndex = mRenderAnal.getInputPort();
    if (mRenderAnal.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mRenderAnal setParam");
      return false;
      }
    }
    //}}}
  if (mRenderHdmi.isInit()) {
    //{{{  wireup hdmiRender and pcm output
    pcmMode.nPortIndex = mRenderHdmi.getInputPort();
    if (mRenderHdmi.setParam (OMX_IndexParamAudioPcm, &pcmMode)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mRenderhdmi setParam");
      return false;
      }
    }
    //}}}
  if (mRenderAnal.isInit()) {
    //{{{  setup analRender
    mTunnelClockAnalog.init (mClock->getOmxCore(), mClock->getOmxCore()->getInputPort(),
                             &mRenderAnal, mRenderAnal.getInputPort() + 1);

    if (mTunnelClockAnalog.establish()) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mTunnel_clock_Analog.Establish");
      return false;
      }
    mRenderAnal.resetEos();
    }
    //}}}
  if (mRenderHdmi.isInit() ) {
    //{{{  setup hdmiRender
    mTunnelClockHdmi.init (
      mClock->getOmxCore(), mClock->getOmxCore()->getInputPort() + (mRenderAnal.isInit() ? 2 : 0),
      &mRenderHdmi, mRenderHdmi.getInputPort() + 1);

    if (mTunnelClockHdmi.establish()) {
      // error return
      cLog::log(LOGERROR, string(__func__) + " mTunnel_clock_hdmi.establish");
      return false;
      }
    mRenderHdmi.resetEos();
    }
    //}}}
  if (mRenderAnal.isInit() ) {
    //{{{  more setup analRender
    // default audio_render is the clock master
    // - if output samples don't fit the timestamps, it will speed up/slow down the clock.
    // - this tends to be better for maintaining audio sync and avoiding audio glitches,
    //   but can affect video/display sync when in dual audio mode, make Analogue the slave
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = (mConfig.mDevice == "omx:both") ? OMX_FALSE : OMX_TRUE;
    if (mRenderAnal.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " setClockRef");
      return false;
      }

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy ((char*)audioDest.sName, "local", sizeof(audioDest.sName));
    if (mRenderAnal.setConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mRenderAnal.setConfig");
      return false;
      }
    }
    //}}}
  if (mRenderHdmi.isInit() ) {
    //{{{  set clock ref src
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);

    //configBool.bEnabled = mConfig.mIsLive ? OMX_FALSE : OMX_TRUE;
    configBool.bEnabled = OMX_TRUE;
    if (mRenderHdmi.setConfig (OMX_IndexConfigBrcmClockReferenceSource, &configBool))
      return false;
    //}}}
    //{{{  set audio dst
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);

    strncpy ((char*)audioDest.sName, "hdmi", strlen("hdmi"));
    if (mRenderHdmi.setConfig (OMX_IndexConfigBrcmAudioDestination, &audioDest)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mRenderhdmi.setConfig");
      return false;
      }
    //}}}
    }
  if (mSplitter.isInit() ) {
    //{{{  wire up splitter
    mTunnelSplitterAnalog.init (&mSplitter, mSplitter.getOutputPort(), &mRenderAnal,
                                mRenderAnal.getInputPort());
    if (mTunnelSplitterAnalog.establish()) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mTunnelSplitterAnalog.establish");
      return false;
      }

    mTunnelSplitterHdmi.init (&mSplitter, mSplitter.getOutputPort() + 1, &mRenderHdmi,
                              mRenderHdmi.getInputPort());
    if (mTunnelSplitterHdmi.establish()) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mTunnelSplitterhdmi.establish");
      return false;
      }
    }
    //}}}
  //{{{  wire up mixer
  mTunnelDecoder.init (&mDecoder, mDecoder.getOutputPort(), &mMixer, mMixer.getInputPort());

  if (mSplitter.isInit())
    mTunnelMixer.init (&mMixer, mMixer.getOutputPort(), &mSplitter, mSplitter.getInputPort());
  else {
    if (mRenderAnal.isInit())
      mTunnelMixer.init (&mMixer, mMixer.getOutputPort(), &mRenderAnal, mRenderAnal.getInputPort());
    if (mRenderHdmi.isInit())
      mTunnelMixer.init (&mMixer, mMixer.getOutputPort(), &mRenderHdmi, mRenderHdmi.getInputPort());
    }
  //}}}

  if (mTunnelDecoder.establish()) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mTunnel_decoder.establish");
    return false;
    }
    //}}}
  if (mMixer.setState (OMX_StateExecuting)) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mMixer setState");
    return false;
    }
    //}}}
  if (mTunnelMixer.establish()) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " mTunnel_decoder.establish");
    return false;
    }
    //}}}

  if (mSplitter.isInit())
    if (mSplitter.setState (OMX_StateExecuting)) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mSplitter setState");
      return false;
      }
      //}}}
  if (mRenderAnal.isInit())
    if (mRenderAnal.setState (OMX_StateExecuting)) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderAnal setState");
      return false;
      }
      //}}}
  if (mRenderHdmi.isInit())
    if (mRenderHdmi.setState (OMX_StateExecuting)) {
      //{{{  error return
      cLog::log (LOGERROR, string(__func__) + " mRenderhdmi setState");
      return false;
      }
      //}}}

  return true;
  }
//}}}
//{{{
void cOmxAudio::applyVolume() {

  float volume = mMute ? 0.f : mCurVolume;
  if ((volume != mLastVolume) && mMixer.isInit()) {
    lock_guard<recursive_mutex> lockGuard (mMutex);

    // set mixer downmix coeffs
    OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
    OMX_INIT_STRUCTURE(mix);

    mix.nPortIndex = mMixer.getInputPort();
    const float* coeff = mDownmixMatrix;
    for (size_t i = 0; i < 8*8; ++i)
      mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * volume));

    if (mMixer.setConfig (OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix)) {
      // error return
      cLog::log (LOGERROR, string(__func__) + " mixer setDownmix");
      return;
      }

    cLog::log (LOGINFO1, string(__func__) + " - changed " + frac(volume, 3,1,' '));
    mLastVolume = volume;
    }
  }
//}}}
//{{{
void cOmxAudio::addBuffer (uint8_t* data, int size, double dts, double pts) {

  lock_guard<recursive_mutex> lockGuard (mMutex);

  auto buffer = mDecoder.getInputBuffer (200);
  if (!buffer) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__) + " timeout");
    return;
    }
    //}}}

  // set buffer datas
  buffer->nOffset = 0;
  buffer->nFilledLen = size;
  memcpy (buffer->pBuffer, data, size);

  // set buffer flags and timestamp
  buffer->nTimeStamp = toOmxTime ((uint64_t)(pts == kNoPts) ? 0 : pts);
  buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;

  if (mSetStartTime) {
    buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
    mLastPts = pts;
    cLog::log (LOGINFO, string(__func__) + " - setStartTime " + frac(pts/kPtsScale, 6,2,' '));
    mSetStartTime = false;
    }
  else if (pts == kNoPts) {
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

  if (mDecoder.emptyThisBuffer (buffer)) {
    //{{{  error, return
    cLog::log (LOGERROR, string(__func__) + " emptyThisBuffer");
    mDecoder.decoderEmptyBufferDone (mDecoder.getHandle(), buffer);
    return;
    }
    //}}}

  if (!mDecoder.waitEvent (OMX_EventPortSettingsChanged, 0))
    if (!srcChanged())
      cLog::log (LOGERROR, string(__func__) + "  srcChanged");
  }
//}}}
