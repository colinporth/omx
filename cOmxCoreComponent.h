//{{{  includes
#pragma once

#include <string>
#include <queue>
#include <semaphore.h>

#include "cOmx.h"
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
typedef struct omx_event {
  OMX_EVENTTYPE eEvent;
  OMX_U32 nData1;
  OMX_U32 nData2;
  } omx_event;
//}}}

class cOmxCoreClock;
class cOmxCoreComponent {
public:
  cOmxCoreComponent();
  ~cOmxCoreComponent();

  bool init (const std::string& name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks = NULL);
  bool isInit() const { return mHandle != NULL; }
  bool deInit();

  OMX_ERRORTYPE allocInputBuffers (bool useBuffers = false);
  OMX_ERRORTYPE allocOutputBuffers (bool useBffers = false);
  OMX_ERRORTYPE freeOutputBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);
  OMX_ERRORTYPE freeInputBuffers();
  OMX_ERRORTYPE freeOutputBuffers();
  void flushAll();
  void flushInput();
  void flushOutput();

  OMX_HANDLETYPE getComponent() const { return mHandle; }
  unsigned int getInputPort() const { return mInputPort; }
  unsigned int getOutputPort() const { return mOutputPort; }
  std::string getName() const { return mComponentName; }

  OMX_BUFFERHEADERTYPE* getInputBuffer (long timeout=200);
  OMX_BUFFERHEADERTYPE* getOutputBuffer (long timeout=200);
  unsigned int getInputBufferSize() const { return mInputBufferCount * mInputBufferSize; }
  unsigned int getOutputBufferSize() const { return mOutputBufferCount * mOutputBufferSize; }
  unsigned int getInputBufferSpace() const { return mOmxInputAvaliable.size() * mInputBufferSize; }
  unsigned int getOutputBufferSpace() const { return mOmxOutputAvailable.size() * mOutputBufferSize; }

  OMX_ERRORTYPE enablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE disablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE disableAllPorts();

  OMX_ERRORTYPE addEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  void removeEvent(OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  OMX_ERRORTYPE waitForEvent (OMX_EVENTTYPE event, long timeout = 300);

  OMX_ERRORTYPE sendCommand(OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData);
  OMX_ERRORTYPE waitForCommand (OMX_U32 command, OMX_U32 nData2, long timeout = 2000);

  OMX_ERRORTYPE setStateForComponent (OMX_STATETYPE state);
  OMX_STATETYPE getState() const;

  OMX_ERRORTYPE setParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct);
  OMX_ERRORTYPE getParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const;

  OMX_ERRORTYPE setConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct);
  OMX_ERRORTYPE getConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const;

  // OMXCore Decoder delegate callback routines.
  static OMX_ERRORTYPE decoderEventHandlerCallback (
    OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
  static OMX_ERRORTYPE decoderEmptyBufferDoneCallback (
    OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer);
  static OMX_ERRORTYPE decoderFillBufferDoneCallback (
    OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBufferHeader);

  // OMXCore decoder callback routines.
  OMX_ERRORTYPE decoderEventHandler (OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent,
                                     OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
  OMX_ERRORTYPE decoderEmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer);
  OMX_ERRORTYPE decoderFillBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer);

  OMX_ERRORTYPE emptyThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);
  OMX_ERRORTYPE fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);

  OMX_ERRORTYPE waitForInputDone (long timeout=200);
  OMX_ERRORTYPE waitForOutputDone (long timeout=200);

  bool isEOS() const { return mEos; }
  bool badState() const { return mResourceError; }
  void resetEos();

  void ignoreNextError (OMX_S32 error) { mIgnoreError = error; }

private:
  void transitionToStateLoaded();

  OMX_HANDLETYPE  mHandle;
  unsigned int mInputPort;
  unsigned int mOutputPort;
  std::string mComponentName;
  pthread_mutex_t mOmxEventMutex;
  pthread_mutex_t mOmxEosMutex;
  std::vector<omx_event> mOmxEvents;
  OMX_S32 mIgnoreError;

  OMX_CALLBACKTYPE mCallbacks;

  // OMXCore input buffers (demuxer packets)
  pthread_mutex_t mOmxInputMutex;
  std::queue<OMX_BUFFERHEADERTYPE*> mOmxInputAvaliable;
  std::vector<OMX_BUFFERHEADERTYPE*> mOmxInputBuffers;
  unsigned int mInputAlignment;
  unsigned int mInputBufferSize;
  unsigned int mInputBufferCount;
  bool mOmxInputUseBuffers;

  // OMXCore output buffers (video frames)
  pthread_mutex_t mOmxOutputMutex;
  std::queue<OMX_BUFFERHEADERTYPE*> mOmxOutputAvailable;
  std::vector<OMX_BUFFERHEADERTYPE*> mOmxOutputBuffers;
  unsigned int mOutputAlignment;
  unsigned int mOutputBufferSize;
  unsigned int mOutputBufferCount;
  bool mOmxOutputUseBuffers;
  cOmx* mOmx;

  pthread_cond_t mInputBufferCond;
  pthread_cond_t mOutputBufferCond;
  pthread_cond_t mOmxEventCond;

  bool mFlushInput;
  bool mFlushOutput;
  bool mResourceError;

  bool mEos;
  bool mExit;
  };
