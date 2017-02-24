#pragma once
//{{{  includes
#include <sys/types.h>
#include <assert.h>
#include <string>
#include <queue>

#include "cAvLibs.h"

#include "cOmxStreamInfo.h"
#include "cSingleLock.h"

using namespace std;
//}}}
//{{{  defines
#define MAX_OMX_STREAMS        100

#ifndef FFMPEG_FILE_BUFFER_SIZE
  #define FFMPEG_FILE_BUFFER_SIZE   32768 // default reading size for ffmpeg
#endif

#ifndef MAX_STREAMS
  #define MAX_STREAMS 100
#endif
//}}}

//{{{
typedef struct OMXPacket {
  double    pts; // pts in DVD_TIME_BASE
  double    dts; // dts in DVD_TIME_BASE
  double    now; // dts in DVD_TIME_BASE
  double    duration; // duration in DVD_TIME_BASE if available
  int       size;
  uint8_t   *data;
  int       stream_index;
  cOmxStreamInfo hints;
  enum AVMediaType codec_type;
  } OMXPacket;
//}}}
//{{{
enum OMXStreamType {
  OMXSTREAM_NONE      = 0,
  OMXSTREAM_AUDIO     = 1,
  OMXSTREAM_VIDEO     = 2,
  OMXSTREAM_SUBTITLE  = 3
  };
//}}}
//{{{
typedef struct OMXStream {
  char language[4];
  std::string name;
  std::string codec_name;
  AVStream    *stream;
  OMXStreamType type;
  int         id;
  void        *extradata;
  unsigned int extrasize;
  unsigned int index;
  cOmxStreamInfo hints;
  } OMXStream;
//}}}

class cFile;
class cOmxReader {
public:
  cOmxReader();
  ~cOmxReader();

  static void FreePacket (OMXPacket* pkt);
  static double NormalizeFrameDuration (double frameduration);

  bool Open (std::string filename, bool dump_format, bool live = false, float timeout = 0.0f, std::string cookie = "", std::string user_agent = "", std::string lavfdopts = "", std::string avdict = "");
  void ClearStreams();
  bool Close();

  OMXPacket* Read();
  AVMediaType PacketType (OMXPacket *pkt);
  bool SeekTime (int time, bool backwords, double *startpts);

  bool CanSeek();
  bool IsEof();
  std::string getFilename() const { return m_filename; }

  int AudioStreamCount() { return m_audio_count; };
  int VideoStreamCount() { return m_video_count; };
  int SubtitleStreamCount() { return m_subtitle_count; };

  int GetStreamLength();
  bool IsActive (int stream_index);
  bool IsActive (OMXStreamType type, int stream_index);

  //{{{
  int GetRelativeIndex (size_t index) {
    assert(index < MAX_STREAMS);
    return m_streams[index].index;
    }
  //}}}
  int GetAudioIndex() { return (m_audio_index >= 0) ? m_streams[m_audio_index].index : -1; };
  int GetSubtitleIndex() { return (m_subtitle_index >= 0) ? m_streams[m_subtitle_index].index : -1; };
  int GetVideoIndex() { return (m_video_index >= 0) ? m_streams[m_video_index].index : -1; };

  bool GetHints (AVStream *stream, cOmxStreamInfo *hints);
  bool GetHints (OMXStreamType type, unsigned int index, cOmxStreamInfo &hints);
  bool GetHints (OMXStreamType type, cOmxStreamInfo &hints);

  std::string GetCodecName (OMXStreamType type);
  std::string GetCodecName (OMXStreamType type, unsigned int index);
  std::string GetStreamCodecName (AVStream *stream);
  std::string GetStreamLanguage (OMXStreamType type, unsigned int index);
  std::string GetStreamName (OMXStreamType type, unsigned int index);
  std::string GetStreamType (OMXStreamType type, unsigned int index);

  int GetWidth() { return m_width; };
  int GetHeight() { return m_height; };
  double GetAspectRatio() { return m_aspect; };

  bool SetActiveStream (OMXStreamType type, unsigned int index);
  double SelectAspect (AVStream* st, bool& forced);
  void SetSpeed (int iSpeed);

  void UpdateCurrentPTS();

private:
  bool getStreams();
  void addStream (int id);
  double convertTimestamp (int64_t pts, int den, int num);
  bool setActiveStreamInternal (OMXStreamType type, unsigned int index);
  OMXPacket* allocPacket (int size);

  //{{{  vars
  cCriticalSection  m_critSection;

  cFile*           mFile;
  AVFormatContext* mAvFormatContext;
  AVIOContext*     m_ioContext;
  bool             m_eof;

  cAvUtil          mAvUtil;
  cAvCodec         mAvCodec;
  cAvFormat        mAvFormat;

  OMXStream        m_streams[MAX_STREAMS];

  int              m_video_index;
  int              m_audio_index;
  int              m_subtitle_index;
  int              m_video_count;
  int              m_audio_count;
  int              m_subtitle_count;

  bool             m_open;
  std::string      m_filename;
  bool             m_bAVI;

  double           m_iCurrentPts;
  int              m_speed;
  unsigned int     m_program;
  double           m_aspect;
  int              m_width;
  int              m_height;
  bool             m_seek;
  //}}}
  };
