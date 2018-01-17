// cOmxCore.h
//{{{  includes
#pragma once

#include <semaphore.h>
#include <string>
#include <queue>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
//}}}
//{{{
#define OMX_INIT_STRUCTURE(a) \
  memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP
//}}}
#define OMX_MAX_PORTS 10

//{{{
typedef struct omxEvent {
  OMX_EVENTTYPE eEvent;
  OMX_U32 nData1;
  OMX_U32 nData2;
  } omxEvent;
//}}}

class cOmxCore {
public:
  cOmxCore();
  ~cOmxCore();

  bool init (const std::string& name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks = NULL);
  bool deInit();

  bool isInit() const { return mHandle != NULL; }

  OMX_HANDLETYPE getComponent() const { return mHandle; }
  std::string getName() const { return mComponentName; }

  unsigned int getInputPort() const { return mInputPort; }
  unsigned int getOutputPort() const { return mOutputPort; }
  OMX_ERRORTYPE enablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE disablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE disableAllPorts();

  OMX_ERRORTYPE allocInputBuffers (bool useBuffers = false);
  OMX_ERRORTYPE allocOutputBuffers (bool useBffers = false);
  OMX_BUFFERHEADERTYPE* getInputBuffer (long timeout=200);
  OMX_BUFFERHEADERTYPE* getOutputBuffer (long timeout=200);

  unsigned int getInputBufferSize() const { return mInputBufferCount * mInputBufferSize; }
  unsigned int getOutputBufferSize() const { return mOutputBufferCount * mOutputBufferSize; }
  unsigned int getInputBufferSpace() const { return mInputAvaliable.size() * mInputBufferSize; }
  unsigned int getOutputBufferSpace() const { return mOutputAvailable.size() * mOutputBufferSize; }

  OMX_ERRORTYPE emptyThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);
  OMX_ERRORTYPE fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);

  OMX_ERRORTYPE waitInputDone (long timeout = 200);
  OMX_ERRORTYPE waitOutputDone (long timeout = 200);

  OMX_ERRORTYPE freeOutputBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);
  OMX_ERRORTYPE freeInputBuffers();
  OMX_ERRORTYPE freeOutputBuffers();

  void flushAll();
  void flushInput();
  void flushOutput();

  OMX_ERRORTYPE addEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  void removeEvent(OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  OMX_ERRORTYPE waitEvent (OMX_EVENTTYPE event, long timeout = 300);

  OMX_ERRORTYPE sendCommand(OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData);
  OMX_ERRORTYPE waitCommand (OMX_U32 command, OMX_U32 nData2, long timeout = 2000);

  OMX_STATETYPE getState() const;
  OMX_ERRORTYPE setState (OMX_STATETYPE state);

  OMX_ERRORTYPE getParam (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const;
  OMX_ERRORTYPE setParam (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct);

  OMX_ERRORTYPE getConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const;
  OMX_ERRORTYPE setConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct);

  // OMXCore Decoder delegate callback routines.
  static OMX_ERRORTYPE decoderEventHandlerCallback (
    OMX_HANDLETYPE component, OMX_PTR appData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventData);
  static OMX_ERRORTYPE decoderEmptyBufferDoneCallback (
    OMX_HANDLETYPE component, OMX_PTR appData, OMX_BUFFERHEADERTYPE* buffer);
  static OMX_ERRORTYPE decoderFillBufferDoneCallback (
    OMX_HANDLETYPE component, OMX_PTR appData, OMX_BUFFERHEADERTYPE* bufferHeader);

  // OMXCore decoder callback routines.
  OMX_ERRORTYPE decoderEventHandler (OMX_HANDLETYPE component, OMX_EVENTTYPE eEvent,
                                     OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventData);
  OMX_ERRORTYPE decoderEmptyBufferDone (OMX_HANDLETYPE component, OMX_BUFFERHEADERTYPE* buffer);
  OMX_ERRORTYPE decoderFillBufferDone (OMX_HANDLETYPE component, OMX_BUFFERHEADERTYPE* buffer);

  bool isEOS() const { return mEos; }
  bool badState() const { return mResourceError; }
  void resetEos();

  void ignoreNextError (OMX_S32 error) { mIgnoreError = error; }

private:
  void transitionToStateLoaded();

  OMX_HANDLETYPE mHandle = nullptr;
  std::string mComponentName;

  unsigned int mInputPort = 0;
  unsigned int mOutputPort = 0;

  pthread_mutex_t mEventMutex;
  std::vector<omxEvent> mEvents;
  OMX_S32 mIgnoreError = OMX_ErrorNone;

  OMX_CALLBACKTYPE mCallbacks;

  // OMXCore input buffers (demuxer packets)
  pthread_mutex_t mInputMutex;
  std::queue<OMX_BUFFERHEADERTYPE*> mInputAvaliable;
  std::vector<OMX_BUFFERHEADERTYPE*> mInputBuffers;
  unsigned int mInputAlignment = 0;
  unsigned int mInputBufferSize = 0;
  unsigned int mInputBufferCount = 0;
  bool mInputUseBuffers = false;

  // OMXCore output buffers (video frames)
  pthread_mutex_t mOutputMutex;
  std::queue<OMX_BUFFERHEADERTYPE*> mOutputAvailable;
  std::vector<OMX_BUFFERHEADERTYPE*> mOutputBuffers;
  unsigned int mOutputAlignment = 0;
  unsigned int mOutputBufferSize = 0;
  unsigned int mOutputBufferCount = 0;
  bool mOutputUseBuffers = false;

  pthread_cond_t mInputBufferCond;
  pthread_cond_t mOutputBufferCond;
  pthread_cond_t mEventCond;

  bool mFlushInput = false;
  bool mFlushOutput = false;
  bool mResourceError = false;

  pthread_mutex_t mEosMutex;
  bool mEos = false;
  bool mExit = false;
  };
