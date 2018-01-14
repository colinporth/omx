// cOmxCoreComponent.cpp
//{{{  includes
#include <stdio.h>
#include <string>
#include <assert.h>

#include "cOmxCoreComponent.h"

#include "../shared/utils/cLog.h"
#include "cOmxClock.h"

using namespace std;
//}}}

//{{{
// aligned memory allocation.
// - alloc extra space and store the original allocation in it (so that we can free later on)
//   the returned address will be the nearest alligned address within the space allocated.
void* alignedMalloc (size_t s, size_t alignTo) {

  char* fullAlloc = (char*)malloc (s + alignTo + sizeof (char*));
  char* alignedAlloc = (char*)(((((unsigned long)fullAlloc + sizeof (char*))) + (alignTo-1)) & ~(alignTo-1));
  *(char**)(alignedAlloc - sizeof(char*)) = fullAlloc;
  return alignedAlloc;
  }
//}}}
//{{{
void alignedFree (void* p) {

  if (!p)
    return;
  char* fullAlloc = *(char**)(((char*)p) - sizeof(char*));
  free (fullAlloc);
  }
//}}}
//{{{
void addTimespecs (struct timespec &time, long millisecs) {

  long long nsec = time.tv_nsec + (long long)millisecs * 1000000;
  while (nsec > 1000000000) {
    time.tv_sec += 1;
     nsec -= 1000000000;
     }

  time.tv_nsec = nsec;
  }
//}}}

// cOmxCoreComponent
//{{{
cOmxCoreComponent::cOmxCoreComponent() {

  m_input_port  = 0;
  m_output_port = 0;
  m_handle      = NULL;

  m_input_alignment     = 0;
  m_input_buffer_size  = 0;
  m_input_buffer_count  = 0;

  m_output_alignment    = 0;
  m_output_buffer_size  = 0;
  m_output_buffer_count = 0;
  m_flush_input         = false;
  m_flush_output        = false;
  m_resource_error      = false;

  m_eos                 = false;
  m_exit = false;

  m_omx_input_use_buffers  = false;
  m_omx_output_use_buffers = false;

  m_omx_events.clear();
  m_ignore_error = OMX_ErrorNone;

  pthread_mutex_init (&m_omx_input_mutex, NULL);
  pthread_mutex_init (&m_omx_output_mutex, NULL);
  pthread_mutex_init (&m_omx_event_mutex, NULL);
  pthread_mutex_init (&m_omx_eos_mutex, NULL);
  pthread_cond_init (&m_input_buffer_cond, NULL);
  pthread_cond_init (&m_output_buffer_cond, NULL);
  pthread_cond_init (&m_omx_event_cond, NULL);

  mOmx = cOmx::getOMX();
  }
//}}}
//{{{
cOmxCoreComponent::~cOmxCoreComponent() {

  deInit();

  pthread_mutex_destroy (&m_omx_input_mutex);
  pthread_mutex_destroy (&m_omx_output_mutex);
  pthread_mutex_destroy (&m_omx_event_mutex);
  pthread_mutex_destroy (&m_omx_eos_mutex);
  pthread_cond_destroy (&m_input_buffer_cond);
  pthread_cond_destroy (&m_output_buffer_cond);
  pthread_cond_destroy (&m_omx_event_cond);
  }
//}}}

//{{{
bool cOmxCoreComponent::init (const string& name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks) {

  m_input_port = 0;
  m_output_port = 0;
  m_handle = NULL;
  m_input_alignment = 0;
  m_input_buffer_size = 0;
  m_input_buffer_count = 0;
  m_output_alignment = 0;
  m_output_buffer_size = 0;
  m_output_buffer_count = 0;
  m_flush_input = false;
  m_flush_output = false;
  m_resource_error = false;
  m_eos = false;
  m_exit = false;
  m_omx_input_use_buffers = false;
  m_omx_output_use_buffers = false;

  m_omx_events.clear();
  m_ignore_error = OMX_ErrorNone;
  m_componentName = name;

  m_callbacks.EventHandler = &cOmxCoreComponent::decoderEventHandlerCallback;
  m_callbacks.EmptyBufferDone = &cOmxCoreComponent::decoderEmptyBufferDoneCallback;
  m_callbacks.FillBufferDone = &cOmxCoreComponent::decoderFillBufferDoneCallback;
  if (callbacks && callbacks->EventHandler)
    m_callbacks.EventHandler = callbacks->EventHandler;
  if (callbacks && callbacks->EmptyBufferDone)
    m_callbacks.EmptyBufferDone = callbacks->EmptyBufferDone;
  if (callbacks && callbacks->FillBufferDone)
    m_callbacks.FillBufferDone = callbacks->FillBufferDone;

  // Get video component handle setting up callbacks, component is in loaded state on return.
  if (!m_handle) {
    OMX_ERRORTYPE omxErr;
    omxErr = mOmx->getHandle (&m_handle, (char*)name.c_str(), this, &m_callbacks);
    if (!m_handle || omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "omxCoreComp::init - no component handle %s 0x%08x",
                 name.c_str(), (int)omxErr);
      deInit();
      return false;
      }
    }

  OMX_PORT_PARAM_TYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  if (OMX_GetParameter (m_handle, index, &port_param) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s - no get port_param %s",  __func__, name.c_str());

  if (disableAllPorts() != OMX_ErrorNone)
    cLog::log (LOGERROR, "omxCoreComp::init - disable ports %s", name.c_str());

  m_input_port  = port_param.nStartPortNumber;
  m_output_port = m_input_port + 1;
  if (m_componentName == "OMX.broadcom.audio_mixer") {
    m_input_port  = port_param.nStartPortNumber + 1;
    m_output_port = port_param.nStartPortNumber;
    }

  if (m_output_port > port_param.nStartPortNumber+port_param.nPorts-1)
    m_output_port = port_param.nStartPortNumber+port_param.nPorts-1;

  cLog::log (LOGINFO1, "omxCoreComp::init - %s in:%d out:%d h:%p",
             m_componentName.c_str(), m_input_port, m_output_port, m_handle);

  m_exit = false;
  m_flush_input   = false;
  m_flush_output  = false;

  return true;
  }
//}}}
//{{{
bool cOmxCoreComponent::deInit() {

  m_exit = true;
  m_flush_input = true;
  m_flush_output = true;

  if (m_handle) {
    flushAll();
    freeOutputBuffers();
    freeInputBuffers();
    transitionToStateLoaded();

    cLog::log (LOGINFO1, "cOmxCoreComponent::deInit - %s h:%p", m_componentName.c_str(), m_handle);

    OMX_ERRORTYPE omxErr;
    omxErr = mOmx->freeHandle (m_handle);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreComponent::deInit - free handle %s 0x%08x",
                 m_componentName.c_str(), omxErr);
    m_handle = NULL;

    m_input_port = 0;
    m_output_port = 0;
    m_componentName = "";
    m_resource_error = false;
    }

  return true;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::allocInputBuffers (bool use_buffers /* = false **/) {

  m_omx_input_use_buffers = use_buffers;
  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_input_port;
  OMX_ERRORTYPE omxErr = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setStateForComponent (OMX_StateLoaded);
    setStateForComponent (OMX_StateIdle);
    }

  omxErr = enablePort (m_input_port, false);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  m_input_alignment = portFormat.nBufferAlignment;
  m_input_buffer_count = portFormat.nBufferCountActual;
  m_input_buffer_size= portFormat.nBufferSize;
  cLog::log (LOGINFO1, "%s port:%d, min:%u, act:%u, size:%u, a:%u",
                       m_componentName.c_str(),
                       getInputPort(), portFormat.nBufferCountMin,
                       portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE* buffer = NULL;
    OMX_U8* data = NULL;
    if (m_omx_input_use_buffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, m_input_alignment);
      omxErr = OMX_UseBuffer (m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s %s OMX_UseBuffer 0x%x", __func__, m_componentName.c_str(), omxErr);
      if (m_omx_input_use_buffers && data)
        alignedFree (data);
      return omxErr;
      }

    buffer->nInputPortIndex = m_input_port;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    m_omx_input_buffers.push_back (buffer);
    m_omx_input_avaliable.push (buffer);
    }

  omxErr = waitForCommand (OMX_CommandPortEnable, m_input_port);
  if (omxErr != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "WaitForCommand:OMX_CommandPortEnable %s 0x%08x", m_componentName.c_str(), omxErr);
    return omxErr;
    }
    //}}}

  m_flush_input = false;
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::allocOutputBuffers (bool use_buffers /* = false */) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  m_omx_output_use_buffers = use_buffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_output_port;
  OMX_ERRORTYPE omxErr = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setStateForComponent (OMX_StateLoaded);
    setStateForComponent (OMX_StateIdle);
    }

  omxErr = enablePort (m_output_port, false);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  m_output_alignment = portFormat.nBufferAlignment;
  m_output_buffer_count = portFormat.nBufferCountActual;
  m_output_buffer_size = portFormat.nBufferSize;
  cLog::log (LOGINFO1, "%s %s port:%d, min:%u, act:%u, size:%u, a:%u",
             __func__, m_componentName.c_str(), m_output_port, portFormat.nBufferCountMin,
             portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE *buffer = NULL;
    OMX_U8* data = NULL;

    if (m_omx_output_use_buffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, m_output_alignment);
      omxErr = OMX_UseBuffer(m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize);

    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s %sOMX_UseBuffer 0x%x", __func__, m_componentName.c_str(), omxErr);
      if (m_omx_output_use_buffers && data)
       alignedFree(data);
      return omxErr;
      }

    buffer->nOutputPortIndex = m_output_port;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate= (void*)i;
    m_omx_output_buffers.push_back(buffer);
    m_omx_output_available.push(buffer);
    }

  omxErr = waitForCommand (OMX_CommandPortEnable, m_output_port);
  if (omxErr != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "WaitForCommand:OMX_CommandPortEnable %s 0x%08x",
                          m_componentName.c_str(), omxErr);
    return omxErr;
    }
    //}}}

  m_flush_output = false;
  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeOutputBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  if (!m_handle || !omxBuffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_FreeBuffer (m_handle, m_output_port, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s result 0x%x",  __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeInputBuffers() {

  if (!m_handle)
    return OMX_ErrorUndefined;
  if (m_omx_input_buffers.empty())
    return OMX_ErrorNone;

  m_flush_input = true;
  OMX_ERRORTYPE omxErr = disablePort (m_input_port, false);

  pthread_mutex_lock (&m_omx_input_mutex);
  pthread_cond_broadcast (&m_input_buffer_cond);

  for (size_t i = 0; i < m_omx_input_buffers.size(); i++) {
    uint8_t* buf = m_omx_input_buffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (m_handle, m_input_port, m_omx_input_buffers[i]);
    if(m_omx_input_use_buffers && buf)
      alignedFree(buf);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s deallocate omx input buffer%s 0x%08x", __func__, m_componentName.c_str(), omxErr);
    }
  pthread_mutex_unlock (&m_omx_input_mutex);

  omxErr = waitForCommand (OMX_CommandPortDisable, m_input_port);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x", __func__, m_componentName.c_str(), omxErr);

  waitForInputDone (1000);

  pthread_mutex_lock(&m_omx_input_mutex);
  assert (m_omx_input_buffers.size() == m_omx_input_avaliable.size());

  m_omx_input_buffers.clear();

  while (!m_omx_input_avaliable.empty())
    m_omx_input_avaliable.pop();

  m_input_alignment     = 0;
  m_input_buffer_size   = 0;
  m_input_buffer_count  = 0;

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeOutputBuffers() {

  if(!m_handle)
    return OMX_ErrorUndefined;
  if(m_omx_output_buffers.empty())
    return OMX_ErrorNone;

  m_flush_output = true;
  OMX_ERRORTYPE omxErr = disablePort (m_output_port, false);
  pthread_mutex_lock (&m_omx_output_mutex);
  pthread_cond_broadcast (&m_output_buffer_cond);

  for (size_t i = 0; i < m_omx_output_buffers.size(); i++) {
    uint8_t* buf = m_omx_output_buffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (m_handle, m_output_port, m_omx_output_buffers[i]);
    if (m_omx_output_use_buffers && buf)
      alignedFree (buf);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s deallocate omx output buffer %s 0x%08x", __func__, m_componentName.c_str(), omxErr);
    }
  pthread_mutex_unlock (&m_omx_output_mutex);

  omxErr = waitForCommand (OMX_CommandPortDisable, m_output_port);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x",  __func__, m_componentName.c_str(), omxErr);

  waitForOutputDone (1000);

  pthread_mutex_lock (&m_omx_output_mutex);
  assert (m_omx_output_buffers.size() == m_omx_output_available.size());

  m_omx_output_buffers.clear();
  while (!m_omx_output_available.empty())
    m_omx_output_available.pop();

  m_output_alignment    = 0;
  m_output_buffer_size  = 0;
  m_output_buffer_count = 0;

  pthread_mutex_unlock (&m_omx_output_mutex);
  return omxErr;
  }
//}}}
//{{{
void cOmxCoreComponent::flushAll() {
  flushInput();
  flushOutput();
  }
//}}}
//{{{
void cOmxCoreComponent::flushInput() {

  if (!m_handle || m_resource_error)
    return;

  if (OMX_SendCommand (m_handle, OMX_CommandFlush, m_input_port, NULL) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s OMX_SendCommand", __func__, m_componentName.c_str());
  if (waitForCommand (OMX_CommandFlush, m_input_port) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s WaitForCommand", __func__, m_componentName.c_str());
  }
//}}}
//{{{
void cOmxCoreComponent::flushOutput() {

  if (!m_handle || m_resource_error)
    return;

  if (OMX_SendCommand (m_handle, OMX_CommandFlush, m_output_port, NULL) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s OMX_SendCommand",  __func__, m_componentName.c_str());
  if (waitForCommand (OMX_CommandFlush, m_output_port) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s WaitForCommand",  __func__, m_componentName.c_str());
  }
//}}}

//{{{
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::getInputBuffer (long timeout /*=200*/) {
// timeout in milliseconds

  if (!m_handle)
    return NULL;

  pthread_mutex_lock (&m_omx_input_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omx_input_buffer = NULL;
  while (!m_flush_input) {
    if (m_resource_error)
      break;
    if (!m_omx_input_avaliable.empty()) {
      omx_input_buffer = m_omx_input_avaliable.front();
      m_omx_input_avaliable.pop();
      break;
    }

    if (pthread_cond_timedwait (&m_input_buffer_cond, &m_omx_input_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omx_input_buffer;
  }
//}}}
//{{{
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::getOutputBuffer (long timeout /*=200*/) {

  if (!m_handle)
    return NULL;

  pthread_mutex_lock (&m_omx_output_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omx_output_buffer = NULL;
  while (!m_flush_output) {
    if (m_resource_error)
      break;
    if (!m_omx_output_available.empty()) {
      omx_output_buffer = m_omx_output_available.front();
      m_omx_output_available.pop();
      break;
      }

    if (pthread_cond_timedwait (&m_output_buffer_cond, &m_omx_output_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_output_mutex);

  return omx_output_buffer;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::emptyThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  if (!m_handle || !omxBuffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_EmptyThisBuffer (m_handle, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x",  __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  if (!m_handle || !omxBuffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_FillThisBuffer (m_handle, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s result 0x%x",  __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForInputDone (long timeout /*=200*/) {

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_input_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (m_input_buffer_count != m_omx_input_avaliable.size()) {
    if (m_resource_error)
      break;
    if (pthread_cond_timedwait (&m_input_buffer_cond, &m_omx_input_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      omxErr = OMX_ErrorTimeout;
      break;
    }
  }

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForOutputDone (long timeout /*=200*/) {

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_output_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (m_output_buffer_count != m_omx_output_available.size()) {
    if (m_resource_error)
      break;
    if (pthread_cond_timedwait (&m_output_buffer_cond, &m_omx_output_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      omxErr = OMX_ErrorTimeout;
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_output_mutex);
  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::enablePort (unsigned int port,  bool wait) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  OMX_ERRORTYPE omxErr = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omxErr);

  if (portFormat.bEnabled == OMX_FALSE) {
    omxErr = OMX_SendCommand (m_handle, OMX_CommandPortEnable, port, NULL);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s enable port %d %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omxErr);
      return omxErr;
      }
    else if (wait)
      omxErr = waitForCommand (OMX_CommandPortEnable, port);
    }

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::disablePort (unsigned int port, bool wait) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  OMX_ERRORTYPE omxErr = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omxErr);

  if (portFormat.bEnabled == OMX_TRUE) {
    omxErr = OMX_SendCommand(m_handle, OMX_CommandPortDisable, port, NULL);
    if(omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s disable port %d %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omxErr);
      return omxErr;
      }
    else if (wait)
      omxErr = waitForCommand(OMX_CommandPortDisable, port);
    }

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::disableAllPorts() {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_INDEXTYPE idxTypes[] = {
    OMX_IndexParamAudioInit, OMX_IndexParamImageInit, OMX_IndexParamVideoInit, OMX_IndexParamOtherInit };

  OMX_PORT_PARAM_TYPE ports;
  OMX_INIT_STRUCTURE(ports);
  int i;
  for (i = 0; i < 4; i++) {
    OMX_ERRORTYPE omxErr = OMX_GetParameter (m_handle, idxTypes[i], &ports);
    if (omxErr == OMX_ErrorNone) {
      uint32_t j;
      for (j = 0; j < ports.nPorts; j++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);
        portFormat.nPortIndex = ports.nStartPortNumber+j;
        omxErr = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
        if (omxErr != OMX_ErrorNone)
          if (portFormat.bEnabled == OMX_FALSE)
            continue;

        omxErr = OMX_SendCommand (m_handle, OMX_CommandPortDisable, ports.nStartPortNumber+j, NULL);
        if(omxErr != OMX_ErrorNone)
          cLog::log (LOGERROR, "%s disable port %d %s 0x%08x", __func__,
                     (int)(ports.nStartPortNumber) + j, m_componentName.c_str(), (int)omxErr);
        omxErr = waitForCommand (OMX_CommandPortDisable, ports.nStartPortNumber+j);
        if (omxErr != OMX_ErrorNone && omxErr != OMX_ErrorSameState)
          return omxErr;
        }
      }
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::addEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  omx_event event;
  event.eEvent = eEvent;
  event.nData1 = nData1;
  event.nData2 = nData2;

  pthread_mutex_lock (&m_omx_event_mutex);
  removeEvent (eEvent, nData1, nData2);
  m_omx_events.push_back (event);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&m_omx_event_cond);
  pthread_mutex_unlock (&m_omx_event_mutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
void cOmxCoreComponent::removeEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  for (vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); ) {
    omx_event event = *it;
    if (event.eEvent == eEvent && event.nData1 == nData1 && event.nData2 == nData2) {
      it = m_omx_events.erase(it);
      continue;
      }
    ++it;
    }
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForEvent (OMX_EVENTTYPE eventType, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);
  while (true) {
    for (vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) {
      omx_event event = *it;
      if (event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1) {
        m_omx_events.erase (it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return OMX_ErrorNone;
        }
      else if(event.eEvent == OMX_EventError) {
        m_omx_events.erase (it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return (OMX_ERRORTYPE)event.nData1;
      }
      else if (event.eEvent == eventType) {
        m_omx_events.erase (it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return OMX_ErrorNone;
        }
      }

    if (m_resource_error)
      break;
    if (pthread_cond_timedwait (&m_omx_event_cond, &m_omx_event_mutex, &endtime) != 0) {
      pthread_mutex_unlock (&m_omx_event_mutex);
      return OMX_ErrorTimeout;
      }
    }
  pthread_mutex_unlock (&m_omx_event_mutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::sendCommand (OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr;

  omxErr = OMX_SendCommand (m_handle, cmd, cmdParam, cmdParamData);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForCommand (OMX_U32 command, OMX_U32 nData2, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (true) {
    for (vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) {
      omx_event event = *it;
      if (event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1) {
        m_omx_events.erase (it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return OMX_ErrorNone;
        }

      else if (event.eEvent == OMX_EventError) {
        m_omx_events.erase(it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return (OMX_ERRORTYPE)event.nData1;
        }
      else if (event.eEvent == OMX_EventCmdComplete && event.nData1 == command && event.nData2 == nData2) {
        m_omx_events.erase (it);
        pthread_mutex_unlock (&m_omx_event_mutex);
        return OMX_ErrorNone;
        }
      }

    if (m_resource_error)
      break;

    if (pthread_cond_timedwait (&m_omx_event_cond, &m_omx_event_mutex, &endtime) != 0) {
      cLog::log (LOGERROR, "%s %s wait timeout event.eEvent 0x%08x event.command 0x%08x event.nData2 %d",
                 __func__, m_componentName.c_str(), (int)OMX_EventCmdComplete, (int)command, (int)nData2);

      pthread_mutex_unlock (&m_omx_event_mutex);
      return OMX_ErrorTimeout;
      }
    }

  pthread_mutex_unlock (&m_omx_event_mutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::setStateForComponent (OMX_STATETYPE state) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_STATETYPE state_actual = OMX_StateMax;
  if (state == state_actual)
    return OMX_ErrorNone;

  OMX_ERRORTYPE omxErr = OMX_SendCommand(m_handle, OMX_CommandStateSet, state, 0);
  if (omxErr != OMX_ErrorNone) {
    if(omxErr == OMX_ErrorSameState) {
      cLog::log (LOGERROR, "%s %s same state", __func__, m_componentName.c_str());
      omxErr = OMX_ErrorNone;
      }
    else
      cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);
    }
  else {
    omxErr = waitForCommand (OMX_CommandStateSet, state);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);
    }

  return omxErr;
  }
//}}}
//{{{
OMX_STATETYPE cOmxCoreComponent::getState() const {

  if (!m_handle)
    return (OMX_STATETYPE)0;

  OMX_STATETYPE state;
  OMX_ERRORTYPE omxErr = OMX_GetState (m_handle, &state);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s failed with omxErr(0x%x)", __func__, m_componentName.c_str(), omxErr);

  return state;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::setParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_SetParameter(m_handle, paramIndex, paramStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::getParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_GetParameter(m_handle, paramIndex, paramStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::setConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_SetConfig (m_handle, configIndex, configStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::getConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_GetConfig (m_handle, configIndex, configStruct);
  if(omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omxErr);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEventHandlerCallback (OMX_HANDLETYPE hComponent, 
    OMX_PTR pAppData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData) {

  if (!pAppData)
    return OMX_ErrorNone;

  cOmxCoreComponent* ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->decoderEventHandler (hComponent, eEvent, nData1, nData2, pEventData);
  }
//}}}
//{{{
// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
OMX_ERRORTYPE cOmxCoreComponent::decoderEmptyBufferDoneCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                                                 OMX_BUFFERHEADERTYPE* pBuffer) {
  if (!pAppData)
    return OMX_ErrorNone;
  cOmxCoreComponent* ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->decoderEmptyBufferDone (hComponent, pBuffer);
  }
//}}}
//{{{
// DecoderFillBufferDone -- OMXCore output buffer has been filled
OMX_ERRORTYPE cOmxCoreComponent::decoderFillBufferDoneCallback (OMX_HANDLETYPE hComponent,
                                                                OMX_PTR pAppData,
                                                                OMX_BUFFERHEADERTYPE* pBuffer) {
  if (!pAppData)
    return OMX_ErrorNone;

  cOmxCoreComponent* ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->decoderFillBufferDone (hComponent, pBuffer);
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {

  if (m_exit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_input_mutex);
  m_omx_input_avaliable.push (pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&m_input_buffer_cond);
  pthread_mutex_unlock (&m_omx_input_mutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderFillBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {

  if (m_exit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_output_mutex);
  m_omx_output_available.push (pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&m_output_buffer_cond);
  pthread_mutex_unlock (&m_omx_output_mutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEventHandler (OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent,
                                                      OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData) {

  // if the error is expected, then we can skip it
  if (eEvent == OMX_EventError && (OMX_S32)nData1 == m_ignore_error) {
    cLog::log (LOGINFO1, "%s %s Ignoring expected event: eEvent(0x%x), nData1(0x%x), nData2(0x%x), pEventData(0x%p)",
                          __func__, getName().c_str(), eEvent, nData1, nData2, pEventData);
    m_ignore_error = OMX_ErrorNone;
    return OMX_ErrorNone;
    }

  addEvent (eEvent, nData1, nData2);

  switch (eEvent) {
    //{{{
    case OMX_EventBufferFlag:
      if (nData2 & OMX_BUFFERFLAG_EOS) {
        pthread_mutex_lock (&m_omx_eos_mutex);
        m_eos = true;
        pthread_mutex_unlock (&m_omx_eos_mutex);
        }
      break;
    //}}}
    //{{{
    case OMX_EventError:
      switch ((OMX_S32)nData1) {
        //{{{
        case OMX_ErrorSameState:
          //#if defined(OMX_DEBUG_EVENTHANDLER)
          //cLog::log (LOGERROR, "%s %s - OMX_ErrorSameState, same state", __func__,  getName().c_str());
          //#endif
          break;
        //}}}
        //{{{
        case OMX_ErrorInsufficientResources:
          cLog::log (LOGERROR, "%s %s OMX_ErrorInsufficientResources, insufficient resources",
                     __func__, getName().c_str());
          m_resource_error = true;
          break;
        //}}}
        //{{{
        case OMX_ErrorFormatNotDetected:
          cLog::log (LOGERROR, "%s %s OMX_ErrorFormatNotDetected, cannot parse input stream",
                     __func__,  getName().c_str());
          break;
        //}}}
        //{{{
        case OMX_ErrorPortUnpopulated:
          cLog::log (LOGINFO1, "%s %s OMX_ErrorPortUnpopulated port %d",
                     __func__,  getName().c_str(), (int)nData2);
          break;
        //}}}
        //{{{
        case OMX_ErrorStreamCorrupt:
          cLog::log (LOGERROR, "%s %s OMX_ErrorStreamCorrupt, Bitstream corrupt",
                     __func__,  getName().c_str());
          m_resource_error = true;
          break;
        //}}}
        //{{{
        case OMX_ErrorUnsupportedSetting:
          cLog::log (LOGERROR, "%s %s OMX_ErrorUnsupportedSetting, unsupported setting",
                     __func__,  getName().c_str());
          break;
        //}}}
        //{{{
        default:
          cLog::log (LOGERROR, "%s %s - OMX_EventError detected, nData1(0x%x), port %d",
                     __func__,  getName().c_str(), nData1, (int)nData2);
          break;
        //}}}
        }

      // wake things up
      if (m_resource_error) {
        pthread_cond_broadcast (&m_output_buffer_cond);
        pthread_cond_broadcast (&m_input_buffer_cond);
        pthread_cond_broadcast (&m_omx_event_cond);
       }
    break;
    //}}}

    #if defined(OMX_DEBUG_EVENTHANDLER)
      //{{{
      case OMX_EventMark:
        cLog::log (LOGINFO1, "%s %s - OMX_EventMark", __func__,  getName().c_str());
        break;
      //}}}
      //{{{
      case OMX_EventResourcesAcquired:
        cLog::log (LOGINFO1, "%s %s- OMX_EventResourcesAcquired", __func__,  getName().c_str());
        break;
      //}}}
    #endif

    case OMX_EventCmdComplete: break;
    case OMX_EventPortSettingsChanged: break;
    case OMX_EventParamOrConfigChanged: break;
    default:
      cLog::log (LOGINFO1, "%s %s Unknown eEvent(0x%x), nData1(0x%x), port %d",
                            __func__, getName().c_str(), eEvent, nData1, (int)nData2);
    break;
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
void cOmxCoreComponent::resetEos() {

  pthread_mutex_lock (&m_omx_eos_mutex);
  m_eos = false;
  pthread_mutex_unlock (&m_omx_eos_mutex);
  }
//}}}

// private
//{{{
void cOmxCoreComponent::transitionToStateLoaded() {

  if (!m_handle)
    return;

  if (getState() != OMX_StateLoaded && getState() != OMX_StateIdle)
    setStateForComponent (OMX_StateIdle);

  if (getState() != OMX_StateLoaded)
    setStateForComponent (OMX_StateLoaded);
  }
//}}}
