#pragma once
//{{{  includes
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

  bool Initialize (const std::string &component_name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks = NULL);
  bool IsInitialized() const { return m_handle != NULL; }
  bool Deinitialize();

  OMX_ERRORTYPE AllocInputBuffers (bool use_buffers = false);
  OMX_ERRORTYPE AllocOutputBuffers (bool use_buffers = false);
  OMX_ERRORTYPE FreeOutputBuffer (OMX_BUFFERHEADERTYPE *omx_buffer);
  OMX_ERRORTYPE FreeInputBuffers();
  OMX_ERRORTYPE FreeOutputBuffers();
  void FlushAll();
  void FlushInput();
  void FlushOutput();

  OMX_HANDLETYPE GetComponent() const { return m_handle; }
  unsigned int GetInputPort() const { return m_input_port; }
  unsigned int GetOutputPort() const { return m_output_port; }
  std::string GetName() const { return m_componentName; }

  OMX_BUFFERHEADERTYPE* GetInputBuffer (long timeout=200);
  OMX_BUFFERHEADERTYPE* GetOutputBuffer (long timeout=200);
  unsigned int GetInputBufferSize() const { return m_input_buffer_count * m_input_buffer_size; }
  unsigned int GetOutputBufferSize() const { return m_output_buffer_count * m_output_buffer_size; }
  unsigned int GetInputBufferSpace() const { return m_omx_input_avaliable.size() * m_input_buffer_size; }
  unsigned int GetOutputBufferSpace() const { return m_omx_output_available.size() * m_output_buffer_size; }

  OMX_ERRORTYPE EnablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE DisablePort (unsigned int port, bool wait = true);
  OMX_ERRORTYPE DisableAllPorts();

  OMX_ERRORTYPE AddEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  void RemoveEvent(OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
  OMX_ERRORTYPE WaitForEvent (OMX_EVENTTYPE event, long timeout = 300);

  OMX_ERRORTYPE SendCommand(OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData);
  OMX_ERRORTYPE WaitForCommand (OMX_U32 command, OMX_U32 nData2, long timeout = 2000);

  OMX_ERRORTYPE SetStateForComponent (OMX_STATETYPE state);
  OMX_STATETYPE GetState() const;

  OMX_ERRORTYPE SetParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct);
  OMX_ERRORTYPE GetParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const;

  OMX_ERRORTYPE SetConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct);
  OMX_ERRORTYPE GetConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const;

  OMX_ERRORTYPE UseEGLImage (OMX_BUFFERHEADERTYPE** ppBufferHdr, OMX_U32 nPortIndex,
                             OMX_PTR pAppPrivate, void* eglImage);

  // OMXCore Decoder delegate callback routines.
  static OMX_ERRORTYPE DecoderEventHandlerCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
                                                    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
  static OMX_ERRORTYPE DecoderEmptyBufferDoneCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                                       OMX_BUFFERHEADERTYPE* pBuffer);
  static OMX_ERRORTYPE DecoderFillBufferDoneCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                                      OMX_BUFFERHEADERTYPE* pBufferHeader);

  // OMXCore decoder callback routines.
  OMX_ERRORTYPE DecoderEventHandler (OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent,
                                     OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
  OMX_ERRORTYPE DecoderEmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer);
  OMX_ERRORTYPE DecoderFillBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer);

  OMX_ERRORTYPE EmptyThisBuffer (OMX_BUFFERHEADERTYPE* omx_buffer);
  OMX_ERRORTYPE FillThisBuffer (OMX_BUFFERHEADERTYPE* omx_buffer);

  OMX_ERRORTYPE WaitForInputDone (long timeout=200);
  OMX_ERRORTYPE WaitForOutputDone (long timeout=200);

  bool IsEOS() const { return m_eos; }
  bool BadState() const { return m_resource_error; }
  void ResetEos();

  void IgnoreNextError (OMX_S32 error) { m_ignore_error = error; }

private:
  void TransitionToStateLoaded();

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
