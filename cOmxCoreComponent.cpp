// cOmxCoreComponent.cpp
//{{{  includes
#include "cOmxCoreComponent.h"

#include <stdio.h>
#include <string>
#include <assert.h>

#include "cLog.h"
#include "omxAlsa.h"
#include "cOmxClock.h"
//}}}

//{{{
// aligned memory allocation.
// - alloc extra space and store the original allocation in it (so that we can free later on)
//   the returned address will be the nearest alligned address within the space allocated.
void* aligned_malloc (size_t s, size_t alignTo) {

  char* fullAlloc = (char*)malloc (s + alignTo + sizeof (char*));
  char* alignedAlloc = (char*)(((((unsigned long)fullAlloc + sizeof (char*))) + (alignTo-1)) & ~(alignTo-1));
  *(char**)(alignedAlloc - sizeof(char*)) = fullAlloc;
  return alignedAlloc;
  }
//}}}
//{{{
void aligned_free (void* p) {

  if (!p)
    return;
  char* fullAlloc = *(char**)(((char*)p) - sizeof(char*));
  free (fullAlloc);
  }
//}}}
//{{{
void add_timespecs (struct timespec &time, long millisecs) {

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

  mOmx = cOmx::GetOMX();
  }
//}}}
//{{{
cOmxCoreComponent::~cOmxCoreComponent() {

  Deinitialize();

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
bool cOmxCoreComponent::Initialize (const std::string &component_name,
                                    OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks) {
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

  m_componentName = component_name;

  m_callbacks.EventHandler    = &cOmxCoreComponent::DecoderEventHandlerCallback;
  m_callbacks.EmptyBufferDone = &cOmxCoreComponent::DecoderEmptyBufferDoneCallback;
  m_callbacks.FillBufferDone  = &cOmxCoreComponent::DecoderFillBufferDoneCallback;
  if (callbacks && callbacks->EventHandler)
    m_callbacks.EventHandler    = callbacks->EventHandler;
  if (callbacks && callbacks->EmptyBufferDone)
    m_callbacks.EmptyBufferDone = callbacks->EmptyBufferDone;
  if (callbacks && callbacks->FillBufferDone)
    m_callbacks.FillBufferDone  = callbacks->FillBufferDone;

  // Get video component handle setting up callbacks, component is in loaded state on return.
  if (!m_handle) {
    OMX_ERRORTYPE omx_err;
    if (strncmp ("OMX.alsa.", component_name.c_str(), 9) == 0)
      omx_err = OMXALSA_GetHandle (&m_handle, (char*)component_name.c_str(), this, &m_callbacks);
    else
      omx_err = mOmx->OMX_GetHandle (&m_handle, (char*)component_name.c_str(), this, &m_callbacks);

    if (!m_handle || omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreComponent::Initialize no component handle %s 0x%08x",
                 component_name.c_str(), (int)omx_err);
      Deinitialize();
      return false;
      }
    }

  OMX_PORT_PARAM_TYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  if (OMX_GetParameter (m_handle, index, &port_param) != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s no get port_param %s",  __func__, component_name.c_str());

  if (DisableAllPorts() != OMX_ErrorNone)
    cLog::Log (LOGERROR, "cOmxCoreComponent::Initialize disable ports %s", component_name.c_str());

  m_input_port  = port_param.nStartPortNumber;
  m_output_port = m_input_port + 1;
  if (m_componentName == "OMX.broadcom.audio_mixer") {
    m_input_port  = port_param.nStartPortNumber + 1;
    m_output_port = port_param.nStartPortNumber;
    }

  if (m_output_port > port_param.nStartPortNumber+port_param.nPorts-1)
    m_output_port = port_param.nStartPortNumber+port_param.nPorts-1;

  cLog::Log (LOGDEBUG, "cOmxCoreComponent::Initialize %s inPort:%d outPort:%d handle:%p",
             m_componentName.c_str(), m_input_port, m_output_port, m_handle);

  m_exit = false;
  m_flush_input   = false;
  m_flush_output  = false;

  return true;
  }
//}}}
//{{{
bool cOmxCoreComponent::Deinitialize() {

  m_exit = true;
  m_flush_input = true;
  m_flush_output = true;

  if (m_handle) {
    FlushAll();
    FreeOutputBuffers();
    FreeInputBuffers();
    TransitionToStateLoaded();

    cLog::Log (LOGDEBUG, "cOmxCoreComponent::Deinitialize() %s handle:%p", m_componentName.c_str(), m_handle);

    OMX_ERRORTYPE omx_err;
    if (strncmp ("OMX.alsa.", m_componentName.c_str(), 9) == 0)
      omx_err = OMXALSA_FreeHandle (m_handle);
    else
      omx_err = mOmx->OMX_FreeHandle (m_handle);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreComponent::Deinitialize() no free handle %s 0x%08x",
                 m_componentName.c_str(), omx_err);
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
OMX_ERRORTYPE cOmxCoreComponent::EmptyThisBuffer (OMX_BUFFERHEADERTYPE* omx_buffer) {

  if (!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_EmptyThisBuffer(m_handle, omx_buffer);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s 0x%x",  __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::FillThisBuffer (OMX_BUFFERHEADERTYPE* omx_buffer) {

  if (!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_FillThisBuffer(m_handle, omx_buffer);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s result 0x%x",  __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::FreeOutputBuffer (OMX_BUFFERHEADERTYPE* omx_buffer) {

  if (!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_FreeBuffer(m_handle, m_output_port, omx_buffer);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s result 0x%x",  __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}

//{{{
void cOmxCoreComponent::FlushAll() {
  FlushInput();
  FlushOutput();
  }
//}}}
//{{{
void cOmxCoreComponent::FlushInput() {

  if (!m_handle || m_resource_error)
    return;
  if (OMX_SendCommand (m_handle, OMX_CommandFlush, m_input_port, NULL) != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s OMX_SendCommand", __func__, m_componentName.c_str());
  if (WaitForCommand (OMX_CommandFlush, m_input_port) != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s WaitForCommand", __func__, m_componentName.c_str());
  }
//}}}
//{{{
void cOmxCoreComponent::FlushOutput() {

  if (!m_handle || m_resource_error)
    return;
  if (OMX_SendCommand (m_handle, OMX_CommandFlush, m_output_port, NULL) != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s OMX_SendCommand",  __func__, m_componentName.c_str());
  if (WaitForCommand (OMX_CommandFlush, m_output_port) != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s WaitForCommand",  __func__, m_componentName.c_str());
  }
//}}}

//{{{
// timeout in milliseconds
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::GetInputBuffer (long timeout /*=200*/) {

  if (!m_handle)
    return NULL;

  pthread_mutex_lock (&m_omx_input_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);

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
        cLog::Log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omx_input_buffer;
  }
//}}}
//{{{
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::GetOutputBuffer (long timeout /*=200*/) {

  if (!m_handle)
    return NULL;

  pthread_mutex_lock (&m_omx_output_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omx_output_buffer = NULL;
  while (!m_flush_output) {
    if (m_resource_error)
      break;
    if (!m_omx_output_available.empty()) {
      omx_output_buffer = m_omx_output_available.front();
      m_omx_output_available.pop();
      break;
      }

    if (pthread_cond_timedwait(&m_output_buffer_cond, &m_omx_output_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::Log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_output_mutex);

  return omx_output_buffer;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::WaitForInputDone (long timeout /*=200*/) {

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_input_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);

  while (m_input_buffer_count != m_omx_input_avaliable.size()) {
    if (m_resource_error)
      break;
    if (pthread_cond_timedwait(&m_input_buffer_cond, &m_omx_input_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::Log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      omx_err = OMX_ErrorTimeout;
      break;
    }
  }

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::WaitForOutputDone (long timeout /*=200*/) {

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  pthread_mutex_lock (&m_omx_output_mutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);

  while (m_output_buffer_count != m_omx_output_available.size()) {
    if (m_resource_error)
      break;
    if (pthread_cond_timedwait(&m_output_buffer_cond, &m_omx_output_mutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::Log (LOGERROR, "%s %s wait event timeout", __func__, m_componentName.c_str());
      omx_err = OMX_ErrorTimeout;
      break;
      }
    }

  pthread_mutex_unlock (&m_omx_output_mutex);
  return omx_err;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::AllocInputBuffers (bool use_buffers /* = false **/) {

  m_omx_input_use_buffers = use_buffers;
  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_input_port;
  OMX_ERRORTYPE omx_err = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omx_err != OMX_ErrorNone)
    return omx_err;

  if (GetState() != OMX_StateIdle) {
    if (GetState() != OMX_StateLoaded)
      SetStateForComponent(OMX_StateLoaded);
    SetStateForComponent(OMX_StateIdle);
    }

  omx_err = EnablePort (m_input_port, false);
  if (omx_err != OMX_ErrorNone)
    return omx_err;

  m_input_alignment = portFormat.nBufferAlignment;
  m_input_buffer_count = portFormat.nBufferCountActual;
  m_input_buffer_size= portFormat.nBufferSize;

  cLog::Log (LOGDEBUG, "%s %s - port(%d), nBufferCountMin(%u), nBufferCountActual(%u), nBufferSize(%u), nBufferAlignmen(%u)",
             __func__, m_componentName.c_str(), GetInputPort(), portFormat.nBufferCountMin,
             portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE* buffer = NULL;
    OMX_U8* data = NULL;
    if (m_omx_input_use_buffers) {
      data = (OMX_U8*)aligned_malloc (portFormat.nBufferSize, m_input_alignment);
      omx_err = OMX_UseBuffer (m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize, data);
      }
    else
      omx_err = OMX_AllocateBuffer (m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s %s OMX_UseBuffer 0x%x", __func__, m_componentName.c_str(), omx_err);
      if (m_omx_input_use_buffers && data)
        aligned_free (data);
      return omx_err;
      }

    buffer->nInputPortIndex = m_input_port;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    m_omx_input_buffers.push_back (buffer);
    m_omx_input_avaliable.push (buffer);
    }

  omx_err = WaitForCommand (OMX_CommandPortEnable, m_input_port);
  if (omx_err != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "%s WaitForCommand:OMX_CommandPortEnable %s 0x%08x", __func__, m_componentName.c_str(), omx_err);
    return omx_err;
    }

  m_flush_input = false;
  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::AllocOutputBuffers (bool use_buffers /* = false */) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  m_omx_output_use_buffers = use_buffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_output_port;
  OMX_ERRORTYPE omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omx_err != OMX_ErrorNone)
    return omx_err;

  if(GetState() != OMX_StateIdle) {
    if (GetState() != OMX_StateLoaded)
      SetStateForComponent(OMX_StateLoaded);
    SetStateForComponent(OMX_StateIdle);
    }

  omx_err = EnablePort(m_output_port, false);
  if (omx_err != OMX_ErrorNone)
    return omx_err;

  m_output_alignment = portFormat.nBufferAlignment;
  m_output_buffer_count = portFormat.nBufferCountActual;
  m_output_buffer_size = portFormat.nBufferSize;
  cLog::Log (LOGDEBUG, "%s %s port(%d), nBufferCountMin(%u), nBufferCountActual(%u), nBufferSize(%u) nBufferAlignmen(%u)",
             __func__, m_componentName.c_str(), m_output_port, portFormat.nBufferCountMin,
             portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE *buffer = NULL;
    OMX_U8* data = NULL;

    if (m_omx_output_use_buffers) {
      data = (OMX_U8*)aligned_malloc(portFormat.nBufferSize, m_output_alignment);
      omx_err = OMX_UseBuffer(m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize, data);
      }
    else
      omx_err = OMX_AllocateBuffer(m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s %sOMX_UseBuffer 0x%x", __func__, m_componentName.c_str(), omx_err);

      if (m_omx_output_use_buffers && data)
       aligned_free(data);

      return omx_err;
      }

    buffer->nOutputPortIndex = m_output_port;
    buffer->nFilledLen       = 0;
    buffer->nOffset          = 0;
    buffer->pAppPrivate      = (void*)i;
    m_omx_output_buffers.push_back(buffer);
    m_omx_output_available.push(buffer);
    }

  omx_err = WaitForCommand (OMX_CommandPortEnable, m_output_port);
  if (omx_err != OMX_ErrorNone) {
    cLog::Log (LOGERROR, "%s WaitForCommand:OMX_CommandPortEnable %s 0x%08x", __func__, m_componentName.c_str(), omx_err);
    return omx_err;
    }

  m_flush_output = false;
  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::FreeInputBuffers() {

  if (!m_handle)
    return OMX_ErrorUndefined;
  if (m_omx_input_buffers.empty())
    return OMX_ErrorNone;

  m_flush_input = true;
  OMX_ERRORTYPE omx_err = DisablePort (m_input_port, false);

  pthread_mutex_lock (&m_omx_input_mutex);
  pthread_cond_broadcast (&m_input_buffer_cond);

  for (size_t i = 0; i < m_omx_input_buffers.size(); i++) {
    uint8_t* buf = m_omx_input_buffers[i]->pBuffer;
    omx_err = OMX_FreeBuffer (m_handle, m_input_port, m_omx_input_buffers[i]);
    if(m_omx_input_use_buffers && buf)
      aligned_free(buf);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "%s deallocate omx input buffer%s 0x%08x", __func__, m_componentName.c_str(), omx_err);
    }
  pthread_mutex_unlock (&m_omx_input_mutex);

  omx_err = WaitForCommand (OMX_CommandPortDisable, m_input_port);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x", __func__, m_componentName.c_str(), omx_err);

  WaitForInputDone (1000);

  pthread_mutex_lock(&m_omx_input_mutex);
  assert (m_omx_input_buffers.size() == m_omx_input_avaliable.size());

  m_omx_input_buffers.clear();

  while (!m_omx_input_avaliable.empty())
    m_omx_input_avaliable.pop();

  m_input_alignment     = 0;
  m_input_buffer_size   = 0;
  m_input_buffer_count  = 0;

  pthread_mutex_unlock (&m_omx_input_mutex);
  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::FreeOutputBuffers() {

  if(!m_handle)
    return OMX_ErrorUndefined;
  if(m_omx_output_buffers.empty())
    return OMX_ErrorNone;

  m_flush_output = true;
  OMX_ERRORTYPE omx_err = DisablePort (m_output_port, false);
  pthread_mutex_lock (&m_omx_output_mutex);
  pthread_cond_broadcast (&m_output_buffer_cond);

  for (size_t i = 0; i < m_omx_output_buffers.size(); i++) {
    uint8_t* buf = m_omx_output_buffers[i]->pBuffer;
    omx_err = OMX_FreeBuffer (m_handle, m_output_port, m_omx_output_buffers[i]);
    if (m_omx_output_use_buffers && buf)
      aligned_free (buf);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "%s deallocate omx output buffer %s 0x%08x", __func__, m_componentName.c_str(), omx_err);
    }
  pthread_mutex_unlock (&m_omx_output_mutex);

  omx_err = WaitForCommand (OMX_CommandPortDisable, m_output_port);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x",  __func__, m_componentName.c_str(), omx_err);

  WaitForOutputDone(1000);

  pthread_mutex_lock (&m_omx_output_mutex);
  assert(m_omx_output_buffers.size() == m_omx_output_available.size());

  m_omx_output_buffers.clear();
  while (!m_omx_output_available.empty())
    m_omx_output_available.pop();

  m_output_alignment    = 0;
  m_output_buffer_size  = 0;
  m_output_buffer_count = 0;

  pthread_mutex_unlock (&m_omx_output_mutex);
  return omx_err;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::EnablePort (unsigned int port,  bool wait) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  OMX_ERRORTYPE omx_err = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omx_err);

  if (portFormat.bEnabled == OMX_FALSE) {
    omx_err = OMX_SendCommand (m_handle, OMX_CommandPortEnable, port, NULL);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s enable port %d %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omx_err);
      return omx_err;
      }
    else if (wait)
      omx_err = WaitForCommand (OMX_CommandPortEnable, port);
    }

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::DisablePort (unsigned int port, bool wait) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  OMX_ERRORTYPE omx_err = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omx_err);

  if (portFormat.bEnabled == OMX_TRUE) {
    omx_err = OMX_SendCommand(m_handle, OMX_CommandPortDisable, port, NULL);
    if(omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s disable port %d %s 0x%08x", __func__, port, m_componentName.c_str(), (int)omx_err);
      return omx_err;
      }
    else if (wait)
      omx_err = WaitForCommand(OMX_CommandPortDisable, port);
    }

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::DisableAllPorts() {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_INDEXTYPE idxTypes[] = {
    OMX_IndexParamAudioInit, OMX_IndexParamImageInit, OMX_IndexParamVideoInit, OMX_IndexParamOtherInit };

  OMX_PORT_PARAM_TYPE ports;
  OMX_INIT_STRUCTURE(ports);
  int i;
  for (i = 0; i < 4; i++) {
    OMX_ERRORTYPE omx_err = OMX_GetParameter (m_handle, idxTypes[i], &ports);
    if (omx_err == OMX_ErrorNone) {
      uint32_t j;
      for (j = 0; j < ports.nPorts; j++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);
        portFormat.nPortIndex = ports.nStartPortNumber+j;
        omx_err = OMX_GetParameter (m_handle, OMX_IndexParamPortDefinition, &portFormat);
        if (omx_err != OMX_ErrorNone)
          if (portFormat.bEnabled == OMX_FALSE)
            continue;

        omx_err = OMX_SendCommand (m_handle, OMX_CommandPortDisable, ports.nStartPortNumber+j, NULL);
        if(omx_err != OMX_ErrorNone)
          cLog::Log (LOGERROR, "%s disable port %d %s 0x%08x", __func__,
                     (int)(ports.nStartPortNumber) + j, m_componentName.c_str(), (int)omx_err);
        omx_err = WaitForCommand (OMX_CommandPortDisable, ports.nStartPortNumber+j);
        if (omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
          return omx_err;
        }
      }
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::AddEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  omx_event event;
  event.eEvent = eEvent;
  event.nData1 = nData1;
  event.nData2 = nData2;

  pthread_mutex_lock (&m_omx_event_mutex);
  RemoveEvent (eEvent, nData1, nData2);
  m_omx_events.push_back (event);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&m_omx_event_cond);
  pthread_mutex_unlock (&m_omx_event_mutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
void cOmxCoreComponent::RemoveEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); ) {
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
OMX_ERRORTYPE cOmxCoreComponent::WaitForEvent (OMX_EVENTTYPE eventType, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);
  while (true) {
    for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) {
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
OMX_ERRORTYPE cOmxCoreComponent::SendCommand (OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_SendCommand (m_handle, cmd, cmdParam, cmdParamData);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::WaitForCommand (OMX_U32 command, OMX_U32 nData2, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  add_timespecs (endtime, timeout);

  while (true) {
    for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) {
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
      cLog::Log (LOGERROR, "%s %s wait timeout event.eEvent 0x%08x event.command 0x%08x event.nData2 %d",
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
OMX_ERRORTYPE cOmxCoreComponent::SetStateForComponent (OMX_STATETYPE state) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_STATETYPE state_actual = OMX_StateMax;
  if (state == state_actual)
    return OMX_ErrorNone;

  OMX_ERRORTYPE omx_err = OMX_SendCommand(m_handle, OMX_CommandStateSet, state, 0);
  if (omx_err != OMX_ErrorNone) {
    if(omx_err == OMX_ErrorSameState) {
      cLog::Log (LOGERROR, "%s %s same state", __func__, m_componentName.c_str());
      omx_err = OMX_ErrorNone;
      }
    else
      cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);
    }
  else {
    omx_err = WaitForCommand (OMX_CommandStateSet, state);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);
    }

  return omx_err;
  }
//}}}
//{{{
OMX_STATETYPE cOmxCoreComponent::GetState() const {

  if (!m_handle)
    return (OMX_STATETYPE)0;

  OMX_STATETYPE state;
  OMX_ERRORTYPE omx_err = OMX_GetState (m_handle, &state);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s failed with omx_err(0x%x)", __func__, m_componentName.c_str(), omx_err);

  return state;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::SetParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_SetParameter(m_handle, paramIndex, paramStruct);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::GetParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_GetParameter(m_handle, paramIndex, paramStruct);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::SetConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_SetConfig(m_handle, configIndex, configStruct);
  if (omx_err != OMX_ErrorNone)
    cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::GetConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const {

  if (!m_handle)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_GetConfig(m_handle, configIndex, configStruct);
  if(omx_err != OMX_ErrorNone)
    cLog::Log(LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);

  return omx_err;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::UseEGLImage (OMX_BUFFERHEADERTYPE** ppBufferHdr,
                                              OMX_U32 nPortIndex, OMX_PTR pAppPrivate, void* eglImage) {

  if (m_callbacks.FillBufferDone == &cOmxCoreComponent::DecoderFillBufferDoneCallback) {
    if (!m_handle)
      return OMX_ErrorUndefined;

    m_omx_output_use_buffers = false;

    OMX_PARAM_PORTDEFINITIONTYPE portFormat;
    OMX_INIT_STRUCTURE(portFormat);
    portFormat.nPortIndex = m_output_port;
    OMX_ERRORTYPE omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
    if (omx_err != OMX_ErrorNone)
      return omx_err;

    if (GetState() != OMX_StateIdle) {
      if (GetState() != OMX_StateLoaded)
        SetStateForComponent(OMX_StateLoaded);
      SetStateForComponent(OMX_StateIdle);
      }

    omx_err = EnablePort(m_output_port, false);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s %s EnablePort 0x%x", __func__, m_componentName.c_str(), omx_err);
      return omx_err;
      }

    m_output_alignment = portFormat.nBufferAlignment;
    m_output_buffer_count = portFormat.nBufferCountActual;
    m_output_buffer_size = portFormat.nBufferSize;

    if (portFormat.nBufferCountActual != 1) {
      cLog::Log (LOGERROR, "%s %s nBufferCountActual unexpected %d", __func__,
                 m_componentName.c_str(), portFormat.nBufferCountActual);
      return omx_err;
      }

    cLog::Log (LOGDEBUG, "%s %s ) - port(%d), nBufferCountMin(%u), nBufferCountActual(%u), nBufferSize(%u) nBufferAlignmen(%u)",
               __func__, m_componentName.c_str(), m_output_port, portFormat.nBufferCountMin,
               portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

    for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
      omx_err = OMX_UseEGLImage(m_handle, ppBufferHdr, nPortIndex, pAppPrivate, eglImage);
      if(omx_err != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);
        return omx_err;
        }

      OMX_BUFFERHEADERTYPE *buffer = *ppBufferHdr;
      buffer->nOutputPortIndex = m_output_port;
      buffer->nFilledLen = 0;
      buffer->nOffset = 0;
      buffer->pAppPrivate = (void*)i;
      m_omx_output_buffers.push_back (buffer);
      m_omx_output_available.push (buffer);
      }

    omx_err = WaitForCommand (OMX_CommandPortEnable, m_output_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s %s EnablePort 0x%x", __func__, m_componentName.c_str(), omx_err);
        return omx_err;
        }
    m_flush_output = false;
    return omx_err;
    }

  else {
    OMX_ERRORTYPE omx_err = OMX_UseEGLImage (m_handle, ppBufferHdr, nPortIndex, pAppPrivate, eglImage);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "%s %s 0x%x", __func__, m_componentName.c_str(), omx_err);
      return omx_err;
      }
    return omx_err;
    }
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::DecoderEventHandlerCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData) {

  if (!pAppData)
    return OMX_ErrorNone;

  cOmxCoreComponent *ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->DecoderEventHandler(hComponent, eEvent, nData1, nData2, pEventData);
  }
//}}}
//{{{
// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
OMX_ERRORTYPE cOmxCoreComponent::DecoderEmptyBufferDoneCallback (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                                                 OMX_BUFFERHEADERTYPE* pBuffer) {
  if (!pAppData)
    return OMX_ErrorNone;
  cOmxCoreComponent *ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->DecoderEmptyBufferDone( hComponent, pBuffer);
  }
//}}}
//{{{
// DecoderFillBufferDone -- OMXCore output buffer has been filled
OMX_ERRORTYPE cOmxCoreComponent::DecoderFillBufferDoneCallback(
  OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_BUFFERHEADERTYPE* pBuffer)
{
  if(!pAppData)
    return OMX_ErrorNone;

  cOmxCoreComponent *ctx = static_cast<cOmxCoreComponent*>(pAppData);
  return ctx->DecoderFillBufferDone(hComponent, pBuffer);
}
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::DecoderEmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer)
{
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
OMX_ERRORTYPE cOmxCoreComponent::DecoderFillBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {

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
OMX_ERRORTYPE cOmxCoreComponent::DecoderEventHandler (OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent,
                                                      OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData) {

  // if the error is expected, then we can skip it
  if (eEvent == OMX_EventError && (OMX_S32)nData1 == m_ignore_error) {
    cLog::Log (LOGDEBUG, "%s %s Ignoring expected event: eEvent(0x%x), nData1(0x%x), nData2(0x%x), pEventData(0x%p)",
               __func__, GetName().c_str(), eEvent, nData1, nData2, pEventData);
    m_ignore_error = OMX_ErrorNone;
    return OMX_ErrorNone;
    }

  AddEvent (eEvent, nData1, nData2);

  switch (eEvent) {
    //{{{
    case OMX_EventCmdComplete:
      break;
    //}}}
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
    case OMX_EventPortSettingsChanged:
      break;
    //}}}
    //{{{
    case OMX_EventParamOrConfigChanged:
      break;
    //}}}
    #if defined(OMX_DEBUG_EVENTHANDLER)
      //{{{
      case OMX_EventMark:
        cLog::Log (LOGDEBUG, "%s %s - OMX_EventMark", __func__,  GetName().c_str());
        break;
      //}}}
      //{{{
      case OMX_EventResourcesAcquired:
        cLog::Log (LOGDEBUG, "%s %s- OMX_EventResourcesAcquired", __func__,  GetName().c_str());
        break;
      //}}}
    #endif
    //{{{
    case OMX_EventError:
      switch((OMX_S32)nData1) {
        //{{{
        case OMX_ErrorSameState:
          //#if defined(OMX_DEBUG_EVENTHANDLER)
          //cLog::Log (LOGERROR, "%s %s - OMX_ErrorSameState, same state", __func__,  GetName().c_str());
          //#endif
          break;
        //}}}
        //{{{
        case OMX_ErrorInsufficientResources:
          cLog::Log (LOGERROR, "%s %s OMX_ErrorInsufficientResources, insufficient resources",
                     __func__,  GetName().c_str());
          m_resource_error = true;
          break;
        //}}}
        //{{{
        case OMX_ErrorFormatNotDetected:
          cLog::Log (LOGERROR, "%s %s OMX_ErrorFormatNotDetected, cannot parse input stream",
                     __func__,  GetName().c_str());
          break;
        //}}}
        //{{{
        case OMX_ErrorPortUnpopulated:
          cLog::Log (LOGWARNING, "%s %s OMX_ErrorPortUnpopulated port %d",
                     __func__,  GetName().c_str(), (int)nData2);
          break;
        //}}}
        //{{{
        case OMX_ErrorStreamCorrupt:
          cLog::Log (LOGERROR, "%s %s OMX_ErrorStreamCorrupt, Bitstream corrupt",
                     __func__,  GetName().c_str());
          m_resource_error = true;
          break;
        //}}}
        //{{{
        case OMX_ErrorUnsupportedSetting:
          cLog::Log (LOGERROR, "%s %s OMX_ErrorUnsupportedSetting, unsupported setting",
                     __func__,  GetName().c_str());
          break;
        //}}}
        //{{{
        default:
          cLog::Log (LOGERROR, "%s %s - OMX_EventError detected, nData1(0x%x), port %d",
                     __func__,  GetName().c_str(), nData1, (int)nData2);
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
    //{{{
    default:
      cLog::Log (LOGWARNING, "%s %s Unknown eEvent(0x%x), nData1(0x%x), port %d",
                 __func__, GetName().c_str(), eEvent, nData1, (int)nData2);
    break;
    //}}}
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
void cOmxCoreComponent::ResetEos() {

  pthread_mutex_lock (&m_omx_eos_mutex);
  m_eos = false;
  pthread_mutex_unlock (&m_omx_eos_mutex);
  }
//}}}

// private
//{{{
void cOmxCoreComponent::TransitionToStateLoaded() {

  if (!m_handle)
    return;

  if (GetState() != OMX_StateLoaded && GetState() != OMX_StateIdle)
    SetStateForComponent (OMX_StateIdle);

  if (GetState() != OMX_StateLoaded)
    SetStateForComponent (OMX_StateLoaded);
  }
//}}}
