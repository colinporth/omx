// cOmxReader.h
#pragma once
//{{{  includes
#include <sys/types.h>
#include <assert.h>
#include <string>
#include <queue>

#include "avLibs.h"

#include "cOmxStreamInfo.h"
#include "cSingleLock.h"
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

  static void freePacket (OMXPacket*& packet);
  static double normalizeFrameDuration (double frameduration);

  // gets
  bool isEof();
  bool isActive (int stream_index);
  bool isActive (OMXStreamType type, int stream_index);
  bool canSeek();

  std::string getFilename() const { return m_filename; }
  int getWidth() { return m_width; };
  int getHeight() { return m_height; };
  double getAspectRatio() { return m_aspect; };
  int getAudioStreamCount() { return m_audio_count; };
  int getVideoStreamCount() { return m_video_count; };
  int getAudioIndex() { return (m_audio_index >= 0) ? m_streams[m_audio_index].index : -1; };
  int getVideoIndex() { return (m_video_index >= 0) ? m_streams[m_video_index].index : -1; };
  int getRelativeIndex (size_t index) { return m_streams[index].index; }
  int getStreamLength();

  std::string getCodecName (OMXStreamType type);
  std::string getCodecName (OMXStreamType type, unsigned int index);
  std::string getStreamCodecName (AVStream *stream);
  std::string getStreamLanguage (OMXStreamType type, unsigned int index);
  std::string getStreamName (OMXStreamType type, unsigned int index);
  std::string getStreamType (OMXStreamType type, unsigned int index);
  bool getHints (AVStream *stream, cOmxStreamInfo *hints);
  bool getHints (OMXStreamType type, unsigned int index, cOmxStreamInfo &hints);
  bool getHints (OMXStreamType type, cOmxStreamInfo &hints);
  AVMediaType getPacketType (OMXPacket* packet);

  // sets
  bool setActiveStream (OMXStreamType type, unsigned int index);
  double selectAspect (AVStream* st, bool& forced);
  void setSpeed (int iSpeed);

  // actions
  bool open (std::string filename, bool dump_format, bool live, float timeout,
             std::string cookie, std::string user_agent, std::string lavfdopts, std::string avdict);
  OMXPacket* readPacket();
  bool seek (float time, double& startPts);
  void updateCurrentPTS();
  void clearStreams();
  bool close();

private:
  bool getStreams();
  void addStream (int id);

  double convertTimestamp (int64_t pts, int den, int num);
  bool setActiveStreamInternal (OMXStreamType type, unsigned int index);

  OMXPacket* allocPacket (int size);

  //{{{  vars
  cCriticalSection  m_critSection;

  std::string      m_filename;

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
  int              m_video_count;
  int              m_audio_count;

  double           m_iCurrentPts;
  int              m_speed;
  unsigned int     m_program;
  double           m_aspect;
  int              m_width;
  int              m_height;
  bool             m_seek;
  //}}}
  };
