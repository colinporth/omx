// cSwAudio.cpp
//{{{  includes
#include "cAudio.h"

#include "cLog.h"
#include "cPcmRemap.h"
//}}}
#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};

//{{{
static unsigned count_bits (int64_t value) {
  unsigned bits = 0;
  for (; value; ++bits)
    value &= value - 1;
  return bits;
  }
//}}}

//{{{
cSwAudio::cSwAudio() {

  mBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  m_iBufferOutputUsed = 0;

  mCodecContext = NULL;
  mConvert = NULL;
  m_bOpenedCodec = false;

  m_channels = 0;
  mFrame1 = NULL;
  m_frameSize = 0;
  m_bGotFrame = false;
  m_bNoConcatenate = false;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = AV_SAMPLE_FMT_NONE;
  }
//}}}
//{{{
cSwAudio::~cSwAudio() {
  mAvUtil.av_free (mBufferOutput);
  mBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  m_iBufferOutputUsed = 0;
  Dispose();
  }
//}}}

//{{{
bool cSwAudio::Open (cOmxStreamInfo &hints, enum PCMLayout layout) {

  m_bOpenedCodec = false;

  mAvCodec.avcodec_register_all();

  auto avCodec = mAvCodec.avcodec_find_decoder (hints.codec);
  if (!avCodec) {
    cLog::Log (LOGDEBUG,"cSwAudio::Open no codec %d", hints.codec);
    return false;
    }

  m_bFirstFrame = true;
  mCodecContext = mAvCodec.avcodec_alloc_context3 (avCodec);
  mCodecContext->debug_mv = 0;
  mCodecContext->debug = 0;
  mCodecContext->workaround_bugs = 1;

  if (avCodec->capabilities & CODEC_CAP_TRUNCATED)
    mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  mCodecContext->channels = hints.channels;
  mCodecContext->sample_rate = hints.samplerate;
  mCodecContext->block_align = hints.blockalign;
  mCodecContext->bit_rate = hints.bitrate;
  mCodecContext->bits_per_coded_sample = hints.bitspersample;
  if (hints.codec == AV_CODEC_ID_TRUEHD) {
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
  if (mCodecContext->request_channel_layout)
    cLog::Log (LOGNOTICE,"cSwAudio::Open channel layout %x", (unsigned)mCodecContext->request_channel_layout);

  if (mCodecContext->bits_per_coded_sample == 0)
    mCodecContext->bits_per_coded_sample = 16;

  if (hints.extradata && hints.extrasize > 0 ) {
    mCodecContext->extradata_size = hints.extrasize;
    mCodecContext->extradata = (uint8_t*)mAvUtil.av_mallocz (hints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy (mCodecContext->extradata, hints.extradata, hints.extrasize);
    }

  if (mAvCodec.avcodec_open2 (mCodecContext, avCodec, NULL) < 0) {
    cLog::Log (LOGDEBUG, "cSwAudio::Open cannot open codec");
    Dispose();
    return false;
    }

  mFrame1 = mAvCodec.av_frame_alloc();
  m_bOpenedCodec = true;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;

  cLog::Log (LOGDEBUG, "cOmxAudio::Open");

  return true;
  }
//}}}
//{{{
void cSwAudio::Dispose() {

  if (mFrame1)
    mAvUtil.av_free (mFrame1);
  mFrame1 = NULL;

  if (mConvert)
    mSwResample.swr_free (&mConvert);

  if (mCodecContext) {
    if (mCodecContext->extradata)
      mAvUtil.av_free (mCodecContext->extradata);
    mCodecContext->extradata = NULL;
    if (m_bOpenedCodec)
      mAvCodec.avcodec_close (mCodecContext);
    m_bOpenedCodec = false;
    mAvUtil.av_free (mCodecContext);
    mCodecContext = NULL;
    }

  m_bGotFrame = false;
  }
//}}}

//{{{
int cSwAudio::GetChannels() {
  return mCodecContext ? mCodecContext->channels : 0;
  }
//}}}
//{{{
int cSwAudio::GetSampleRate() {
  return mCodecContext ? mCodecContext->sample_rate : 0;
  }
//}}}
//{{{
int cSwAudio::GetBitsPerSample() {
  return mCodecContext ? (mCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32) : 0;
  }
//}}}
//{{{
int cSwAudio::GetBitRate() {
  return mCodecContext ? mCodecContext->bit_rate : 0;
  }
//}}}
//{{{
uint64_t cSwAudio::GetChannelMap() {

  uint64_t layout;
  int bits = count_bits (mCodecContext->channel_layout);
  if (bits == mCodecContext->channels)
    layout = mCodecContext->channel_layout;
  else {
    cLog::Log (LOGINFO, "cSwAudio::GetChannelMap channels:%d layout %d", mCodecContext->channels, bits);
    layout = mAvUtil.av_get_default_channel_layout(mCodecContext->channels);
    }

  return layout;
  }
//}}}

//{{{
int cSwAudio::Decode (BYTE* pData, int iSize, double dts, double pts) {

  int iBytesUsed, got_frame;
  if (!mCodecContext)
    return -1;

  AVPacket avpkt;
  if (!m_iBufferOutputUsed) {
    m_dts = dts;
    m_pts = pts;
    }
  if (m_bGotFrame)
    return 0;

  mAvCodec.av_init_packet(&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = mAvCodec.avcodec_decode_audio4 (mCodecContext, mFrame1, &got_frame, &avpkt);
  if (iBytesUsed < 0 || !got_frame)
    return iBytesUsed;

  /* some codecs will attempt to consume more data than what we gave */
  if (iBytesUsed > iSize) {
    cLog::Log (LOGWARNING, "cSwAudio::Decode attempted to consume more data than given");
    iBytesUsed = iSize;
    }
  m_bGotFrame = true;

  if (m_bFirstFrame)
    cLog::Log (LOGDEBUG, "cSwAudio::Decode %p:%d f:%d:%d ch:%d sm:%d sz:%d %p:%p:%p:%p:%p:%p:%p:%p",
               pData, iSize,
               mCodecContext->sample_fmt, m_desiredSampleFormat,
               mCodecContext->channels, mFrame1->nb_samples, mFrame1->linesize[0],
               mFrame1->data[0], mFrame1->data[1], mFrame1->data[2], mFrame1->data[3],
               mFrame1->data[4], mFrame1->data[5], mFrame1->data[6], mFrame1->data[7]
               );

  return iBytesUsed;
  }
//}}}
//{{{
int cSwAudio::GetData (BYTE** dst, double& dts, double& pts) {

  if (!m_bGotFrame)
    return 0;

  int inLineSize, outLineSize;

  /* input audio is aligned */
  int inputSize = mAvUtil.av_samples_get_buffer_size(&inLineSize, mCodecContext->channels, mFrame1->nb_samples, mCodecContext->sample_fmt, 0);

  /* output audio will be packed */
  int outputSize = mAvUtil.av_samples_get_buffer_size(&outLineSize, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1);

  if (!m_bNoConcatenate && m_iBufferOutputUsed && (int)m_frameSize != outputSize) {
    cLog::Log (LOGERROR, "cSwAudio::GetData size:%d->%d", m_frameSize, outputSize);
    m_bNoConcatenate = true;
    }

  // if this buffer won't fit then flush out what we have
  int desired_size = AUDIO_DECODE_OUTPUT_BUFFER * (mCodecContext->channels * GetBitsPerSample()) >> (rounded_up_channels_shift[mCodecContext->channels] + 4);
  if (m_iBufferOutputUsed && (m_iBufferOutputUsed + outputSize > desired_size || m_bNoConcatenate)) {
    int ret = m_iBufferOutputUsed;
    m_iBufferOutputUsed = 0;
    m_bNoConcatenate = false;
    dts = m_dts;
    pts = m_pts;
    *dst = mBufferOutput;
    return ret;
    }
  m_frameSize = outputSize;

  if (m_iBufferOutputAlloced < m_iBufferOutputUsed + outputSize) {
    mBufferOutput = (BYTE*)mAvUtil.av_realloc(mBufferOutput, m_iBufferOutputUsed + outputSize + FF_INPUT_BUFFER_PADDING_SIZE);
    m_iBufferOutputAlloced = m_iBufferOutputUsed + outputSize;
    }

  /* need to convert format */
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

      if (!mConvert || mSwResample.swr_init (mConvert) < 0) {
        cLog::Log (LOGERROR, "cSwAudio::Decode unable to initialise convert format:%d to %d",
                   mCodecContext->sample_fmt, m_desiredSampleFormat);
        return 0;
        }
      }

    /* use unaligned flag to keep output packed */
    uint8_t *out_planes[mCodecContext->channels];
    if (mAvUtil.av_samples_fill_arrays(out_planes, NULL, mBufferOutput + m_iBufferOutputUsed, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
       mSwResample.swr_convert(mConvert, out_planes, mFrame1->nb_samples, (const uint8_t**)mFrame1->data, mFrame1->nb_samples) < 0) {
      cLog::Log (LOGERROR, "cSwAudio::Decode unable to convert format %d to %d",
                (int)mCodecContext->sample_fmt, m_desiredSampleFormat);
      outputSize = 0;
      }
    }
  else {
    /* copy to a contiguous buffer */
    uint8_t *out_planes[mCodecContext->channels];
    if (mAvUtil.av_samples_fill_arrays(out_planes, NULL, mBufferOutput + m_iBufferOutputUsed, mCodecContext->channels, mFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
      mAvUtil.av_samples_copy(out_planes, mFrame1->data, 0, 0, mFrame1->nb_samples, mCodecContext->channels, m_desiredSampleFormat) < 0 )
      outputSize = 0;
    }
  m_bGotFrame = false;

  if (m_bFirstFrame) {
    cLog::Log (LOGDEBUG, "cSwAudio::GetData size:%d/%d line:%d/%d buf:%p, desired:%d",
               inputSize, outputSize, inLineSize, outLineSize, mBufferOutput, desired_size);
    m_bFirstFrame = false;
    }

  m_iBufferOutputUsed += outputSize;
  return 0;
  }
//}}}
//{{{
void cSwAudio::Reset() {

  if (mCodecContext)
    mAvCodec.avcodec_flush_buffers (mCodecContext);

  m_bGotFrame = false;
  m_iBufferOutputUsed = 0;
  }
//}}}
