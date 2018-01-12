// cSwAudio.cpp
//{{{  includes
#include "cAudio.h"

#include "../shared/utils/cLog.h"
#include "cPcmRemap.h"

using namespace std;
//}}}

#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)

// local
const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};
//{{{
unsigned countBits (int64_t value) {

  unsigned bits = 0;
  for (; value; ++bits)
    value &= value - 1;
  return bits;
  }
//}}}

//{{{
cSwAudio::~cSwAudio() {

  mAvUtil.av_free(mBufferOutput);
  mBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  m_iBufferOutputUsed = 0;

  dispose();
  }
//}}}

//{{{
uint64_t cSwAudio::getChannelMap() {

  int bits = countBits (mCodecContext->channel_layout);

  uint64_t layout;
  if (bits == mCodecContext->channels)
    layout = mCodecContext->channel_layout;
  else {
    cLog::log (LOGINFO, "cSwAudio - GetChannelMap channels:%d layout:%d",
                        mCodecContext->channels, bits);
    layout = mAvUtil.av_get_default_channel_layout (mCodecContext->channels);
    }

  return layout;
  }
//}}}
//{{{
int cSwAudio::getData (BYTE** dst, double& dts, double& pts) {

  if (!mGotFrame)
    return 0;

  // input audio is aligned
  int inLineSize;
  int inputSize = mAvUtil.av_samples_get_buffer_size (&inLineSize, mCodecContext->channels, mFrame1->nb_samples, mCodecContext->sample_fmt, 0);

  // output audio will be packed
  int outLineSize;
  int outputSize = mAvUtil.av_samples_get_buffer_size (&outLineSize, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1);

  if (!mNoConcatenate && m_iBufferOutputUsed && (int)m_frameSize != outputSize) {
    cLog::log (LOGERROR, "cSwAudio::getData size:%d->%d", m_frameSize, outputSize);
    mNoConcatenate = true;
    }

  // if this buffer won't fit then flush out what we have
  int desired_size = AUDIO_DECODE_OUTPUT_BUFFER * (mCodecContext->channels * getBitsPerSample()) >> (rounded_up_channels_shift[mCodecContext->channels] + 4);
  if (m_iBufferOutputUsed && (m_iBufferOutputUsed + outputSize > desired_size || mNoConcatenate)) {
    int ret = m_iBufferOutputUsed;
    m_iBufferOutputUsed = 0;
    mNoConcatenate = false;
    dts = mDts;
    pts = mPts;
    *dst = mBufferOutput;
    return ret;
    }
  m_frameSize = outputSize;

  if (m_iBufferOutputAlloced < m_iBufferOutputUsed + outputSize) {
    mBufferOutput = (BYTE*)mAvUtil.av_realloc(mBufferOutput, m_iBufferOutputUsed + outputSize + FF_INPUT_BUFFER_PADDING_SIZE);
    m_iBufferOutputAlloced = m_iBufferOutputUsed + outputSize;
    }

  // need to convert format
  if (mCodecContext->sample_fmt != m_desiredSampleFormat) {
    if (mConvert && (mCodecContext->sample_fmt != m_iSampleFormat || m_channels != mCodecContext->channels)) {
      mSwResample.swr_free (&mConvert);
      m_channels = mCodecContext->channels;
      }

    if (!mConvert) {
      m_iSampleFormat = mCodecContext->sample_fmt;
      mConvert = mSwResample.swr_alloc_set_opts (NULL,
                   mAvUtil.av_get_default_channel_layout(mCodecContext->channels),
                   m_desiredSampleFormat, mCodecContext->sample_rate,
                   mAvUtil.av_get_default_channel_layout(mCodecContext->channels),
                   mCodecContext->sample_fmt, mCodecContext->sample_rate,
                   0, NULL);

      if (!mConvert || mSwResample.swr_init(mConvert) < 0) {
        cLog::log (LOGERROR, "cSwAudio::getData unable to initialise convert format:%d to %d",
                             mCodecContext->sample_fmt, m_desiredSampleFormat);
        return 0;
        }
      }

    // use unaligned flag to keep output packed
    uint8_t* out_planes[mCodecContext->channels];
    if (mAvUtil.av_samples_fill_arrays(out_planes, NULL, mBufferOutput + m_iBufferOutputUsed, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
       mSwResample.swr_convert(mConvert, out_planes, mFrame1->nb_samples, (const uint8_t **)mFrame1->data, mFrame1->nb_samples) < 0) {
      cLog::log (LOGERROR, "cSwAudio::getData decode unable to convert format %d to %d",
                           (int)mCodecContext->sample_fmt, m_desiredSampleFormat);
      outputSize = 0;
      }
    }
  else {
    // copy to a contiguous buffer
    uint8_t* out_planes[mCodecContext->channels];
    if (mAvUtil.av_samples_fill_arrays(out_planes, NULL, mBufferOutput + m_iBufferOutputUsed, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
      mAvUtil.av_samples_copy(out_planes, mFrame1->data, 0, 0, mFrame1->nb_samples, mCodecContext->channels, m_desiredSampleFormat) < 0 )
      outputSize = 0;
    }
  mGotFrame = false;

  if (mFirstFrame)
    cLog::log (LOGINFO1, "cSwAudio::getData size:%d/%d line:%d/%d buf:%p, desired:%d",
               inputSize, outputSize, inLineSize, outLineSize, mBufferOutput, desired_size);
  mFirstFrame = false;

  m_iBufferOutputUsed += outputSize;
  return 0;
  }
//}}}

//{{{
bool cSwAudio::open (cOmxStreamInfo &hints, enum PCMLayout layout) {

  cLog::log (LOGINFO, "cSwAudio::open");

  mAvCodec.avcodec_register_all();

  auto codec = mAvCodec.avcodec_find_decoder (hints.codec);
  if (!codec) {
    //{{{  error return
    cLog::log (LOGINFO1,"cSwAudio::open no codec %d", hints.codec);
    return false;
    }
    //}}}

  mFirstFrame = true;
  mCodecContext = mAvCodec.avcodec_alloc_context3 (codec);
  mCodecContext->debug_mv = 0;
  mCodecContext->debug = 0;
  mCodecContext->workaround_bugs = 1;

  if (codec->capabilities & CODEC_CAP_TRUNCATED)
    mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  mCodecContext->channels = hints.channels;
  mCodecContext->sample_rate = hints.samplerate;
  mCodecContext->block_align = hints.blockalign;
  mCodecContext->bit_rate = hints.bitrate;
  mCodecContext->bits_per_coded_sample = hints.bitspersample;
  if (hints.codec == AV_CODEC_ID_TRUEHD) {
    //{{{  truehd
    if (layout == PCM_LAYOUT_2_0) {
      mCodecContext->request_channel_layout = AV_CH_LAYOUT_STEREO;
      mCodecContext->channels = 2;
      mCodecContext->channel_layout = mAvUtil.av_get_default_channel_layout (mCodecContext->channels);
      }
    else if (layout <= PCM_LAYOUT_5_1) {
      mCodecContext->request_channel_layout = AV_CH_LAYOUT_5POINT1;
      mCodecContext->channels = 6;
      mCodecContext->channel_layout = mAvUtil.av_get_default_channel_layout (mCodecContext->channels);
      }
    }
    //}}}
  if (mCodecContext->request_channel_layout)
    cLog::log (LOGINFO, "cSwAudio::open - channel layout %x",
                        (unsigned)mCodecContext->request_channel_layout);

  if (mCodecContext->bits_per_coded_sample == 0)
    mCodecContext->bits_per_coded_sample = 16;

  if (hints.extradata && hints.extrasize > 0 ) {
    //{{{  extradata
    mCodecContext->extradata_size = hints.extrasize;
    mCodecContext->extradata = (uint8_t*)mAvUtil.av_mallocz (hints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy (mCodecContext->extradata, hints.extradata, hints.extrasize);
    }
    //}}}

  if (mAvCodec.avcodec_open2 (mCodecContext, codec, NULL) < 0) {
    //{{{  error return
    cLog::log (LOGERROR, "cSwAudio::open cannot open codec");
    dispose();
    return false;
    }
    //}}}

  mFrame1 = mAvCodec.av_frame_alloc();
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat =
    (mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16) ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;

  return true;
  }
//}}}
//{{{
int cSwAudio::decode (BYTE* pData, int iSize, double dts, double pts) {

  int iBytesUsed, got_frame;

  AVPacket avpkt;
  if (!m_iBufferOutputUsed) {
    mDts = dts;
    mPts = pts;
    }

  if (mGotFrame)
    return 0;

  mAvCodec.av_init_packet (&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = mAvCodec.avcodec_decode_audio4 (mCodecContext, mFrame1, &got_frame, &avpkt);
  if (iBytesUsed < 0 || !got_frame)
    return iBytesUsed;

  // some codecs will attempt to consume more data than what we gave
  if (iBytesUsed > iSize) {
    cLog::log (LOGINFO1, "cSwAudio - Decode attempted to consume more data than given");
    iBytesUsed = iSize;
    }
  mGotFrame = true;

  if (mFirstFrame)
    cLog::log (LOGINFO, "cSwAudio - Decode %d format:%d:%d chan:%d samples:%d lineSize:%d",
               iSize,
               mCodecContext->sample_fmt, m_desiredSampleFormat,
               mCodecContext->channels,
               mFrame1->nb_samples, mFrame1->linesize[0]
               );

  return iBytesUsed;
  }
//}}}
//{{{
void cSwAudio::reset() {

  mAvCodec.avcodec_flush_buffers (mCodecContext);
  mGotFrame = false;
  m_iBufferOutputUsed = 0;
  }
//}}}
//{{{
void cSwAudio::dispose() {

  if (mFrame1)
    mAvUtil.av_free (mFrame1);
  mFrame1 = NULL;

  if (mConvert)
    mSwResample.swr_free (&mConvert);

  if (mCodecContext) {
    if (mCodecContext->extradata)
      mAvUtil.av_free (mCodecContext->extradata);
    mCodecContext->extradata = NULL;

    mAvCodec.avcodec_close (mCodecContext);
    mAvUtil.av_free (mCodecContext);
    mCodecContext = NULL;
    }

  mGotFrame = false;
  }
//}}}
