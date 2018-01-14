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

  mOmxEvents.clear();

  pthread_mutex_init (&mOmxInputMutex, nullptr);
  pthread_mutex_init (&mOmxOutputMutex, nullptr);
  pthread_mutex_init (&mOmxEventMutex, nullptr);
  pthread_mutex_init (&mOmxEosMutex, nullptr);
  pthread_cond_init (&mInputBufferCond, nullptr);
  pthread_cond_init (&mOutputBufferCond, nullptr);
  pthread_cond_init (&mOmxEventCond, nullptr);

  mOmx = cOmx::getOMX();
  }
//}}}
//{{{
cOmxCoreComponent::~cOmxCoreComponent() {

  deInit();

  pthread_mutex_destroy (&mOmxInputMutex);
  pthread_mutex_destroy (&mOmxOutputMutex);
  pthread_mutex_destroy (&mOmxEventMutex);
  pthread_mutex_destroy (&mOmxEosMutex);
  pthread_cond_destroy (&mInputBufferCond);
  pthread_cond_destroy (&mOutputBufferCond);
  pthread_cond_destroy (&mOmxEventCond);
  }
//}}}

//{{{
bool cOmxCoreComponent::init (const string& name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks) {

  mInputPort = 0;
  mOutputPort = 0;
  mHandle = nullptr;
  mInputAlignment = 0;
  mInputBufferSize = 0;
  mInputBufferCount = 0;
  mOutputAlignment = 0;
  mOutputBufferSize = 0;
  mOutputBufferCount = 0;
  mFlushInput = false;
  mFlushOutput = false;
  mResourceError = false;
  mEos = false;
  mExit = false;
  mOmxInputUseBuffers = false;
  mOmxOutputUseBuffers = false;

  mOmxEvents.clear();
  mIgnoreError = OMX_ErrorNone;
  mComponentName = name;

  mCallbacks.EventHandler = &cOmxCoreComponent::decoderEventHandlerCallback;
  mCallbacks.EmptyBufferDone = &cOmxCoreComponent::decoderEmptyBufferDoneCallback;
  mCallbacks.FillBufferDone = &cOmxCoreComponent::decoderFillBufferDoneCallback;
  if (callbacks && callbacks->EventHandler)
    mCallbacks.EventHandler = callbacks->EventHandler;
  if (callbacks && callbacks->EmptyBufferDone)
    mCallbacks.EmptyBufferDone = callbacks->EmptyBufferDone;
  if (callbacks && callbacks->FillBufferDone)
    mCallbacks.FillBufferDone = callbacks->FillBufferDone;

  // Get video component handle setting up callbacks, component is in loaded state on return.
  if (!mHandle) {
    auto omxErr = mOmx->getHandle (&mHandle, (char*)name.c_str(), this, &mCallbacks);
    if (!mHandle || omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "omxCoreComp::init - no component handle %s 0x%08x", name.c_str(), (int)omxErr);
      deInit();
      return false;
      }
    }

  OMX_PORT_PARAM_TYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  if (OMX_GetParameter (mHandle, index, &port_param) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s - no get port_param %s",  __func__, name.c_str());

  if (disableAllPorts() != OMX_ErrorNone)
    cLog::log (LOGERROR, "omxCoreComp::init - disable ports %s", name.c_str());

  mInputPort  = port_param.nStartPortNumber;
  mOutputPort = mInputPort + 1;
  if (mComponentName == "OMX.broadcom.audio_mixer") {
    mInputPort  = port_param.nStartPortNumber + 1;
    mOutputPort = port_param.nStartPortNumber;
    }

  if (mOutputPort > port_param.nStartPortNumber+port_param.nPorts-1)
    mOutputPort = port_param.nStartPortNumber+port_param.nPorts-1;

  cLog::log (LOGINFO1, "omxCoreComp::init - %s in:%d out:%d h:%p",
                       mComponentName.c_str(), mInputPort, mOutputPort, mHandle);

  mExit = false;
  mFlushInput = false;
  mFlushOutput = false;

  return true;
  }
//}}}
//{{{
bool cOmxCoreComponent::deInit() {

  mExit = true;
  mFlushInput = true;
  mFlushOutput = true;

  flushAll();
  freeOutputBuffers();
  freeInputBuffers();
  transitionToStateLoaded();

  cLog::log (LOGINFO1, "cOmxCoreComponent::deInit - %s h:%p", mComponentName.c_str(), mHandle);

  auto omxErr = mOmx->freeHandle (mHandle);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "cOmxCoreComponent::deInit - free handle %s 0x%08x",
               mComponentName.c_str(), omxErr);
  mHandle = nullptr;

  mInputPort = 0;
  mOutputPort = 0;
  mComponentName = "";
  mResourceError = false;

  return true;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::enablePort (unsigned int port,  bool wait) {

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, mComponentName.c_str(), (int)omxErr);

  if (portFormat.bEnabled == OMX_FALSE) {
    omxErr = OMX_SendCommand (mHandle, OMX_CommandPortEnable, port, nullptr);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s enable port %d %s 0x%08x", __func__, port, mComponentName.c_str(), (int)omxErr);
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

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if(omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, mComponentName.c_str(), (int)omxErr);

  if (portFormat.bEnabled == OMX_TRUE) {
    omxErr = OMX_SendCommand(mHandle, OMX_CommandPortDisable, port, nullptr);
    if(omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s disable port %d %s 0x%08x", __func__, port, mComponentName.c_str(), (int)omxErr);
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

  OMX_INDEXTYPE idxTypes[] = {
    OMX_IndexParamAudioInit, OMX_IndexParamImageInit, OMX_IndexParamVideoInit, OMX_IndexParamOtherInit };

  OMX_PORT_PARAM_TYPE ports;
  OMX_INIT_STRUCTURE(ports);
  int i;
  for (i = 0; i < 4; i++) {
    auto omxErr = OMX_GetParameter (mHandle, idxTypes[i], &ports);
    if (omxErr == OMX_ErrorNone) {
      uint32_t j;
      for (j = 0; j < ports.nPorts; j++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);
        portFormat.nPortIndex = ports.nStartPortNumber+j;
        omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
        if (omxErr != OMX_ErrorNone)
          if (portFormat.bEnabled == OMX_FALSE)
            continue;

        omxErr = OMX_SendCommand (mHandle, OMX_CommandPortDisable, ports.nStartPortNumber+j, nullptr);
        if(omxErr != OMX_ErrorNone)
          cLog::log (LOGERROR, "%s disable port %d %s 0x%08x", __func__,
                     (int)(ports.nStartPortNumber) + j, mComponentName.c_str(), (int)omxErr);
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
OMX_ERRORTYPE cOmxCoreComponent::allocInputBuffers (bool useBuffers /* = false **/) {

  mOmxInputUseBuffers = useBuffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = mInputPort;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setStateForComponent (OMX_StateLoaded);
    setStateForComponent (OMX_StateIdle);
    }

  omxErr = enablePort (mInputPort, false);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  mInputAlignment = portFormat.nBufferAlignment;
  mInputBufferCount = portFormat.nBufferCountActual;
  mInputBufferSize= portFormat.nBufferSize;
  cLog::log (LOGINFO1, "%s port:%d, min:%u, act:%u, size:%u, a:%u",
                       mComponentName.c_str(),
                       getInputPort(), portFormat.nBufferCountMin,
                       portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE* buffer = nullptr;
    OMX_U8* data = nullptr;
    if (mOmxInputUseBuffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, mInputAlignment);
      omxErr = OMX_UseBuffer (mHandle, &buffer, mInputPort, nullptr, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (mHandle, &buffer, mInputPort, nullptr, portFormat.nBufferSize);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s %s OMX_UseBuffer 0x%x", __func__, mComponentName.c_str(), omxErr);
      if (mOmxInputUseBuffers && data)
        alignedFree (data);
      return omxErr;
      }

    buffer->nInputPortIndex = mInputPort;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    mOmxInputBuffers.push_back (buffer);
    mOmxInputAvaliable.push (buffer);
    }

  omxErr = waitForCommand (OMX_CommandPortEnable, mInputPort);
  if (omxErr != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "WaitForCommand:OMX_CommandPortEnable %s 0x%08x", mComponentName.c_str(), omxErr);
    return omxErr;
    }
    //}}}

  mFlushInput = false;
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::allocOutputBuffers (bool useBuffers /* = false */) {

  mOmxOutputUseBuffers = useBuffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = mOutputPort;
  auto omxErr = OMX_GetParameter(mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setStateForComponent (OMX_StateLoaded);
    setStateForComponent (OMX_StateIdle);
    }

  omxErr = enablePort (mOutputPort, false);
  if (omxErr != OMX_ErrorNone)
    return omxErr;

  mOutputAlignment = portFormat.nBufferAlignment;
  mOutputBufferCount = portFormat.nBufferCountActual;
  mOutputBufferSize = portFormat.nBufferSize;
  cLog::log (LOGINFO1, "%s %s port:%d, min:%u, act:%u, size:%u, a:%u",
             __func__, mComponentName.c_str(), mOutputPort, portFormat.nBufferCountMin,
             portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE *buffer = nullptr;
    OMX_U8* data = nullptr;

    if (mOmxOutputUseBuffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, mOutputAlignment);
      omxErr = OMX_UseBuffer (mHandle, &buffer, mOutputPort, nullptr, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (mHandle, &buffer, mOutputPort, nullptr, portFormat.nBufferSize);

    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "%s %OMX_UseBuffer 0x%x", __func__, mComponentName.c_str(), omxErr);
      if (mOmxOutputUseBuffers && data)
       alignedFree(data);
      return omxErr;
      }

    buffer->nOutputPortIndex = mOutputPort;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    mOmxOutputBuffers.push_back (buffer);
    mOmxOutputAvailable.push (buffer);
    }

  omxErr = waitForCommand (OMX_CommandPortEnable, mOutputPort);
  if (omxErr != OMX_ErrorNone) {
    //{{{  error return
    cLog::log (LOGERROR, "WaitForCommand:OMX_CommandPortEnable %s 0x%08x",
                          mComponentName.c_str(), omxErr);
    return omxErr;
    }
    //}}}

  mFlushOutput = false;
  return omxErr;
  }
//}}}
//{{{
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::getInputBuffer (long timeout /*=200*/) {
// timeout in milliseconds

  pthread_mutex_lock (&mOmxInputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omxInputBuffer = nullptr;
  while (!mFlushInput) {
    if (mResourceError)
      break;
    if (!mOmxInputAvaliable.empty()) {
      omxInputBuffer = mOmxInputAvaliable.front();
      mOmxInputAvaliable.pop();
      break;
    }

    if (pthread_cond_timedwait (&mInputBufferCond, &mOmxInputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mComponentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&mOmxInputMutex);
  return omxInputBuffer;
  }
//}}}
//{{{
OMX_BUFFERHEADERTYPE* cOmxCoreComponent::getOutputBuffer (long timeout /*=200*/) {

  pthread_mutex_lock (&mOmxOutputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omxOutputBuffer = nullptr;
  while (!mFlushOutput) {
    if (mResourceError)
      break;
    if (!mOmxOutputAvailable.empty()) {
      omxOutputBuffer = mOmxOutputAvailable.front();
      mOmxOutputAvailable.pop();
      break;
      }

    if (pthread_cond_timedwait (&mOutputBufferCond, &mOmxOutputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mComponentName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&mOmxOutputMutex);

  return omxOutputBuffer;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::emptyThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_EmptyThisBuffer (mHandle, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x",  __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_FillThisBuffer (mHandle, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s result 0x%x",  __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForInputDone (long timeout /*=200*/) {

  auto omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&mOmxInputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (mInputBufferCount != mOmxInputAvaliable.size()) {
    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mInputBufferCond, &mOmxInputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mComponentName.c_str());
      omxErr = OMX_ErrorTimeout;
      break;
    }
  }

  pthread_mutex_unlock (&mOmxInputMutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForOutputDone (long timeout /*=200*/) {

  auto omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&mOmxOutputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (mOutputBufferCount != mOmxOutputAvailable.size()) {
    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mOutputBufferCond, &mOmxOutputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mComponentName.c_str());
      omxErr = OMX_ErrorTimeout;
      break;
      }
    }

  pthread_mutex_unlock (&mOmxOutputMutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeOutputBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_FreeBuffer (mHandle, mOutputPort, omxBuffer);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s result 0x%x",  __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeInputBuffers() {

  if (mOmxInputBuffers.empty())
    return OMX_ErrorNone;

  mFlushInput = true;
  auto omxErr = disablePort (mInputPort, false);

  pthread_mutex_lock (&mOmxInputMutex);
  pthread_cond_broadcast (&mInputBufferCond);

  for (size_t i = 0; i < mOmxInputBuffers.size(); i++) {
    uint8_t* buf = mOmxInputBuffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (mHandle, mInputPort, mOmxInputBuffers[i]);
    if(mOmxInputUseBuffers && buf)
      alignedFree(buf);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s deallocate omx input buffer%s 0x%08x", __func__, mComponentName.c_str(), omxErr);
    }
  pthread_mutex_unlock (&mOmxInputMutex);

  omxErr = waitForCommand (OMX_CommandPortDisable, mInputPort);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x", __func__, mComponentName.c_str(), omxErr);

  waitForInputDone (1000);

  pthread_mutex_lock(&mOmxInputMutex);
  assert (mOmxInputBuffers.size() == mOmxInputAvaliable.size());

  mOmxInputBuffers.clear();

  while (!mOmxInputAvaliable.empty())
    mOmxInputAvaliable.pop();

  mInputAlignment     = 0;
  mInputBufferSize   = 0;
  mInputBufferCount  = 0;

  pthread_mutex_unlock (&mOmxInputMutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::freeOutputBuffers() {

  if(mOmxOutputBuffers.empty())
    return OMX_ErrorNone;

  mFlushOutput = true;
  auto omxErr = disablePort (mOutputPort, false);
  pthread_mutex_lock (&mOmxOutputMutex);
  pthread_cond_broadcast (&mOutputBufferCond);

  for (size_t i = 0; i < mOmxOutputBuffers.size(); i++) {
    uint8_t* buf = mOmxOutputBuffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (mHandle, mOutputPort, mOmxOutputBuffers[i]);
    if (mOmxOutputUseBuffers && buf)
      alignedFree (buf);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s deallocate omx output buffer %s 0x%08x", __func__, mComponentName.c_str(), omxErr);
    }
  pthread_mutex_unlock (&mOmxOutputMutex);

  omxErr = waitForCommand (OMX_CommandPortDisable, mOutputPort);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s WaitForCommand:OMX_CommandPortDisable %s 0x%08x",  __func__, mComponentName.c_str(), omxErr);

  waitForOutputDone (1000);

  pthread_mutex_lock (&mOmxOutputMutex);
  assert (mOmxOutputBuffers.size() == mOmxOutputAvailable.size());

  mOmxOutputBuffers.clear();
  while (!mOmxOutputAvailable.empty())
    mOmxOutputAvailable.pop();

  mOutputAlignment    = 0;
  mOutputBufferSize  = 0;
  mOutputBufferCount = 0;

  pthread_mutex_unlock (&mOmxOutputMutex);
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

  if (!mHandle || mResourceError)
    return;

  if (OMX_SendCommand (mHandle, OMX_CommandFlush, mInputPort, nullptr) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s OMX_SendCommand", __func__, mComponentName.c_str());

  if (waitForCommand (OMX_CommandFlush, mInputPort) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s WaitForCommand", __func__, mComponentName.c_str());
  }
//}}}
//{{{
void cOmxCoreComponent::flushOutput() {

  if (!mHandle || mResourceError)
    return;

  if (OMX_SendCommand (mHandle, OMX_CommandFlush, mOutputPort, nullptr) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s OMX_SendCommand",  __func__, mComponentName.c_str());

  if (waitForCommand (OMX_CommandFlush, mOutputPort) != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s WaitForCommand",  __func__, mComponentName.c_str());
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::addEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  omx_event event;
  event.eEvent = eEvent;
  event.nData1 = nData1;
  event.nData2 = nData2;

  pthread_mutex_lock (&mOmxEventMutex);
  removeEvent (eEvent, nData1, nData2);
  mOmxEvents.push_back (event);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mOmxEventCond);
  pthread_mutex_unlock (&mOmxEventMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
void cOmxCoreComponent::removeEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  for (auto it = mOmxEvents.begin(); it != mOmxEvents.end(); ) {
    auto event = *it;
    if (event.eEvent == eEvent && event.nData1 == nData1 && event.nData2 == nData2) {
      it = mOmxEvents.erase(it);
      continue;
      }
    ++it;
    }
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForEvent (OMX_EVENTTYPE eventType, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&mOmxEventMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);
  while (true) {
    for (auto it = mOmxEvents.begin(); it != mOmxEvents.end(); it++) {
      auto event = *it;
      if (event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1) {
        mOmxEvents.erase (it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return OMX_ErrorNone;
        }
      else if (event.eEvent == OMX_EventError) {
        mOmxEvents.erase (it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return (OMX_ERRORTYPE)event.nData1;
      }
      else if (event.eEvent == eventType) {
        mOmxEvents.erase (it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return OMX_ErrorNone;
        }
      }

    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mOmxEventCond, &mOmxEventMutex, &endtime) != 0) {
      pthread_mutex_unlock (&mOmxEventMutex);
      return OMX_ErrorTimeout;
      }
    }

  pthread_mutex_unlock (&mOmxEventMutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::sendCommand (OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData) {

  auto omxErr = OMX_SendCommand (mHandle, cmd, cmdParam, cmdParamData);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::waitForCommand (OMX_U32 command, OMX_U32 nData2, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&mOmxEventMutex);
  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimespecs (endtime, timeout);

  while (true) {
    for (auto it = mOmxEvents.begin(); it != mOmxEvents.end(); it++) {
      auto event = *it;
      if (event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1) {
        mOmxEvents.erase (it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return OMX_ErrorNone;
        }
      else if (event.eEvent == OMX_EventError) {
        mOmxEvents.erase(it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return (OMX_ERRORTYPE)event.nData1;
        }
      else if (event.eEvent == OMX_EventCmdComplete &&
               event.nData1 == command && event.nData2 == nData2) {
        mOmxEvents.erase (it);
        pthread_mutex_unlock (&mOmxEventMutex);
        return OMX_ErrorNone;
        }
      }

    if (mResourceError)
      break;

    if (pthread_cond_timedwait (&mOmxEventCond, &mOmxEventMutex, &endtime) != 0) {
      cLog::log (LOGERROR, "%s %s wait timeout event.eEvent 0x%08x event.command 0x%08x event.nData2 %d",
                 __func__, mComponentName.c_str(), (int)OMX_EventCmdComplete, (int)command, (int)nData2);

      pthread_mutex_unlock (&mOmxEventMutex);
      return OMX_ErrorTimeout;
      }
    }

  pthread_mutex_unlock (&mOmxEventMutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_STATETYPE cOmxCoreComponent::getState() const {

  OMX_STATETYPE state;
  auto omxErr = OMX_GetState (mHandle, &state);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s failed with omxErr(0x%x)", __func__, mComponentName.c_str(), omxErr);

  return state;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::setStateForComponent (OMX_STATETYPE state) {

  OMX_STATETYPE state_actual = OMX_StateMax;
  if (state == state_actual)
    return OMX_ErrorNone;

  auto omxErr = OMX_SendCommand(mHandle, OMX_CommandStateSet, state, 0);
  if (omxErr != OMX_ErrorNone) {
    if(omxErr == OMX_ErrorSameState) {
      cLog::log (LOGERROR, "%s %s same state", __func__, mComponentName.c_str());
      omxErr = OMX_ErrorNone;
      }
    else
      cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);
    }
  else {
    omxErr = waitForCommand (OMX_CommandStateSet, state);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);
    }

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::getParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const {

  auto omxErr = OMX_GetParameter(mHandle, paramIndex, paramStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::setParameter (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) {

  auto omxErr = OMX_SetParameter(mHandle, paramIndex, paramStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::getConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const {

  auto omxErr = OMX_GetConfig (mHandle, configIndex, configStruct);
  if(omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::setConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) {

  auto omxErr = OMX_SetConfig (mHandle, configIndex, configStruct);
  if (omxErr != OMX_ErrorNone)
    cLog::log (LOGERROR, "%s %s 0x%x", __func__, mComponentName.c_str(), omxErr);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEventHandlerCallback (OMX_HANDLETYPE component,
    OMX_PTR appData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventData) {

  if (!appData)
    return OMX_ErrorNone;

  auto ctx = static_cast<cOmxCoreComponent*>(appData);
  return ctx->decoderEventHandler (component, eEvent, nData1, nData2, eventData);
  }
//}}}
//{{{
// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
OMX_ERRORTYPE cOmxCoreComponent::decoderEmptyBufferDoneCallback (OMX_HANDLETYPE component,
                                                                 OMX_PTR appData,
                                                                 OMX_BUFFERHEADERTYPE* buffer) {
  if (!appData)
    return OMX_ErrorNone;

  auto ctx = static_cast<cOmxCoreComponent*>(appData);
  return ctx->decoderEmptyBufferDone (component, buffer);
  }
//}}}
//{{{
// DecoderFillBufferDone -- OMXCore output buffer has been filled
OMX_ERRORTYPE cOmxCoreComponent::decoderFillBufferDoneCallback (OMX_HANDLETYPE component,
                                                                OMX_PTR appData,
                                                                OMX_BUFFERHEADERTYPE* buffer) {
  if (!appData)
    return OMX_ErrorNone;

  auto ctx = static_cast<cOmxCoreComponent*>(appData);
  return ctx->decoderFillBufferDone (component, buffer);
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {

  if (mExit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&mOmxInputMutex);
  mOmxInputAvaliable.push (pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mInputBufferCond);
  pthread_mutex_unlock (&mOmxInputMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderFillBufferDone (OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {

  if (mExit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&mOmxOutputMutex);
  mOmxOutputAvailable.push (pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mOutputBufferCond);
  pthread_mutex_unlock (&mOmxOutputMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreComponent::decoderEventHandler (OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent,
                                                      OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData) {

  // if the error is expected, then we can skip it
  if (eEvent == OMX_EventError && (OMX_S32)nData1 == mIgnoreError) {
    cLog::log (LOGINFO1, "%s %s Ignoring expected event: eEvent(0x%x), nData1(0x%x), nData2(0x%x), pEventData(0x%p)",
                          __func__, getName().c_str(), eEvent, nData1, nData2, pEventData);
    mIgnoreError = OMX_ErrorNone;
    return OMX_ErrorNone;
    }

  addEvent (eEvent, nData1, nData2);

  switch (eEvent) {
    //{{{
    case OMX_EventBufferFlag:
      if (nData2 & OMX_BUFFERFLAG_EOS) {
        pthread_mutex_lock (&mOmxEosMutex);
        mEos = true;
        pthread_mutex_unlock (&mOmxEosMutex);
        }
      break;
    //}}}
    //{{{
    case OMX_EventError:
      switch ((OMX_S32)nData1) {
        //{{{
        case OMX_ErrorSameState:
          //#if defined(OMX_DEBUGEventHANDLER)
          //cLog::log (LOGERROR, "%s %s - OMX_ErrorSameState, same state", __func__,  getName().c_str());
          //#endif
          break;
        //}}}
        //{{{
        case OMX_ErrorInsufficientResources:
          cLog::log (LOGERROR, "%s %s OMX_ErrorInsufficientResources, insufficient resources",
                     __func__, getName().c_str());
          mResourceError = true;
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
          mResourceError = true;
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
          cLog::log (LOGERROR, "%s %s - OMX_EVENTError detected, nData1(0x%x), port %d",
                     __func__,  getName().c_str(), nData1, (int)nData2);
          break;
        //}}}
        }

      // wake things up
      if (mResourceError) {
        pthread_cond_broadcast (&mOutputBufferCond);
        pthread_cond_broadcast (&mInputBufferCond);
        pthread_cond_broadcast (&mOmxEventCond);
       }
    break;
    //}}}

    #if defined(OMX_DEBUGEventHANDLER)
      //{{{
      case OMX_EventMark:
        cLog::log (LOGINFO1, "%s %s - OMXEventMark", __func__,  getName().c_str());
        break;
      //}}}
      //{{{
      case OMX_EventResourcesAcquired:
        cLog::log (LOGINFO1, "%s %s- OMXEventResourcesAcquired", __func__,  getName().c_str());
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

  pthread_mutex_lock (&mOmxEosMutex);
  mEos = false;
  pthread_mutex_unlock (&mOmxEosMutex);
  }
//}}}

// private
//{{{
void cOmxCoreComponent::transitionToStateLoaded() {

  if (getState() != OMX_StateLoaded && getState() != OMX_StateIdle)
    setStateForComponent (OMX_StateIdle);

  if (getState() != OMX_StateLoaded)
    setStateForComponent (OMX_StateLoaded);
  }
//}}}
