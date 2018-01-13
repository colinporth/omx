// cOmxReader.h
//{{{  includes
#pragma once

#include <sys/types.h>
#include <assert.h>
#include <string>
#include <mutex>
#include <queue>

#include "avLibs.h"

#include "cOmxStreamInfo.h"
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
  AVStream* stream;
  OMXStreamType type;
  int id;
  void* extradata;
  unsigned int extrasize;
  unsigned int index;
  cOmxStreamInfo hints;
  } OMXStream;
//}}}
//{{{
typedef struct OMXPacket {
  double pts; // pts in DVD_TIME_BASE
  double dts; // dts in DVD_TIME_BASE
  double now; // dts in DVD_TIME_BASE
  double duration; // duration in DVD_TIME_BASE if available
  int size;
  uint8_t* data;
  int stream_index;
  cOmxStreamInfo hints;
  enum AVMediaType codec_type;
  } OMXPacket;
//}}}

class cFile;
class cOmxReader {
public:
  cOmxReader();
  ~cOmxReader();

  static void freePacket (OMXPacket*& packet);
  static double normalizeFrameDuration (double frameduration);

  // gets
  bool isEof() { return mEof; }
  bool isActive (int stream_index);
  bool isActive (OMXStreamType type, int stream_index);
  bool canSeek();

  std::string getFilename() const { return mFilename; }
  int getWidth() { return mWidth; };
  int getHeight() { return mHeight; };
  double getAspectRatio() { return mAspect; };
  int getAudioStreamCount() { return mAudioCount; };
  int getVideoStreamCount() { return mVideoCount; };
  int getAudioIndex() { return (mAudioIndex >= 0) ? mStreams[mAudioIndex].index : -1; };
  int getVideoIndex() { return (mVideoIndex >= 0) ? mStreams[mVideoIndex].index : -1; };
  int getRelativeIndex (size_t index) { return mStreams[index].index; }
  int getStreamLength() { return (int)(mAvFormatContext->duration / (AV_TIME_BASE / 1000)); }

  std::string getCodecName (OMXStreamType type);
  std::string getCodecName (OMXStreamType type, unsigned int index);
  std::string getStreamType (OMXStreamType type, unsigned int index);
  std::string getStreamName (OMXStreamType type, unsigned int index);
  std::string getStreamCodecName (AVStream *stream);
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
  std::recursive_mutex mMutex;

  std::string mFilename;
  cFile* mFile;
  bool mEof;

  AVFormatContext* mAvFormatContext;
  AVIOContext* mIoContext;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  OMXStream mStreams[MAX_STREAMS];
  int mVideoIndex;
  int mAudioIndex;
  int mVideoCount;
  int mAudioCount;
  unsigned int mProgram;

  int mSpeed;
  double mICurrentPts;

  double mAspect;
  int mWidth;
  int mHeight;
  bool mSeek;
  };
  //}}}
