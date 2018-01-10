// cOmxReader.cpp
//{{{  includes
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <string>

#include "cOmxReader.h"

#include "platformDefs.h"
#include "../shared/utils/cLog.h"

#include "cOmxClock.h"

using namespace std;
//}}}
//{{{  defines
#define MAX_DATA_SIZE_VIDEO    8 * 1024 * 1024
#define MAX_DATA_SIZE_AUDIO    2 * 1024 * 1024
#define MAX_DATA_SIZE          10 * 1024 * 1024

#define FFMPEG_FILE_BUFFER_SIZE   32768

// can handle truncated reads where function returns before entire buffer has been filled
#define READ_TRUNCATED 0x01

// support read in the minimum defined chunk size, this disables internal cache then
#define READ_CHUNKED   0x02

// use cache to access this file
#define READ_CACHED     0x04

// open without caching. regardless to file type
#define READ_NO_CACHE  0x08

// calcuate bitrate for file while reading
#define READ_BITRATE   0x10

#define RESET_TIMEOUT(x) do { \
  timeout_start = currentHostCounter(); \
  timeout_duration = (x) * timeout_default_duration; \
  } while (0)
//}}}
//{{{  static vars
static bool g_abort = false;
static int64_t timeout_start;
static int64_t timeout_default_duration;
static int64_t timeout_duration;
//}}}
//{{{
typedef enum {
  IOCTRL_NATIVE        = 1, /**< SNativeIoControl structure, containing what should be passed to native ioctrl */
  IOCTRL_SEEK_POSSIBLE = 2, /**< return 0 if known not to work, 1 if it should work */
  IOCTRL_CACHE_STATUS  = 3, /**< SCacheStatus structure */
  IOCTRL_CACHE_SETRATE = 4, /**< unsigned int with with speed limit for caching in bytes per second */
  } EIoControl;
//}}}
//{{{
class cFile {
public:
  //{{{
  cFile()
  {
    mFile = NULL;
    m_flags = 0;
    m_iLength = 0;
    m_bPipe = false;
  }
  //}}}
  //{{{
  ~cFile() {
    if (mFile && !m_bPipe)
      fclose (mFile);
    }
  //}}}

  //{{{
  bool Open (const string& strFileName, unsigned int flags) {

    m_flags = flags;

    if (strFileName.compare(0, 5, "pipe:") == 0) {
      m_bPipe = true;
      mFile = stdin;
      m_iLength = 0;
      return true;
      }

    mFile = fopen64 (strFileName.c_str(), "r");
    if (!mFile)
      return false;

    fseeko64 (mFile, 0, SEEK_END);
    m_iLength = ftello64 (mFile);
    fseeko64 (mFile, 0, SEEK_SET);

    return true;
    }
  //}}}
  //{{{
  unsigned int Read (void *lpBuf, int64_t uiBufSize)
  {
    unsigned int ret = 0;

    if(!mFile)
      return 0;

    ret = fread(lpBuf, 1, uiBufSize, mFile);

    return ret;
  }
  //}}}
  //{{{
  int IoControl (EIoControl request, void* param)
  {
    if(request == IOCTRL_SEEK_POSSIBLE && mFile)
    {
      if (m_bPipe)
        return false;

      struct stat st;
      if (fstat(fileno(mFile), &st) == 0)
      {
        return !S_ISFIFO(st.st_mode);
      }
    }

    return -1;
  }
  //}}}
  //{{{
  int64_t Seek (int64_t iFilePosition, int iWhence)
  {
    if (!mFile)
      return -1;

    return fseeko64(mFile, iFilePosition, iWhence);;
  }
  //}}}
  //{{{
  void Close()
  {
    if(mFile && !m_bPipe)
      fclose(mFile);
    mFile = NULL;
  }
  //}}}

  //{{{
  bool Exists (const string& strFileName, bool bUseCache = true)
  {
    FILE *fp;

    if (strFileName.compare(0, 5, "pipe:") == 0)
      return true;

    fp = fopen64(strFileName.c_str(), "r");

    if(!fp)
      return false;

    fclose(fp);

    return true;
  }
  //}}}
  //{{{
  int64_t GetLength()
  {
    return m_iLength;
  }
  //}}}
  //{{{
  int64_t GetPosition()
  {
    if (!mFile)
      return -1;

    return ftello64(mFile);
  }
  //}}}
  //{{{
  int GetChunkSize() {
    return 6144 /*FFMPEG_FILE_BUFFER_SIZE*/;
    };
  //}}}
  //{{{
  bool IsEOF()
  {
    if (!mFile)
      return -1;

    if (m_bPipe)
      return false;

    return feof(mFile) != 0;
  }
  //}}}

private:
  unsigned int m_flags;
  FILE* mFile;
  int64_t m_iLength;
  bool m_bPipe;
  };
//}}}

// local
//{{{
int64_t currentHostCounter() {
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (((int64_t)now.tv_sec * 1000000000LL) + now.tv_nsec);
  }
//}}}
//{{{
int interrupt_cb (void *unused) {

  int ret = 0;
  if (g_abort) {
    cLog::log (LOGERROR, "cOmxReader::interrupt_cb - abort");
    ret = 1;
    }
  else if (timeout_duration && currentHostCounter() - timeout_start > timeout_duration) {
    cLog::log (LOGERROR, "cOmxReader::interrupt_cb - Timed out");
    ret = 1;
    }

  return ret;
  }
//}}}
//{{{
int fileRead (void* h, uint8_t* buf, int size) {

  RESET_TIMEOUT(1);
  if (interrupt_cb(NULL))
    return -1;

  auto file = (cFile*)h;
  return file->Read (buf, size);
  }
//}}}
//{{{
offset_t fileSeek (void* h, offset_t pos, int whence) {

  RESET_TIMEOUT(1);
  if (interrupt_cb (NULL))
    return -1;

  auto file = (cFile*)h;
  if (whence == AVSEEK_SIZE)
    return file->GetLength();
  else
    return file->Seek (pos, whence & ~AVSEEK_FORCE);
  }
//}}}

// static
//{{{
void cOmxReader::freePacket (OMXPacket*& packet) {

  if (packet)
    free (packet->data);

  free (packet);
  packet = nullptr;
  }
//}}}
//{{{
double cOmxReader::normalizeFrameDuration (double frameduration) {

  //if the duration is within 20 microseconds of a common duration, use that
  const double durations[] = { DVD_TIME_BASE * 1.001 / 24.0, DVD_TIME_BASE / 24.0, DVD_TIME_BASE / 25.0,
                               DVD_TIME_BASE * 1.001 / 30.0, DVD_TIME_BASE / 30.0, DVD_TIME_BASE / 50.0,
                               DVD_TIME_BASE * 1.001 / 60.0, DVD_TIME_BASE / 60.0};

  double lowestdiff = DVD_TIME_BASE;
  int selected = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
    double diff = fabs (frameduration - durations[i]);
    if (diff < DVD_MSEC_TO_TIME(0.02) && diff < lowestdiff) {
      selected = i;
      lowestdiff = diff;
      }
    }

  if (selected != -1)
    return durations[selected];
  else
    return frameduration;
  }
//}}}

// cOmxReader
//{{{
cOmxReader::cOmxReader() {

  m_filename = "";
  g_abort = false;
  mFile = NULL;
  m_ioContext = NULL;
  mAvFormatContext = NULL;
  m_eof = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;

  for (int i = 0; i < MAX_STREAMS; i++)
    m_streams[i].extradata = NULL;

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
bool cOmxReader::isEof() {
  return m_eof;
  }
//}}}
//{{{
bool cOmxReader::isActive (int stream_index) {

  if ((m_audio_index != -1) && m_streams[m_audio_index].id == stream_index)
    return true;
  if ((m_video_index != -1) && m_streams[m_video_index].id == stream_index)
    return true;

  return false;
  }
//}}}
//{{{
bool cOmxReader::isActive (OMXStreamType type, int stream_index) {

  if ((m_audio_index != -1) &&
      m_streams[m_audio_index].id == stream_index && m_streams[m_audio_index].type == type)
    return true;

  if ((m_video_index != -1) &&
      m_streams[m_video_index].id == stream_index && m_streams[m_video_index].type == type)
    return true;

  return false;
  }
//}}}
//{{{
bool cOmxReader::canSeek() {

  if (m_ioContext)
    return m_ioContext->seekable;
  if (!mAvFormatContext || !mAvFormatContext->pb)
    return false;
  if (mAvFormatContext->pb->seekable == AVIO_SEEKABLE_NORMAL)
    return true;
  return false;
  }
//}}}
//{{{
int cOmxReader::getStreamLength() {
  return mAvFormatContext ? (int)(mAvFormatContext->duration / (AV_TIME_BASE / 1000)) : 0;
  }
//}}}
//{{{
string cOmxReader::getCodecName (OMXStreamType type) {

  string strStreamName;

  cSingleLock lock (m_critSection);

  switch (type) {
    case OMXSTREAM_AUDIO:
      if (m_audio_index != -1)
        strStreamName = m_streams[m_audio_index].codec_name;
      break;

    case OMXSTREAM_VIDEO:
      if (m_video_index != -1)
        strStreamName = m_streams[m_video_index].codec_name;
      break;

    default:
      break;
    }

  return strStreamName;
  }
//}}}
//{{{
string cOmxReader::getCodecName (OMXStreamType type, unsigned int index) {

  string strStreamName;
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].type == type &&  m_streams[i].index == index) {
      strStreamName = m_streams[i].codec_name;
      break;
      }
    }

  return strStreamName;
  }
//}}}
//{{{
string cOmxReader::getStreamCodecName (AVStream* stream) {

  string strStreamName;
  if (!stream)
    return strStreamName;

  unsigned int in = stream->codec->codec_tag;
  // FourCC codes are only valid on video streams, audio codecs in AVI/WAV
  // are 2 bytes and audio codecs in transport streams have subtle variation
  // e.g AC-3 instead of ac3
  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && in != 0) {
    char fourcc[5];
    memcpy(fourcc, &in, 4);
    fourcc[4] = 0;
    // fourccs have to be 4 characters
    if (strlen(fourcc) == 4) {
      strStreamName = fourcc;
      return strStreamName;
      }
    }

  /* use profile to determine the DTS type */
  if (stream->codec->codec_id == AV_CODEC_ID_DTS) {
    if (stream->codec->profile == FF_PROFILE_DTS_HD_MA)
      strStreamName = "dtshd_ma";
    else if (stream->codec->profile == FF_PROFILE_DTS_HD_HRA)
      strStreamName = "dtshd_hra";
    else
      strStreamName = "dca";
    return strStreamName;
    }

  AVCodec* codec = mAvCodec.avcodec_find_decoder(stream->codec->codec_id);

  if (codec)
    strStreamName = codec->name;

  return strStreamName;
  }
//}}}
//{{{
string cOmxReader::getStreamLanguage (OMXStreamType type, unsigned int index) {

  string language;
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].type == type &&  m_streams[i].index == index) {
      language = m_streams[i].language;
      break;
      }
    }

  return language;
  }
//}}}
//{{{
string cOmxReader::getStreamName (OMXStreamType type, unsigned int index) {

  string name;
  for (int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].type == type &&  m_streams[i].index == index) {
      name = m_streams[i].name;
      break;
      }
    }

  return name;
  }
//}}}
//{{{
string cOmxReader::getStreamType (OMXStreamType type, unsigned int index) {

  string strInfo;

  char sInfo[64];
  for (int i = 0; i < MAX_STREAMS; i++) {
    if(m_streams[i].type == type &&  m_streams[i].index == index) {
      if (m_streams[i].hints.codec == AV_CODEC_ID_AC3)
        strcpy (sInfo, "AC3 ");
      else if (m_streams[i].hints.codec == AV_CODEC_ID_DTS) {
        if (m_streams[i].hints.profile == FF_PROFILE_DTS_HD_MA)
          strcpy (sInfo, "DTS-HD MA ");
        else if (m_streams[i].hints.profile == FF_PROFILE_DTS_HD_HRA)
          strcpy (sInfo, "DTS-HD HRA ");
        else
          strcpy (sInfo, "DTS ");
        }
      else if (m_streams[i].hints.codec == AV_CODEC_ID_MP2)
        strcpy (sInfo, "MP2 ");
      else
        strcpy (sInfo, "");

      if (m_streams[i].hints.channels == 1)
        strcat (sInfo, "Mono");
      else if (m_streams[i].hints.channels == 2)
        strcat (sInfo, "Stereo");
      else if (m_streams[i].hints.channels == 6)
        strcat (sInfo, "5.1");
      else if (m_streams[i].hints.channels != 0) {
        char temp[32];
        sprintf (temp, " %d %s", m_streams[i].hints.channels, "Channels");
        strcat (sInfo, temp);
        }
      break;
      }
   }

  strInfo = sInfo;
  return strInfo;
  }
//}}}

//{{{
bool cOmxReader::getHints (AVStream* stream, cOmxStreamInfo* hints) {

  if (!hints || !stream)
    return false;

  hints->codec         = stream->codec->codec_id;
  hints->extradata     = stream->codec->extradata;
  hints->extrasize     = stream->codec->extradata_size;
  hints->channels      = stream->codec->channels;
  hints->samplerate    = stream->codec->sample_rate;
  hints->blockalign    = stream->codec->block_align;
  hints->bitrate       = stream->codec->bit_rate;
  hints->bitspersample = stream->codec->bits_per_coded_sample;
  if (hints->bitspersample == 0)
    hints->bitspersample = 16;

  hints->width         = stream->codec->width;
  hints->height        = stream->codec->height;
  hints->profile       = stream->codec->profile;
  hints->orientation   = 0;

  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
    hints->fpsrate       = stream->r_frame_rate.num;
    hints->fpsscale      = stream->r_frame_rate.den;

    if (stream->r_frame_rate.num && stream->r_frame_rate.den) {
      hints->fpsrate      = stream->r_frame_rate.num;
      hints->fpsscale     = stream->r_frame_rate.den;
      }
    else {
      hints->fpsscale     = 0;
      hints->fpsrate      = 0;
      }

    hints->aspect = selectAspect (stream, hints->forced_aspect) * stream->codec->width / stream->codec->height;

    auto rtag = mAvUtil.av_dict_get (stream->metadata, "rotate", NULL, 0);
    if (rtag)
      hints->orientation = atoi (rtag->value);
    m_aspect = hints->aspect;
    m_width = hints->width;
    m_height = hints->height;
    }

  return true;
  }
//}}}
//{{{
bool cOmxReader::getHints (OMXStreamType type, unsigned int index, cOmxStreamInfo& hints) {

  for (unsigned int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].type == type && m_streams[i].index == i) {
      hints = m_streams[i].hints;
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
      if (m_audio_index != -1) {
        ret = true;
        hints = m_streams[m_audio_index].hints;
        }
      break;

    case OMXSTREAM_VIDEO:
      if (m_video_index != -1) {
        ret = true;
        hints = m_streams[m_video_index].hints;
        }
      break;

    default:
      break;
    }

  return ret;
  }
//}}}
//{{{
AVMediaType cOmxReader::getPacketType (OMXPacket* packet) {
  return (!mAvFormatContext || !packet) ?
    AVMEDIA_TYPE_UNKNOWN : mAvFormatContext->streams[packet->stream_index]->codec->codec_type;
  }
//}}}

// sets
//{{{
bool cOmxReader::setActiveStream (OMXStreamType type, unsigned int index) {

  cSingleLock lock (m_critSection);
  return setActiveStreamInternal (type, index);
  }
//}}}
//{{{
void cOmxReader::setSpeed (int iSpeed) {

  if (!mAvFormatContext)
    return;

  if (m_speed != DVD_PLAYSPEED_PAUSE && iSpeed == DVD_PLAYSPEED_PAUSE)
    mAvFormat.av_read_pause (mAvFormatContext);
  else if (m_speed == DVD_PLAYSPEED_PAUSE && iSpeed != DVD_PLAYSPEED_PAUSE)
    mAvFormat.av_read_play (mAvFormatContext);
  m_speed = iSpeed;

  AVDiscard discard = AVDISCARD_NONE;
  if (m_speed > 4*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_NONKEY;
  else if (m_speed > 2*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_BIDIR;
  else if (m_speed < DVD_PLAYSPEED_PAUSE)
    discard = AVDISCARD_NONKEY;

  for (unsigned int i = 0; i < mAvFormatContext->nb_streams; i++)
    if (mAvFormatContext->streams[i])
      if (mAvFormatContext->streams[i]->discard != AVDISCARD_ALL)
        mAvFormatContext->streams[i]->discard = discard;
  }
//}}}
//{{{
double cOmxReader::selectAspect (AVStream* st, bool& forced) {

  forced = false;

  /* if stream aspect is 1:1 or 0:0 use codec aspect */
  if ((st->sample_aspect_ratio.den == 1 || st->sample_aspect_ratio.den == 0) &&
      (st->sample_aspect_ratio.num == 1 || st->sample_aspect_ratio.num == 0) &&
       st->codec->sample_aspect_ratio.num != 0)
    return av_q2d(st->codec->sample_aspect_ratio);

  forced = true;
  if (st->sample_aspect_ratio.num != 0)
    return av_q2d(st->sample_aspect_ratio);

  return 0.0;
  }
//}}}

// actions
//{{{
bool cOmxReader::open (string filename, bool dump_format, bool live, float timeout,
                       string cookie, string user_agent, string lavfdopts, string avdict) {

  timeout_default_duration = (int64_t) (timeout * 1e9);
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_filename = filename;
  m_speed = DVD_PLAYSPEED_NORMAL;
  m_program = UINT_MAX;
  AVInputFormat* iformat = NULL;

  RESET_TIMEOUT(3);
  clearStreams();

  mAvFormat.av_register_all();
  mAvFormat.avformat_network_init();
  mAvUtil.av_log_set_level (dump_format ? AV_LOG_INFO : AV_LOG_QUIET);

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

  const AVIOInterruptCB int_cb = { interrupt_cb, NULL };
  mAvFormatContext->interrupt_callback = int_cb;
  mAvFormatContext->flags |= AVFMT_FLAG_NONBLOCK;

  if (m_filename.substr (0,7) == "http://" ||
      m_filename.substr (0,8) == "https://" ||
      m_filename.substr (0,7) == "rtmp://" ||
      m_filename.substr (0,7) == "rtsp://") {
    //{{{  non file input
    // ffmpeg dislikes the useragent from AirPlay urls
    //int idx = m_filename.Find("|User-Agent=AppleCoreMedia");
    size_t idx = m_filename.find ("|");
    if (idx != string::npos)
      m_filename = m_filename.substr(0, idx);

    // Enable seeking if http, ftp
    if (m_filename.substr(0,7) == "http://") {
      if (!live)
        av_dict_set(&d, "seekable", "1", 0);
      if (!cookie.empty())
        av_dict_set(&d, "cookies", cookie.c_str(), 0);
      if (!user_agent.empty())
        av_dict_set(&d, "user_agent", user_agent.c_str(), 0);
      }
    cLog::log (LOGINFO1, "cOmxReader::Open avformat_open_input %s", m_filename.c_str());

    result = mAvFormat.avformat_open_input (&mAvFormatContext, m_filename.c_str(), iformat, &d);
    if (av_dict_count(d) == 0) {
      cLog::log (LOGINFO1, "cOmxReader::Open avformat_open_input enabled SEEKING");
      if (m_filename.substr(0,7) == "http://")
        mAvFormatContext->pb->seekable = AVIO_SEEKABLE_NORMAL;
      }

    av_dict_free (&d);
    if (result < 0) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open avformat_open_input%s ", m_filename.c_str());
      close();
      return false;
      }
      //}}}
    }
    //}}}
  else {
    //{{{  file input
    mFile = new cFile();
    if (!mFile->Open (m_filename, flags)) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open %s ", m_filename.c_str());
      close();
      return false;
      }
      //}}}

    buffer = (unsigned char*)mAvUtil.av_malloc (FFMPEG_FILE_BUFFER_SIZE);
    m_ioContext = mAvFormat.avio_alloc_context (
      buffer, FFMPEG_FILE_BUFFER_SIZE, 0, mFile, fileRead, NULL, fileSeek);
    m_ioContext->max_packet_size = 6144;

    if (m_ioContext->max_packet_size)
      m_ioContext->max_packet_size *= FFMPEG_FILE_BUFFER_SIZE / m_ioContext->max_packet_size;

    if (mFile->IoControl (IOCTRL_SEEK_POSSIBLE, NULL) == 0)
      m_ioContext->seekable = 0;

    mAvFormat.av_probe_input_buffer (m_ioContext, &iformat, m_filename.c_str(), NULL, 0, 0);
    if (!iformat) {
      //{{{  error, return
      cLog::log (LOGERROR, "cOmxReader::Open av_probe_input_buffer %s ", m_filename.c_str());
      close();
      return false;
      }
      //}}}

    mAvFormatContext->pb = m_ioContext;
    result = mAvFormat.avformat_open_input (&mAvFormatContext, m_filename.c_str(), iformat, &d);
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

  m_speed = DVD_PLAYSPEED_NORMAL;
  if (dump_format)
    mAvFormat.av_dump_format (mAvFormatContext, 0, m_filename.c_str(), 0);

  updateCurrentPTS();
  return true;
  }
//}}}
//{{{
OMXPacket* cOmxReader::readPacket() {

  if (!mAvFormatContext || m_eof)
    return NULL;

  cSingleLock lock (m_critSection);

  // assume we are not eof
  if (mAvFormatContext->pb)
    mAvFormatContext->pb->eof_reached = 0;

  // keep track if ffmpeg doesn't always set these
  AVPacket avPacket;
  avPacket.size = 0;
  avPacket.data = NULL;
  avPacket.stream_index = MAX_OMX_STREAMS;

  RESET_TIMEOUT(1);
  if (mAvFormat.av_read_frame (mAvFormatContext, &avPacket) < 0) {
    //{{{  error return
    m_eof = true;
    return NULL;
    }
    //}}}
  if (avPacket.size < 0 || avPacket.stream_index >= MAX_OMX_STREAMS || interrupt_cb(NULL)) {
    //{{{  ffmpeg can return neg packet size, eof return
    if (mAvFormatContext->pb && !mAvFormatContext->pb->eof_reached)
      cLog::log (LOGERROR, "cOmxReader::Read no valid packet");
    mAvCodec.av_free_packet (&avPacket);
    m_eof = true;
    return NULL;
    }
    //}}}

  auto stream = mAvFormatContext->streams[avPacket.stream_index];

  auto packet = allocPacket (avPacket.size);
  if (!packet) {
    //{{{  error return
    m_eof = true;
    mAvCodec.av_free_packet (&avPacket);
    return NULL;
    }
    //}}}

  packet->codec_type = stream->codec->codec_type;
  packet->size = avPacket.size;
  if (avPacket.data)
    memcpy (packet->data, avPacket.data, packet->size);

  packet->stream_index = avPacket.stream_index;
  getHints (stream, &packet->hints);
  packet->dts = convertTimestamp (avPacket.dts, stream->time_base.den, stream->time_base.num);
  packet->pts = convertTimestamp (avPacket.pts, stream->time_base.den, stream->time_base.num);
  packet->duration = DVD_SEC_TO_TIME((double)avPacket.duration * stream->time_base.num / stream->time_base.den);

  // used to guess streamlength
  if ((packet->dts != DVD_NOPTS_VALUE) &&
      (packet->dts > m_iCurrentPts || m_iCurrentPts == DVD_NOPTS_VALUE))
    m_iCurrentPts = packet->dts;

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
bool cOmxReader::seek (int time, double& startPts) {

  bool backwards = (time < 0);
  if (backwards)
    time = -time;

  if (!mAvFormatContext)
    return false;

  if (mFile && !mFile->IoControl (IOCTRL_SEEK_POSSIBLE, NULL)) {
    cLog::log (LOGINFO1, "cOmxReader::SeekTime input stream reports it is not seekable");
    return false;
    }

  cSingleLock lock (m_critSection);

  if (m_ioContext)
    m_ioContext->buf_ptr = m_ioContext->buf_end;

  int64_t seek_pts = (int64_t)time * (AV_TIME_BASE / 1000);
  if (mAvFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    seek_pts += mAvFormatContext->start_time;

  RESET_TIMEOUT(1);
  int ret = mAvFormat.av_seek_frame (mAvFormatContext, -1, seek_pts, backwards ? AVSEEK_FLAG_BACKWARD : 0);
  if (ret >= 0)
    updateCurrentPTS();

  // in this case the start time is requested time
  if (startPts)
    startPts = DVD_MSEC_TO_TIME (time);

  // demuxer will return failure, if you seek to eof
  m_eof = false;
  if (ret < 0) {
    m_eof = true;
    ret = 0;
   }

  cLog::log (LOGINFO1, "cOmxReader::SeekTime %d seek ended up on time %d",
             time, (int)(m_iCurrentPts / DVD_TIME_BASE * 1000));

  return (ret >= 0);
  }
//}}}
//{{{
void cOmxReader::updateCurrentPTS() {

  m_iCurrentPts = DVD_NOPTS_VALUE;

  for (unsigned int i = 0; i < mAvFormatContext->nb_streams; i++) {
    auto stream = mAvFormatContext->streams[i];
    if (stream && stream->cur_dts != (int64_t)AV_NOPTS_VALUE) {
      double ts = convertTimestamp (stream->cur_dts, stream->time_base.den, stream->time_base.num);
      if (m_iCurrentPts == DVD_NOPTS_VALUE || m_iCurrentPts > ts )
        m_iCurrentPts = ts;
      }
    }
  }
//}}}
//{{{
void cOmxReader::clearStreams() {

  m_audio_index = -1;
  m_video_index = -1;

  m_audio_count = 0;
  m_video_count = 0;

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].extradata)
      free (m_streams[i].extradata);

    memset (m_streams[i].language, 0, sizeof(m_streams[i].language));
    m_streams[i].codec_name = "";
    m_streams[i].name = "";
    m_streams[i].type = OMXSTREAM_NONE;
    m_streams[i].stream = NULL;
    m_streams[i].extradata = NULL;
    m_streams[i].extrasize = 0;
    m_streams[i].index = 0;
    m_streams[i].id = 0;
    }

  m_program = UINT_MAX;
  }
//}}}
//{{{
bool cOmxReader::close() {

  if (mAvFormatContext) {
    if (m_ioContext && mAvFormatContext->pb && mAvFormatContext->pb != m_ioContext) {
      cLog::log(LOGINFO1, "cOmxReader::Close - demuxer changed byteContext, possible memleak");
      m_ioContext = mAvFormatContext->pb;
      }
    mAvFormat.avformat_close_input (&mAvFormatContext);
    }

  if (m_ioContext) {
    mAvUtil.av_free (m_ioContext->buffer);
    mAvUtil.av_free (m_ioContext);
    }

  m_ioContext = NULL;
  mAvFormatContext = NULL;

  if (mFile) {
    delete mFile;
    mFile = NULL;
    }

  mAvFormat.avformat_network_deinit();

  m_filename = "";
  m_video_count = 0;
  m_audio_count = 0;
  m_audio_index = -1;
  m_video_index = -1;
  m_eof = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_speed = DVD_PLAYSPEED_NORMAL;

  clearStreams();
  return true;
  }
//}}}

// private
//{{{
bool cOmxReader::getStreams() {

  if (!mAvFormatContext)
    return false;

  unsigned int m_program = UINT_MAX;

  clearStreams();
  if (mAvFormatContext->nb_programs) {
    // look for first non empty stream and discard nonselected programs
    for (unsigned int i = 0; i < mAvFormatContext->nb_programs; i++) {
      if (m_program == UINT_MAX && mAvFormatContext->programs[i]->nb_stream_indexes > 0)
        m_program = i;
      if (i != m_program)
        mAvFormatContext->programs[i]->discard = AVDISCARD_ALL;
      }
    if (m_program != UINT_MAX)
      // add streams from selected program
      for (unsigned int i = 0; i < mAvFormatContext->programs[m_program]->nb_stream_indexes; i++)
        addStream (mAvFormatContext->programs[m_program]->stream_index[i]);
    }

  // if there were no programs or they were all empty, add all streams
  if (m_program == UINT_MAX)
    for (unsigned int i = 0; i < mAvFormatContext->nb_streams; i++)
      addStream(i);

  if (m_video_count)
    setActiveStreamInternal (OMXSTREAM_VIDEO, 0);
  if (m_audio_count)
    setActiveStreamInternal (OMXSTREAM_AUDIO, 0);

  return true;
  }
//}}}
//{{{
void cOmxReader::addStream (int id) {

  if (id > MAX_STREAMS || !mAvFormatContext)
    return;

  // discard if it's a picture attachment (e.g. album art embedded in MP3 or AAC)
  auto pStream = mAvFormatContext->streams[id];
  if (pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO && (pStream->disposition & AV_DISPOSITION_ATTACHED_PIC))
    return;

  switch (pStream->codec->codec_type) {
    //{{{
    case AVMEDIA_TYPE_AUDIO:
      m_streams[id].stream      = pStream;
      m_streams[id].type        = OMXSTREAM_AUDIO;
      m_streams[id].index       = m_audio_count;
      m_streams[id].codec_name  = getStreamCodecName(pStream);
      m_streams[id].id          = id;
      m_audio_count++;
      getHints(pStream, &m_streams[id].hints);
      break;
    //}}}
    //{{{
    case AVMEDIA_TYPE_VIDEO:
      m_streams[id].stream      = pStream;
      m_streams[id].type        = OMXSTREAM_VIDEO;
      m_streams[id].index       = m_video_count;
      m_streams[id].codec_name  = getStreamCodecName(pStream);
      m_streams[id].id          = id;
      m_video_count++;
      getHints(pStream, &m_streams[id].hints);
      break;
    //}}}
    //{{{
    default:
      return;
    //}}}
    }

  auto langTag = mAvUtil.av_dict_get (pStream->metadata, "language", NULL, 0);
  if (langTag)
    strncpy (m_streams[id].language, langTag->value, 3);

  auto titleTag = mAvUtil.av_dict_get (pStream->metadata,"title", NULL, 0);
  if (titleTag)
    m_streams[id].name = titleTag->value;

  if (pStream->codec->extradata && pStream->codec->extradata_size > 0) {
    m_streams[id].extrasize = pStream->codec->extradata_size;
    m_streams[id].extradata = malloc (pStream->codec->extradata_size);
    memcpy (m_streams[id].extradata, pStream->codec->extradata, pStream->codec->extradata_size);
    }
  }
//}}}

//{{{
double cOmxReader::convertTimestamp (int64_t pts, int den, int num) {

  if (mAvFormatContext == NULL)
    return DVD_NOPTS_VALUE;

  if (pts == (int64_t)AV_NOPTS_VALUE)
    return DVD_NOPTS_VALUE;

  // do calculations in floats as they can easily overflow otherwise
  // we don't care for having a completly exact timestamp anyway
  double timestamp = (double)pts * num  / den;
  double starttime = 0.0;

  if (mAvFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    starttime = (double)mAvFormatContext->start_time / AV_TIME_BASE;

  if (timestamp > starttime)
    timestamp -= starttime;
  else if (timestamp + 0.1f > starttime )
    timestamp = 0;

  return timestamp * DVD_TIME_BASE;
  }
//}}}
//{{{
bool cOmxReader::setActiveStreamInternal (OMXStreamType type, unsigned int index) {

  bool ret = false;
  switch (type) {
    //{{{
    case OMXSTREAM_AUDIO:
      if((int)index > (m_audio_count - 1))
        index = (m_audio_count - 1);
      break;
    //}}}
    //{{{
    case OMXSTREAM_VIDEO:
      if((int)index > (m_video_count - 1))
        index = (m_video_count - 1);
      break;
    //}}}
    //{{{
    default:
      break;
    //}}}
    }

  for (int i = 0; i < MAX_STREAMS; i++) {
    if (m_streams[i].type == type &&  m_streams[i].index == index) {
      switch (m_streams[i].type) {
        //{{{
        case OMXSTREAM_AUDIO:
          m_audio_index = i;
          ret = true;
          break;
        //}}}
        //{{{
        case OMXSTREAM_VIDEO:
          m_video_index = i;
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
        m_audio_index = -1;
        break;
      //}}}
      //{{{
      case OMXSTREAM_VIDEO:
        m_video_index = -1;
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

//{{{
OMXPacket* cOmxReader::allocPacket (int size) {

  auto packet = (OMXPacket*)malloc (sizeof(OMXPacket));
  if (packet) {
    memset (packet, 0, sizeof(OMXPacket));
    packet->data = (uint8_t*)malloc (size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!packet->data) {
      free (packet);
      packet = NULL;
      }
    else {
      memset (packet->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
      packet->size = size;
      packet->dts = DVD_NOPTS_VALUE;
      packet->pts = DVD_NOPTS_VALUE;
      packet->now = DVD_NOPTS_VALUE;
      packet->duration = DVD_NOPTS_VALUE;
      }
    }

  return packet;
  }
//}}}
