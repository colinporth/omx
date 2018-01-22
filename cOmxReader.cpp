// cOmxReader.cpp
//{{{  includes
#include <stdio.h>
#include <sys/stat.h>
#include <string>

#include "cOmxReader.h"

#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"

#include "cOmxClock.h"

using namespace std;
//}}}
//{{{  defines
#define FFMPEG_FILE_BUFFER_SIZE  32768

#define READ_TRUNCATED 0x01  // handle truncated reads, function returns before entire buffer filled
#define READ_CHUNKED   0x02  // read minimum defined chunk size, this disables internal cache then
#define READ_CACHED    0x04  // use cache to access this file
#define READ_NO_CACHE  0x08  // open without caching. regardless to file type
#define READ_BITRATE   0x10  // calcuate bitrate for file while reading
//}}}
//{{{
typedef enum {
  IOCTRL_NATIVE        = 1, /**< SNativeIoControl structure, containing what should be passed to native ioctrl */
  IOCTRL_SEEK_POSSIBLE = 2, /**< return 0 if known not to work, 1 if it should work */
  IOCTRL_CACHE_STATUS  = 3, /**< SCacheStatus structure */
  IOCTRL_CACHE_SETRATE = 4, /**< unsigned int with with speed limit for caching in bytes per second */
  } eIoControl;
//}}}

// local
//{{{  vars
int64_t timeoutStart;
int64_t timeoutDefaultDuration;
int64_t timeoutDuration;
//}}}
//{{{
class cFile {
public:
  //{{{
  ~cFile() {
    if (mFile && !mPipe)
      fclose (mFile);
    }
  //}}}

  //{{{
  bool open (const string& strFileName, unsigned int flags) {

    mFlags = flags;

    if (strFileName.compare (0, 5, "pipe:") == 0) {
      mPipe = true;
      mFile = stdin;
      mLength = 0;
      return true;
      }

    mFile = fopen64 (strFileName.c_str(), "r");
    if (!mFile)
      return false;

    fseeko64 (mFile, 0, SEEK_END);
    mLength = ftello64 (mFile);

    fseeko64 (mFile, 0, SEEK_SET);

    return true;
    }
  //}}}
  //{{{
  unsigned int read (void* buffer, int64_t bufferSize) {
    return mFile ? fread (buffer, 1, bufferSize, mFile) : 0;
    }
  //}}}
  //{{{
  int ioControl (eIoControl request, void* param) {

    if (request == IOCTRL_SEEK_POSSIBLE && mFile) {
      if (mPipe)
        return false;

      struct stat st;
      if (fstat(fileno(mFile), &st) == 0)
        return !S_ISFIFO(st.st_mode);
      }

    return -1;
    }
  //}}}
  //{{{
  int64_t seek (int64_t iFilePosition, int iWhence) {
    return mFile ? fseeko64 (mFile, iFilePosition, iWhence) : -1;
    }
  //}}}
  //{{{
  void close() {

    if (mFile && !mPipe)
      fclose (mFile);

    mFile = NULL;
    }
  //}}}

  int64_t getLength() { return mLength; }
  int getChunkSize() { return 6144 /*FFMPEG_FILE_BUFFER_SIZE*/; };
  //{{{
  bool isEOF() {

    if (!mFile)
      return -1;

    if (mPipe)
      return false;

    return feof(mFile) != 0;
    }
  //}}}
  //{{{
  bool exists (const string& strFileName, bool bUseCache = true) {

    if (strFileName.compare (0, 5, "pipe:") == 0)
      return true;

    FILE* file = fopen64 (strFileName.c_str(), "r");
    if (!file)
      return false;

    fclose (file);
    return true;
    }
  //}}}
  int64_t getPosition() { return mFile ? ftello64 (mFile) : -1; }

private:
  unsigned int mFlags = 0;
  FILE* mFile = nullptr;
  int64_t mLength = 0;
  bool mPipe = false;
  };
//}}}
//{{{
int64_t currentHostCounter() {

  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (((int64_t)now.tv_sec * 1000000000LL) + now.tv_nsec);
  }
//}}}
//{{{
int interruptCb (void* unused) {

  int ret = 0;
  if (timeoutDuration && currentHostCounter() - timeoutStart > timeoutDuration) {
    cLog::log (LOGERROR, string(__func__) + " Timed out");
    ret = 1;
    }

  return ret;
  }
//}}}
//{{{
int fileRead (void* h, uint8_t* buf, int size) {

  cLog::log (LOGINFO2, "fileRead %d", size);
  timeoutStart = currentHostCounter();
  timeoutDuration =  timeoutDefaultDuration;

  if (interruptCb (NULL))
    return -1;

  auto file = (cFile*)h;
  return file->read (buf, size);
  }
//}}}
//{{{
offset_t fileSeek (void* h, offset_t pos, int whence) {

  cLog::log (LOGINFO2, "fileSeek %d %d", pos, whence);

  timeoutStart = currentHostCounter();
  timeoutDuration = timeoutDefaultDuration;
  if (interruptCb (NULL))
    return -1;

  auto file = (cFile*)h;
  if (whence == AVSEEK_SIZE)
    return file->getLength();
  else
    return file->seek (pos, whence & ~AVSEEK_FORCE);
  }
//}}}

// cOmxReader
//{{{
cOmxReader::cOmxReader() {

  for (int i = 0; i < MAX_STREAMS; i++)
    mStreams[i].extradata = NULL;

  clearStreams();
  }
//}}}
//{{{
cOmxReader::~cOmxReader() {
  close();
  }
//}}}

// gets
//{{{
bool cOmxReader::isActive (OMXStreamType type, int stream_index) {

  if ((mAudioIndex != -1) &&
      (mStreams[mAudioIndex].id == stream_index) && (mStreams[mAudioIndex].type == type))
    return true;

  if ((mVideoIndex != -1) &&
      (mStreams[mVideoIndex].id == stream_index) && (mStreams[mVideoIndex].type == type))
    return true;

  return false;
  }
//}}}
//{{{
bool cOmxReader::canSeek() {

  if (mIoContext)
    return mIoContext->seekable;

  if (!mAvFormatContext || !mAvFormatContext->pb)
    return false;

  if (mAvFormatContext->pb->seekable == AVIO_SEEKABLE_NORMAL)
    return true;

  return false;
  }
//}}}
//{{{
string cOmxReader::getCodecName (OMXStreamType type) {

  string strStreamName;

  lock_guard<recursive_mutex> lockGuard (mMutex);

  switch (type) {
    case OMXSTREAM_AUDIO:
      if (mAudioIndex != -1)
        strStreamName = mStreams[mAudioIndex].codec_name;
      break;

    case OMXSTREAM_VIDEO:
      if (mVideoIndex != -1)
        strStreamName = mStreams[mVideoIndex].codec_name;
      break;

    default:
      break;
    }

  return strStreamName;
  }
//}}}
//{{{
string cOmxReader::getCodecName (OMXStreamType type, unsigned int index) {

  string str;
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].type == type &&  mStreams[i].index == index) {
      str = mStreams[i].codec_name;
      break;
      }
    }

  return str;
  }
//}}}
//{{{
string cOmxReader::getStreamType (OMXStreamType type, unsigned int index) {

  string str;

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].type == type &&  mStreams[i].index == index) {
      if (mStreams[i].hints.codec == AV_CODEC_ID_AC3)
        str = "AC3 ";
      else if (mStreams[i].hints.codec == AV_CODEC_ID_DTS) {
        if (mStreams[i].hints.profile == FF_PROFILE_DTS_HD_MA)
          str = "DTS-HD MA ";
        else if (mStreams[i].hints.profile == FF_PROFILE_DTS_HD_HRA)
          str = "DTS-HD HRA ";
        else
         str = "DTS ";
        }
      else if (mStreams[i].hints.codec == AV_CODEC_ID_MP2)
        str = "MP2 ";

      if (mStreams[i].hints.channels == 1)
        str += "Mono";
      else if (mStreams[i].hints.channels == 2)
        str += "Stereo";
      else if (mStreams[i].hints.channels == 6)
        str += "5.1";
      else if (mStreams[i].hints.channels != 0)
        str += dec (mStreams[i].hints.channels) + "chans";
      break;
      }
   }

  return str;
  }
//}}}
//{{{
string cOmxReader::getStreamName (OMXStreamType type, unsigned int index) {

  string str;

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].type == type &&  mStreams[i].index == index) {
      str = mStreams[i].name;
      break;
      }
    }

  return str;
  }
//}}}
//{{{
string cOmxReader::getStreamCodecName (AVStream* stream) {

  string str;

  unsigned int in = stream->codec->codec_tag;
  // FourCC codes are only valid on video streams, audio codecs in AVI/WAV
  // are 2 bytes and audio codecs in transport streams have subtle variation
  // e.g AC-3 instead of ac3
  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && in != 0) {
    char fourcc[5];
    memcpy (fourcc, &in, 4);
    fourcc[4] = 0;
    // fourccs have to be 4 characters
    if (strlen (fourcc) == 4) {
      str = fourcc;
      return str;
      }
    }

  /* use profile to determine the DTS type */
  if (stream->codec->codec_id == AV_CODEC_ID_DTS) {
    if (stream->codec->profile == FF_PROFILE_DTS_HD_MA)
      str = "dtshd_ma";
    else if (stream->codec->profile == FF_PROFILE_DTS_HD_HRA)
      str = "dtshd_hra";
    else
      str = "dca";
    return str;
    }

  auto codec = mAvCodec.avcodec_find_decoder (stream->codec->codec_id);
  if (codec)
    str = codec->name;

  return str;
  }
//}}}
//{{{
bool cOmxReader::getHints (AVStream* stream, cOmxStreamInfo* hints) {

  hints->codec = stream->codec->codec_id;
  hints->extradata = stream->codec->extradata;
  hints->extrasize = stream->codec->extradata_size;
  hints->channels = stream->codec->channels;
  hints->samplerate = stream->codec->sample_rate;
  hints->blockalign = stream->codec->block_align;
  hints->bitrate = stream->codec->bit_rate;
  hints->bitspersample = stream->codec->bits_per_coded_sample;
  if (hints->bitspersample == 0)
    hints->bitspersample = 16;

  hints->width = stream->codec->width;
  hints->height = stream->codec->height;
  hints->profile = stream->codec->profile;
  hints->orientation = 0;

  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
    hints->fpsrate = stream->r_frame_rate.num;
    hints->fpsscale = stream->r_frame_rate.den;

    if (stream->r_frame_rate.num && stream->r_frame_rate.den) {
      hints->fpsrate = stream->r_frame_rate.num;
      hints->fpsscale = stream->r_frame_rate.den;
      }
    else {
      hints->fpsscale = 0;
      hints->fpsrate = 0;
      }

    hints->aspect =
      selectAspect (stream, hints->forced_aspect) * stream->codec->width / stream->codec->height;

    auto rtag = mAvUtil.av_dict_get (stream->metadata, "rotate", NULL, 0);
    if (rtag)
      hints->orientation = atoi (rtag->value);

    mAspect = hints->aspect;
    mWidth = hints->width;
    mHeight = hints->height;
    }

  return true;
  }
//}}}
//{{{
bool cOmxReader::getHints (OMXStreamType type, unsigned int index, cOmxStreamInfo& hints) {

  for (unsigned int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].type == type && mStreams[i].index == i) {
      hints = mStreams[i].hints;
      return true;
      }
    }

  return false;
  }
//}}}
//{{{
bool cOmxReader::getHints (OMXStreamType type, cOmxStreamInfo& hints) {

  bool ret = false;

  switch (type) {
    case OMXSTREAM_AUDIO:
      if (mAudioIndex != -1) {
        hints = mStreams[mAudioIndex].hints;
        ret = true;
        }
      break;

    case OMXSTREAM_VIDEO:
      if (mVideoIndex != -1) {
        hints = mStreams[mVideoIndex].hints;
        ret = true;
        }
      break;

    default:
      break;
    }

  return ret;
  }
//}}}
//{{{
AVMediaType cOmxReader::getPacketType (cOmxPacket* packet) {

  return (!mAvFormatContext || !packet) ?
    AVMEDIA_TYPE_UNKNOWN : mAvFormatContext->streams[packet->mStreamIndex]->codec->codec_type;
  }
//}}}

// sets
//{{{
void cOmxReader::setSpeed (double speed) {

  if ((mSpeed != 0.0) && (speed == 0.0))
    mAvFormat.av_read_pause (mAvFormatContext);
  else if ((mSpeed == 0.0) && (speed != 0.0))
    mAvFormat.av_read_play (mAvFormatContext);
  mSpeed = speed;

  AVDiscard discard = AVDISCARD_NONE;
  if (mSpeed > 4.0)
    discard = AVDISCARD_NONKEY;
  else if (mSpeed > 2.0)
    discard = AVDISCARD_BIDIR;
  else if (mSpeed < 0.0)
    discard = AVDISCARD_NONKEY;

  for (auto i = 0u; i < mAvFormatContext->nb_streams; i++)
    if (mAvFormatContext->streams[i])
      if (mAvFormatContext->streams[i]->discard != AVDISCARD_ALL)
        mAvFormatContext->streams[i]->discard = discard;
  }
//}}}
//{{{
double cOmxReader::selectAspect (AVStream* st, bool& forced) {

  forced = false;

  // if stream aspect is 1:1 or 0:0 use codec aspect
  if ((st->sample_aspect_ratio.den == 1 || st->sample_aspect_ratio.den == 0) &&
      (st->sample_aspect_ratio.num == 1 || st->sample_aspect_ratio.num == 0) &&
       st->codec->sample_aspect_ratio.num != 0)
    return av_q2d (st->codec->sample_aspect_ratio);

  forced = true;
  if (st->sample_aspect_ratio.num != 0)
    return av_q2d (st->sample_aspect_ratio);

  return 0.0;
  }
//}}}
//{{{
bool cOmxReader::setActiveStream (OMXStreamType type, unsigned int index) {

  lock_guard<recursive_mutex> lockGuard (mMutex);
  return setActiveStreamInternal (type, index);
  }
//}}}

// actions
//{{{
bool cOmxReader::open (const string& filename, bool dumpFormat, bool live, float timeout,
                       const string& cookie, const string& user_agent,
                       const string& lavfdopts, const string& avdict) {

  timeoutDefaultDuration = (int64_t) (timeout * 1e9);
  mCurPts = kNoPts;
  mFilename = filename;
  mSpeed = 1.0;
  mProgram = UINT_MAX;
  AVInputFormat* iformat = NULL;

  timeoutStart = currentHostCounter();
  timeoutDuration = 3 * timeoutDefaultDuration;
  clearStreams();

  mAvFormat.av_register_all();
  mAvFormat.avformat_network_init();
  mAvUtil.av_log_set_level (dumpFormat ? AV_LOG_INFO : AV_LOG_QUIET);

  unsigned char* buffer = NULL;
  unsigned int flags = READ_TRUNCATED | READ_BITRATE | READ_CHUNKED;

  mAvFormatContext = mAvFormat.avformat_alloc_context();
  int result = mAvFormat.av_set_options_string (mAvFormatContext, lavfdopts.c_str(), ":", ",");
  if (result < 0) {
    //{{{  options error return
    cLog::log (LOGERROR, "cOmxReader::Open not valid lavfdopts %s ", lavfdopts.c_str());
    close();
    return false;
    }
    //}}}

  AVDictionary* d = NULL;
  result = mAvUtil.av_dict_parse_string (&d, avdict.c_str(), ":", ",", 0);
  //{{{  dict error return
  if (result < 0) {
    cLog::log (LOGERROR, "cOmxReader::Open invalid avdict %s ", avdict.c_str());
    close();
    return false;
    }
  //}}}

  const AVIOInterruptCB intCb = { interruptCb, NULL };
  mAvFormatContext->interrupt_callback = intCb;
  mAvFormatContext->flags |= AVFMT_FLAG_NONBLOCK;

  if (mFilename.substr (0,7) == "http://" ||
      mFilename.substr (0,8) == "https://" ||
      mFilename.substr (0,7) == "rtmp://" ||
      mFilename.substr (0,7) == "rtsp://") {
    //{{{  non file input
    // ffmpeg dislikes the useragent from AirPlay urls
    //int idx = m_filename.Find("|User-Agent=AppleCoreMedia");
    size_t idx = mFilename.find ("|");
    if (idx != string::npos)
      mFilename = mFilename.substr (0, idx);

    // Enable seeking if http, ftp
    if (mFilename.substr(0,7) == "http://") {
      if (!live)
        av_dict_set(&d, "seekable", "1", 0);
      if (!cookie.empty())
        av_dict_set(&d, "cookies", cookie.c_str(), 0);
      if (!user_agent.empty())
        av_dict_set(&d, "user_agent", user_agent.c_str(), 0);
      }
    cLog::log (LOGINFO1, "cOmxReader::Open avformat_open_input " + mFilename);

    result = mAvFormat.avformat_open_input (&mAvFormatContext, mFilename.c_str(), iformat, &d);
    if (av_dict_count(d) == 0) {
      cLog::log (LOGINFO1, "cOmxReader::Open avformat_open_input enabled SEEKING");
      if (mFilename.substr(0,7) == "http://")
        mAvFormatContext->pb->seekable = AVIO_SEEKABLE_NORMAL;
      }

    av_dict_free (&d);
    if (result < 0) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open avformat_open_input " + mFilename);
      close();
      return false;
      }
      //}}}
    }
    //}}}
  else {
    //{{{  file input
    mFile = new cFile();
    if (!mFile->open (mFilename, flags)) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open " + mFilename);
      close();
      return false;
      }
      //}}}

    buffer = (unsigned char*)mAvUtil.av_malloc (FFMPEG_FILE_BUFFER_SIZE);
    mIoContext = mAvFormat.avio_alloc_context (
      buffer, FFMPEG_FILE_BUFFER_SIZE, 0, mFile, fileRead, NULL, fileSeek);
    mIoContext->max_packet_size = 6144;

    if (mIoContext->max_packet_size)
      mIoContext->max_packet_size *= FFMPEG_FILE_BUFFER_SIZE / mIoContext->max_packet_size;

    if (mFile->ioControl (IOCTRL_SEEK_POSSIBLE, NULL) == 0)
      mIoContext->seekable = 0;

    mAvFormat.av_probe_input_buffer (mIoContext, &iformat, mFilename.c_str(), NULL, 0, 0);
    if (!iformat) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open av_probe_input_buffer" + mFilename);
      close();
      return false;
      }
      //}}}

    mAvFormatContext->pb = mIoContext;
    result = mAvFormat.avformat_open_input (&mAvFormatContext, mFilename.c_str(), iformat, &d);
    av_dict_free (&d);
    if (result < 0) {
      close();
      return false;
      }
    }
    //}}}

  mAvFormatContext->max_analyze_duration = 10000000;
  if (live)
    mAvFormatContext->flags |= AVFMT_FLAG_NOBUFFER;

  if ((mAvFormat.avformat_find_stream_info (mAvFormatContext, NULL) < 0) || !getStreams()) {
    //{{{  no streams, exit, return
    close();
    return false;
    }
    //}}}
  cLog::log (LOGNOTICE, "cOmxReader::Open streams a:%d v:%d",
                        getAudioStreamCount(), getVideoStreamCount());

  mSpeed = 1.0;
  if (dumpFormat)
    mAvFormat.av_dump_format (mAvFormatContext, 0, mFilename.c_str(), 0);

  updateCurrentPTS();
  return true;
  }
//}}}
//{{{
cOmxPacket* cOmxReader::readPacket() {

  if (mEof)
    return nullptr;

  lock_guard<recursive_mutex> lockGuard (mMutex);

  // assume we are not eof
  if (mAvFormatContext->pb)
    mAvFormatContext->pb->eof_reached = 0;

  // keep track if ffmpeg doesn't always set these
  AVPacket avPacket;
  avPacket.size = 0;
  avPacket.data = NULL;
  avPacket.stream_index = MAX_OMX_STREAMS;

  timeoutStart = currentHostCounter();
  timeoutDuration =  timeoutDefaultDuration;
  if (mAvFormat.av_read_frame (mAvFormatContext, &avPacket) < 0) {
    //{{{  error return
    mEof = true;
    return NULL;
    }
    //}}}
  if (avPacket.size < 0 || avPacket.stream_index >= MAX_OMX_STREAMS || interruptCb(NULL)) {
    //{{{  ffmpeg can return neg packet size, eof return
    if (mAvFormatContext->pb && !mAvFormatContext->pb->eof_reached)
      cLog::log (LOGERROR, "cOmxReader::Read no valid packet");
    mAvCodec.av_free_packet (&avPacket);
    mEof = true;
    return NULL;
    }
    //}}}

  auto stream = mAvFormatContext->streams[avPacket.stream_index];

  // allocate cOmxPacket
  auto packet = new cOmxPacket (avPacket.size, FF_INPUT_BUFFER_PADDING_SIZE);
  packet->mCodecType = stream->codec->codec_type;
  memcpy (packet->mData, avPacket.data, packet->mSize);
  packet->mStreamIndex = avPacket.stream_index;
  getHints (stream, &packet->mHints);
  packet->mDts = convertTimestamp (avPacket.dts, stream->time_base.den, stream->time_base.num);
  packet->mPts = convertTimestamp (avPacket.pts, stream->time_base.den, stream->time_base.num);
  packet->mDuration = ((double)avPacket.duration * stream->time_base.num / stream->time_base.den) * kPtsScale;

  // used to guess streamlength
  if ((packet->mDts != kNoPts) &&
      (packet->mDts > mCurPts || mCurPts == kNoPts))
    mCurPts = packet->mDts;

  // check if stream has passed full duration, needed for live streams
  if (avPacket.dts != (int64_t)AV_NOPTS_VALUE) {
    int64_t duration = avPacket.dts;
    if (stream->start_time != (int64_t)AV_NOPTS_VALUE)
      duration -= stream->start_time;
    if (duration > stream->duration) {
      stream->duration = duration;
      duration = mAvUtil.av_rescale_rnd (
        stream->duration, (int64_t)stream->time_base.num * AV_TIME_BASE, stream->time_base.den, AV_ROUND_NEAR_INF);
      if ((mAvFormatContext->duration == (int64_t)AV_NOPTS_VALUE) ||
          (mAvFormatContext->duration != (int64_t)AV_NOPTS_VALUE && duration > mAvFormatContext->duration))
        mAvFormatContext->duration = duration;
      }
    }
  mAvCodec.av_free_packet (&avPacket);

  return packet;
  }
//}}}
//{{{
bool cOmxReader::seek (float time, double& startPts) {

  // secs to ms
  time *= 1000;

  bool backwards = (time < 0);
  if (backwards)
    time = -time;

  if (mFile && !mFile->ioControl (IOCTRL_SEEK_POSSIBLE, NULL)) {
    cLog::log (LOGERROR, "cOmxReader::seek - not seekable");
    return false;
    }

  lock_guard<recursive_mutex> lockGuard (mMutex);

  if (mIoContext)
    mIoContext->buf_ptr = mIoContext->buf_end;

  auto seekPts = (int64_t)time * (AV_TIME_BASE / 1000);
  if (mAvFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    seekPts += mAvFormatContext->start_time;

  timeoutStart = currentHostCounter();
  timeoutDuration = timeoutDefaultDuration;
  auto ret = mAvFormat.av_seek_frame (mAvFormatContext, -1, seekPts, backwards ? AVSEEK_FLAG_BACKWARD : 0);
  if (ret >= 0)
    updateCurrentPTS();

  // in this case the start time is requested time
  if (startPts)
    startPts = time * kPtsScale / 1000.0;

  // demuxer will return failure, if you seek to eof
  mEof = false;
  if (ret < 0) {
    mEof = true;
    ret = 0;
    }

  cLog::log (LOGINFO1, "cOmxReader::seek " + frac(time,4,2,' ') +
                       " went to " + frac (mCurPts / kPtsScale, 6, 2, ' '));
  return ret >= 0;
  }
//}}}
//{{{
void cOmxReader::updateCurrentPTS() {

  mCurPts = kNoPts;

  for (unsigned int i = 0; i < mAvFormatContext->nb_streams; i++) {
    auto stream = mAvFormatContext->streams[i];
    if (stream && stream->cur_dts != (int64_t)AV_NOPTS_VALUE) {
      double ts = convertTimestamp (stream->cur_dts, stream->time_base.den, stream->time_base.num);
      if (mCurPts == kNoPts || mCurPts > ts )
        mCurPts = ts;
      }
    }
  }
//}}}
//{{{
void cOmxReader::clearStreams() {

  mAudioIndex = -1;
  mVideoIndex = -1;

  mAudioCount = 0;
  mVideoCount = 0;

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].extradata)
      free (mStreams[i].extradata);

    memset (mStreams[i].language, 0, sizeof(mStreams[i].language));
    mStreams[i].codec_name = "";
    mStreams[i].name = "";
    mStreams[i].type = OMXSTREAM_NONE;
    mStreams[i].stream = NULL;
    mStreams[i].extradata = NULL;
    mStreams[i].extrasize = 0;
    mStreams[i].index = 0;
    mStreams[i].id = 0;
    }

  mProgram = UINT_MAX;
  }
//}}}
//{{{
bool cOmxReader::close() {

  if (mAvFormatContext) {
    if (mIoContext && mAvFormatContext->pb && mAvFormatContext->pb != mIoContext) {
      cLog::log(LOGINFO1, "cOmxReader::Close - demuxer changed byteContext, possible memleak");
      mIoContext = mAvFormatContext->pb;
      }
    mAvFormat.avformat_close_input (&mAvFormatContext);
    }

  if (mIoContext) {
    mAvUtil.av_free (mIoContext->buffer);
    mAvUtil.av_free (mIoContext);
    }

  mIoContext = NULL;
  mAvFormatContext = NULL;

  delete mFile;
  mFile = NULL;

  mAvFormat.avformat_network_deinit();

  mFilename = "";
  mVideoCount = 0;
  mAudioCount = 0;
  mAudioIndex = -1;
  mVideoIndex = -1;
  mEof = false;
  mCurPts = kNoPts;
  mSpeed = 1.0;

  clearStreams();
  return true;
  }
//}}}

// private
//{{{
bool cOmxReader::getStreams() {

  unsigned int program = UINT_MAX;

  clearStreams();
  if (mAvFormatContext->nb_programs) {
    // look for first non empty stream and discard nonselected programs
    for (unsigned int i = 0; i < mAvFormatContext->nb_programs; i++) {
      if (program == UINT_MAX && mAvFormatContext->programs[i]->nb_stream_indexes > 0)
        program = i;
      if (i != program)
        mAvFormatContext->programs[i]->discard = AVDISCARD_ALL;
      }
    if (program != UINT_MAX)
      // add streams from selected program
      for (unsigned int i = 0; i < mAvFormatContext->programs[program]->nb_stream_indexes; i++)
        addStream (mAvFormatContext->programs[program]->stream_index[i]);
    }

  // if there were no programs or they were all empty, add all streams
  if (program == UINT_MAX)
    for (unsigned int i = 0; i < mAvFormatContext->nb_streams; i++)
      addStream(i);

  if (mVideoCount)
    setActiveStreamInternal (OMXSTREAM_VIDEO, 0);
  if (mAudioCount)
    setActiveStreamInternal (OMXSTREAM_AUDIO, 0);

  return true;
  }
//}}}
//{{{
void cOmxReader::addStream (int id) {

  if (id > MAX_STREAMS)
    return;

  // discard if it's a picture attachment (e.g. album art embedded in MP3 or AAC)
  auto pStream = mAvFormatContext->streams[id];
  if (pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO && (pStream->disposition & AV_DISPOSITION_ATTACHED_PIC))
    return;

  switch (pStream->codec->codec_type) {
    //{{{
    case AVMEDIA_TYPE_AUDIO:
      mStreams[id].stream      = pStream;
      mStreams[id].type        = OMXSTREAM_AUDIO;
      mStreams[id].index       = mAudioCount;
      mStreams[id].codec_name  = getStreamCodecName(pStream);
      mStreams[id].id          = id;
      mAudioCount++;
      getHints(pStream, &mStreams[id].hints);
      break;
    //}}}
    //{{{
    case AVMEDIA_TYPE_VIDEO:
      mStreams[id].stream      = pStream;
      mStreams[id].type        = OMXSTREAM_VIDEO;
      mStreams[id].index       = mVideoCount;
      mStreams[id].codec_name  = getStreamCodecName(pStream);
      mStreams[id].id          = id;
      mVideoCount++;
      getHints(pStream, &mStreams[id].hints);
      break;
    //}}}
    //{{{
    default:
      return;
    //}}}
    }

  auto langTag = mAvUtil.av_dict_get (pStream->metadata, "language", NULL, 0);
  if (langTag)
    strncpy (mStreams[id].language, langTag->value, 3);

  auto titleTag = mAvUtil.av_dict_get (pStream->metadata,"title", NULL, 0);
  if (titleTag)
    mStreams[id].name = titleTag->value;

  if (pStream->codec->extradata && pStream->codec->extradata_size > 0) {
    mStreams[id].extrasize = pStream->codec->extradata_size;
    mStreams[id].extradata = malloc (pStream->codec->extradata_size);
    memcpy (mStreams[id].extradata, pStream->codec->extradata, pStream->codec->extradata_size);
    }
  }
//}}}

//{{{
double cOmxReader::convertTimestamp (int64_t pts, int den, int num) {

  if (pts == (int64_t)AV_NOPTS_VALUE)
    return kNoPts;

  // do calculations in floats as they can easily overflow otherwise
  // we don't care for having a completly exact timestamp anyway
  double starttime = 0.0;
  if (mAvFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    starttime = (double)mAvFormatContext->start_time / AV_TIME_BASE;

  double timestamp = (double)pts * num  / den;
  if (timestamp > starttime)
    timestamp -= starttime;
  else if (timestamp + 0.1f > starttime )
    timestamp = 0;

  return timestamp * kPtsScale;
  }
//}}}
//{{{
bool cOmxReader::setActiveStreamInternal (OMXStreamType type, unsigned int index) {

  bool ret = false;
  switch (type) {
    //{{{
    case OMXSTREAM_AUDIO:
      if((int)index > (mAudioCount - 1))
        index = (mAudioCount - 1);
      break;
    //}}}
    //{{{
    case OMXSTREAM_VIDEO:
      if((int)index > (mVideoCount - 1))
        index = (mVideoCount - 1);
      break;
    //}}}
    //{{{
    default:
      break;
    //}}}
    }

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (mStreams[i].type == type &&  mStreams[i].index == index) {
      switch (mStreams[i].type) {
        //{{{
        case OMXSTREAM_AUDIO:
          mAudioIndex = i;
          ret = true;
          break;
        //}}}
        //{{{
        case OMXSTREAM_VIDEO:
          mVideoIndex = i;
          ret = true;
          break;
        //}}}
        //{{{
        default:
          break;
        //}}}
        }
      }
    }

  if (!ret) {
    switch (type) {
      //{{{
      case OMXSTREAM_AUDIO:
        mAudioIndex = -1;
        break;
      //}}}
      //{{{
      case OMXSTREAM_VIDEO:
        mVideoIndex = -1;
        break;
      //}}}
      //{{{
      default:
        break;
      //}}}
      }
    }

  return ret;
  }
//}}}
