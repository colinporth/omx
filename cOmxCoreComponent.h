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

  bool init (const std::string &name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks = NULL);
  bool isInit() const { return m_handle != NULL; }
  bool deInit();

  OMX_ERRORTYPE allocInputBuffers (bool use_buffers = false);
  OMX_ERRORTYPE allocOutputBuffers (bool use_buffers = false);
  OMX_ERRORTYPE freeOutputBuffer (OMX_BUFFERHEADERTYPE *omxBuffer);
  OMX_ERRORTYPE freeInputBuffers();
  OMX_ERRORTYPE freeOutputBuffers();
  void flushAll();
  void flushInput();
  void flushOutput();

  OMX_HANDLETYPE getComponent() const { return m_handle; }
  unsigned int getInputPort() const { return m_input_port; }
  unsigned int getOutputPort() const { return m_output_port; }
  std::string getName() const { return m_componentName; }

  OMX_BUFFERHEADERTYPE* getInputBuffer (long timeout=200);
  OMX_BUFFERHEADERTYPE* getOutputBuffer (long timeout=200);
  unsigned int getInputBufferSize() const { return m_input_buffer_count * m_input_buffer_size; }
  unsigned int getOutputBufferSize() const { return m_output_buffer_count * m_output_buffer_size; }
  unsigned int getInputBufferSpace() const { return m_omx_input_avaliable.size() * m_input_buffer_size; }
  unsigned int getOutputBufferSpace() const { return m_omx_output_available.size() * m_output_buffer_size; }

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

  OMX_ERRORTYPE emptyThisBuffer (OMX_BUFFERHEADERTYPE* omx_buffer);
  OMX_ERRORTYPE fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer);

  OMX_ERRORTYPE waitForInputDone (long timeout=200);
  OMX_ERRORTYPE waitForOutputDone (long timeout=200);

  bool isEOS() const { return m_eos; }
  bool badState() const { return m_resource_error; }
  void resetEos();

  void ignoreNextError (OMX_S32 error) { m_ignore_error = error; }

private:
  void transitionToStateLoaded();

  OMX_HANDLETYPE  m_handle;
  unsigned int    m_input_port;
  unsigned int    m_output_port;
  std::string     m_componentName;
  pthread_mutex_t m_omx_event_mutex;
  pthread_mutex_t m_omx_eos_mutex;
  std::vector<omx_event> m_omx_events;
  OMX_S32 m_ignore_error;

  OMX_CALLBACKTYPE  m_callbacks;

  // OMXCore input buffers (demuxer packets)
  pthread_mutex_t m_omx_input_mutex;
  std::queue<OMX_BUFFERHEADERTYPE*> m_omx_input_avaliable;
  std::vector<OMX_BUFFERHEADERTYPE*> m_omx_input_buffers;
  unsigned int m_input_alignment;
  unsigned int m_input_buffer_size;
  unsigned int m_input_buffer_count;
  bool         m_omx_input_use_buffers;

  // OMXCore output buffers (video frames)
  pthread_mutex_t   m_omx_output_mutex;
  std::queue<OMX_BUFFERHEADERTYPE*> m_omx_output_available;
  std::vector<OMX_BUFFERHEADERTYPE*> m_omx_output_buffers;
  unsigned int m_output_alignment;
  unsigned int m_output_buffer_size;
  unsigned int m_output_buffer_count;
  bool         m_omx_output_use_buffers;
  cOmx*          mOmx;

  pthread_cond_t m_input_buffer_cond;
  pthread_cond_t m_output_buffer_cond;
  pthread_cond_t m_omx_event_cond;

  bool m_flush_input;
  bool m_flush_output;
  bool m_resource_error;

  bool m_eos;
  bool m_exit;
  };
