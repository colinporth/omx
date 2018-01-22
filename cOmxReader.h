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
  OMXSTREAM_NONE  = 0,
  OMXSTREAM_AUDIO = 1,
  OMXSTREAM_VIDEO = 2,
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
class cOmxPacket {
public:
  //{{{
  cOmxPacket (int avPacketSize, int padding) {
    mData = (uint8_t*)malloc (avPacketSize + padding);
    mSize = avPacketSize;
    }
  //}}}
  //{{{
  ~cOmxPacket() {
    free (mData);
    }
  //}}}

  uint8_t* mData;
  int mSize;
  double mPts; // pts in DVD_TIME_BASE
  double mDts; // dts in DVD_TIME_BASE
  double mDuration; // duration in DVD_TIME_BASE if available

  int mStreamIndex;
  cOmxStreamInfo mHints;
  enum AVMediaType mCodecType;
  };
//}}}

class cFile;
class cOmxReader {
public:
  cOmxReader();
  ~cOmxReader();

  // gets
  bool isEof() { return mEof; }
  bool isActive (OMXStreamType type, int stream_index);
  bool canSeek();

  std::string getFilename() const { return mFilename; }

  int getWidth() { return mWidth; };
  int getHeight() { return mHeight; };
  double getAspectRatio() { return mAspect; };

  int getAudioIndex() { return (mAudioIndex >= 0) ? mStreams[mAudioIndex].index : -1; };
  int getVideoIndex() { return (mVideoIndex >= 0) ? mStreams[mVideoIndex].index : -1; };
  int getAudioStreamCount() { return mAudioCount; };
  int getVideoStreamCount() { return mVideoCount; };

  int getStreamLength() { return (int)(mAvFormatContext->duration / (AV_TIME_BASE / 1000)); }

  std::string getCodecName (OMXStreamType type);
  std::string getCodecName (OMXStreamType type, unsigned int index);
  std::string getStreamType (OMXStreamType type, unsigned int index);
  std::string getStreamName (OMXStreamType type, unsigned int index);
  std::string getStreamCodecName (AVStream* stream);
  bool getHints (AVStream* stream, cOmxStreamInfo* hints);
  bool getHints (OMXStreamType type, unsigned int index, cOmxStreamInfo& hints);
  bool getHints (OMXStreamType type, cOmxStreamInfo& hints);
  AVMediaType getPacketType (cOmxPacket* packet);

  // sets
  void setSpeed (double speed);
  double selectAspect (AVStream* st, bool& forced);
  bool setActiveStream (OMXStreamType type, unsigned int index);

  // actions
  bool open (const std::string& filename, bool dumpFormat, bool live, float timeout,
             const std::string& cookie, const std::string& user_agent,
             const std::string& lavfdopts, const std::string& avdict);
  cOmxPacket* readPacket();
  bool seek (float time, double& startPts);
  void updateCurrentPTS();
  void clearStreams();
  bool close();

private:
  bool getStreams();
  void addStream (int id);

  double convertTimestamp (int64_t pts, int den, int num);
  bool setActiveStreamInternal (OMXStreamType type, unsigned int index);

  //{{{  vars
  std::recursive_mutex mMutex;

  std::string mFilename;
  cFile* mFile = nullptr;
  bool mEof = false;

  AVIOContext* mIoContext = nullptr;
  AVFormatContext* mAvFormatContext = nullptr;

  cAvUtil mAvUtil;
  cAvCodec mAvCodec;
  cAvFormat mAvFormat;

  OMXStream mStreams[MAX_STREAMS];
  int mVideoIndex = 0;
  int mAudioIndex = 0;
  int mVideoCount = 0;
  int mAudioCount = 0;
  unsigned int mProgram = 0;

  double mSpeed = 1.0;
  double mCurPts = 0.0;

  double mAspect = 0.0;
  int mWidth = 0;
  int mHeight = 0;
  bool mSeek = false;
  };
  //}}}
