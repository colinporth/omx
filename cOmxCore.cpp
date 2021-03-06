// cOmxCore.cpp
//{{{  includes
#include <stdio.h>
#include <string>
#include <assert.h>

#include "cOmxCore.h"

#include "../shared/utils/cLog.h"
#include "cOmxClock.h"

using namespace std;
//}}}

// local
//{{{
void* alignedMalloc (size_t size, size_t alignTo) {
// alloc extra space and store the original allocation in it (so that we can free later on)
// the returned address will be the nearest alligned address within the space allocated.

  auto fullAlloc = (uint8_t*)malloc (size + alignTo + sizeof(uint8_t*));
  auto alignedAlloc = (uint8_t*)(((((unsigned long)fullAlloc +
                         sizeof (uint8_t*))) + (alignTo-1)) & ~(alignTo-1));
  *(uint8_t**)(alignedAlloc - sizeof(uint8_t*)) = fullAlloc;
  return alignedAlloc;
  }
//}}}
//{{{
void alignedFree (void* ptr) {

  if (!ptr)
    return;

  auto fullAlloc = *(uint8_t**)(((uint8_t*)ptr) - sizeof(uint8_t*));
  free (fullAlloc);
  }
//}}}
//{{{
void addTimeSpec (struct timespec &time, long mSecs) {

  long long nSec = time.tv_nsec + (long long)mSecs * 1000000;
  while (nSec > 1000000000) {
    time.tv_sec += 1;
    nSec -= 1000000000;
    }

  time.tv_nsec = nSec;
  }
//}}}

// cOmxCore
//{{{
cOmxCore::cOmxCore() {

  mEvents.clear();

  pthread_mutex_init (&mInputMutex, nullptr);
  pthread_mutex_init (&mOutputMutex, nullptr);
  pthread_mutex_init (&mEventMutex, nullptr);
  pthread_mutex_init (&mEosMutex, nullptr);
  pthread_cond_init (&mInputBufferCond, nullptr);
  pthread_cond_init (&mOutputBufferCond, nullptr);
  pthread_cond_init (&mEventCond, nullptr);
  }
//}}}
//{{{
cOmxCore::~cOmxCore() {

  deInit();

  pthread_mutex_destroy (&mInputMutex);
  pthread_mutex_destroy (&mOutputMutex);
  pthread_mutex_destroy (&mEventMutex);
  pthread_mutex_destroy (&mEosMutex);
  pthread_cond_destroy (&mInputBufferCond);
  pthread_cond_destroy (&mOutputBufferCond);
  pthread_cond_destroy (&mEventCond);
  }
//}}}

//{{{
bool cOmxCore::init (const string& name, OMX_INDEXTYPE index, OMX_CALLBACKTYPE* callbacks) {

  mName = name;

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
  mInputUseBuffers = false;
  mOutputUseBuffers = false;

  mEvents.clear();
  mIgnoreError = OMX_ErrorNone;

  mCallbacks.EventHandler = &cOmxCore::decoderEventHandlerCallback;
  mCallbacks.EmptyBufferDone = &cOmxCore::decoderEmptyBufferDoneCallback;
  mCallbacks.FillBufferDone = &cOmxCore::decoderFillBufferDoneCallback;
  if (callbacks && callbacks->EventHandler)
    mCallbacks.EventHandler = callbacks->EventHandler;
  if (callbacks && callbacks->EmptyBufferDone)
    mCallbacks.EmptyBufferDone = callbacks->EmptyBufferDone;
  if (callbacks && callbacks->FillBufferDone)
    mCallbacks.FillBufferDone = callbacks->FillBufferDone;

  // Get video component handle setting up callbacks, component is in loaded state on return.
  if (!mHandle) {
    auto omxErr = OMX_GetHandle (&mHandle, (char*)name.c_str(), this, &mCallbacks);
    if (!mHandle || omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "omxCoreComp::init - no component handle %s 0x%08x", name.c_str(), (int)omxErr);
      deInit();
      return false;
      }
    }

  OMX_PORT_PARAM_TYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  if (OMX_GetParameter (mHandle, index, &portParam))
    cLog::log (LOGERROR, "%s - no get portParam %s",  __func__, name.c_str());

  if (disableAllPorts())
    cLog::log (LOGERROR, "omxCoreComp::init - disable ports %s", name.c_str());

  mInputPort = portParam.nStartPortNumber;
  mOutputPort = mInputPort + 1;
  if (mName == "OMX.broadcom.audio_mixer") {
    mInputPort = portParam.nStartPortNumber + 1;
    mOutputPort = portParam.nStartPortNumber;
    }

  if (mOutputPort > portParam.nStartPortNumber+portParam.nPorts-1)
    mOutputPort = portParam.nStartPortNumber+portParam.nPorts-1;

  cLog::log (LOGINFO1, "omxCoreComp::init - %s in:%d out:%d h:%p",
                       mName.c_str(), mInputPort, mOutputPort, mHandle);

  mExit = false;
  mFlushInput = false;
  mFlushOutput = false;

  return true;
  }
//}}}
//{{{
bool cOmxCore::deInit() {

  mExit = true;
  mFlushInput = true;
  mFlushOutput = true;

  if (mHandle) {
    flushAll();
    freeOutputBuffers();
    freeInputBuffers();
    transitionToStateLoaded();

    cLog::log (LOGINFO1, "cOmxCore::deInit " + mName);
    if (OMX_FreeHandle (mHandle))
      cLog::log (LOGERROR, __func__ + mName + " freeHandle");
    mHandle = nullptr;
    }

  mInputPort = 0;
  mOutputPort = 0;
  mResourceError = false;

  return true;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::enablePort (unsigned int port,  bool wait) {

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);

  portFormat.nPortIndex = port;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr)
    cLog::log (LOGERROR, "%s get port %d status %s 0x%08x", __func__, port, mName.c_str(), (int)omxErr);

  if (portFormat.bEnabled == OMX_FALSE) {
    omxErr = OMX_SendCommand (mHandle, OMX_CommandPortEnable, port, nullptr);
    if (omxErr) {
      cLog::log (LOGERROR, "%s enable port %d %s 0x%08x", __func__, port, mName.c_str(), (int)omxErr);
      return omxErr;
      }
    else if (wait)
      omxErr = waitCommand (OMX_CommandPortEnable, port);
    }

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::disablePort (unsigned int port, bool wait) {

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);

  portParam.nPortIndex = port;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portParam);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " " + mName +
                         " - getParam port:" + dec(port) + " " + hex(omxErr));

  if (portParam.bEnabled == OMX_TRUE) {
    omxErr = OMX_SendCommand (mHandle, OMX_CommandPortDisable, port, nullptr);
    if (omxErr) {
      cLog::log (LOGERROR, string(__func__) + " " + mName +
                           " - sendCommand port:" + dec(port) + " " + hex(omxErr));
      return omxErr;
      }
    else if (wait)
      omxErr = waitCommand (OMX_CommandPortDisable, port);
    }

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::disableAllPorts() {

  OMX_INDEXTYPE idxTypes[] = {
    OMX_IndexParamAudioInit,
    OMX_IndexParamImageInit,
    OMX_IndexParamVideoInit,
    OMX_IndexParamOtherInit
    };

  OMX_PORT_PARAM_TYPE ports;
  OMX_INIT_STRUCTURE(ports);
  for (int i = 0; i < 4; i++) {
    auto omxErr = OMX_GetParameter (mHandle, idxTypes[i], &ports);
    if (omxErr == OMX_ErrorNone) {
      for (uint32_t j = 0; j < ports.nPorts; j++) {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);

        portFormat.nPortIndex = ports.nStartPortNumber+j;
        omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
        if (omxErr)
          if (portFormat.bEnabled == OMX_FALSE)
            continue;

        omxErr = OMX_SendCommand (mHandle, OMX_CommandPortDisable, ports.nStartPortNumber+j, nullptr);
        if(omxErr)
          cLog::log (LOGERROR, "%s disable port %d %s 0x%08x", __func__,
                               (int)(ports.nStartPortNumber) + j, mName.c_str(), (int)omxErr);

        omxErr = waitCommand (OMX_CommandPortDisable, ports.nStartPortNumber+j);
        if (omxErr && omxErr != OMX_ErrorSameState)
          return omxErr;
        }
      }
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::allocInputBuffers (bool useBuffers /* = false **/) {

  mInputUseBuffers = useBuffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = mInputPort;
  auto omxErr = OMX_GetParameter (mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setState (OMX_StateLoaded);
    setState (OMX_StateIdle);
    }

  omxErr = enablePort (mInputPort, false);
  if (omxErr)
    return omxErr;

  mInputAlignment = portFormat.nBufferAlignment;
  mInputBufferCount = portFormat.nBufferCountActual;
  mInputBufferSize = portFormat.nBufferSize;
  cLog::log (LOGINFO, "cOmxCore::allocInputBuffers - %s - port:%d, min:%u, actual:%u, size:%u:%u",
                      mName.c_str(), getInputPort(),
                      portFormat.nBufferCountMin, portFormat.nBufferCountActual,
                      portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE* buffer = nullptr;
    OMX_U8* data = nullptr;
    if (mInputUseBuffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, mInputAlignment);
      omxErr = OMX_UseBuffer (mHandle, &buffer, mInputPort, nullptr, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (mHandle, &buffer, mInputPort, nullptr, portFormat.nBufferSize);
    if (omxErr) {
      cLog::log (LOGERROR, "%s %s OMX_UseBuffer 0x%x", __func__, mName.c_str(), omxErr);
      if (mInputUseBuffers && data)
        alignedFree (data);
      return omxErr;
      }

    buffer->nInputPortIndex = mInputPort;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    mInputBuffers.push_back (buffer);
    mInputAvaliable.push (buffer);
    }

  omxErr = waitCommand (OMX_CommandPortEnable, mInputPort);
  if (omxErr) {
    //{{{  error return
    cLog::log (LOGERROR, "waitCommand:OMX_CommandPortEnable %s 0x%08x", mName.c_str(), omxErr);
    return omxErr;
    }
    //}}}

  mFlushInput = false;
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::allocOutputBuffers (bool useBuffers /* = false */) {

  mOutputUseBuffers = useBuffers;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);

  portFormat.nPortIndex = mOutputPort;
  auto omxErr = OMX_GetParameter(mHandle, OMX_IndexParamPortDefinition, &portFormat);
  if (omxErr)
    return omxErr;

  if (getState() != OMX_StateIdle) {
    if (getState() != OMX_StateLoaded)
      setState (OMX_StateLoaded);
    setState (OMX_StateIdle);
    }

  omxErr = enablePort (mOutputPort, false);
  if (omxErr)
    return omxErr;

  mOutputAlignment = portFormat.nBufferAlignment;
  mOutputBufferCount = portFormat.nBufferCountActual;
  mOutputBufferSize = portFormat.nBufferSize;
  cLog::log (LOGINFO, "allocOutputBuffers %s - port:%d, countMin:%u, countActual:%u, size:%u, align:%u",
                      mName.c_str(), mOutputPort,
                      portFormat.nBufferCountMin, portFormat.nBufferCountActual,
                      portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE* buffer = nullptr;
    OMX_U8* data = nullptr;

    if (mOutputUseBuffers) {
      data = (OMX_U8*)alignedMalloc (portFormat.nBufferSize, mOutputAlignment);
      omxErr = OMX_UseBuffer (mHandle, &buffer, mOutputPort, nullptr, portFormat.nBufferSize, data);
      }
    else
      omxErr = OMX_AllocateBuffer (mHandle, &buffer, mOutputPort, nullptr, portFormat.nBufferSize);
    if (omxErr) {
      cLog::log (LOGERROR, "%s %OMX_UseBuffer 0x%x", __func__, mName.c_str(), omxErr);
      if (mOutputUseBuffers && data)
       alignedFree (data);
      return omxErr;
      }

    buffer->nOutputPortIndex = mOutputPort;
    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->pAppPrivate = (void*)i;
    mOutputBuffers.push_back (buffer);
    mOutputAvailable.push (buffer);
    }

  omxErr = waitCommand (OMX_CommandPortEnable, mOutputPort);
  if (omxErr) {
    //{{{  error return
    cLog::log (LOGERROR, string(__func__)+ " " + mName +
                         " portEnable:" + dec(mOutputPort) + " " + hex(omxErr));
    return omxErr;
    }
    //}}}

  mFlushOutput = false;
  return omxErr;
  }
//}}}

//{{{
OMX_BUFFERHEADERTYPE* cOmxCore::getInputBuffer (long timeout /*=200*/) {
// timeout in milliseconds

  pthread_mutex_lock (&mInputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omxInputBuffer = nullptr;
  while (!mFlushInput) {
    if (mResourceError)
      break;
    if (!mInputAvaliable.empty()) {
      omxInputBuffer = mInputAvaliable.front();
      mInputAvaliable.pop();
      break;
      }

    if (pthread_cond_timedwait (&mInputBufferCond, &mInputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mName.c_str());
      break;
      }
    }

  pthread_mutex_unlock (&mInputMutex);
  return omxInputBuffer;
  }
//}}}
//{{{
OMX_BUFFERHEADERTYPE* cOmxCore::getOutputBuffer (long timeout /*=200*/) {

  pthread_mutex_lock (&mOutputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  OMX_BUFFERHEADERTYPE* omxOutputBuffer = nullptr;
  while (!mFlushOutput) {
    if (mResourceError)
      break;
    if (!mOutputAvailable.empty()) {
      omxOutputBuffer = mOutputAvailable.front();
      mOutputAvailable.pop();
      break;
      }

    if (pthread_cond_timedwait (&mOutputBufferCond, &mOutputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, "%s %s wait event timeout", __func__, mName);
      break;
      }
    }

  pthread_mutex_unlock (&mOutputMutex);

  return omxOutputBuffer;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::emptyThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_EmptyThisBuffer (mHandle, omxBuffer);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " " + mName);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::fillThisBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_FillThisBuffer (mHandle, omxBuffer);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " " + mName);

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::waitInputDone (long timeout /*=200*/) {

  auto omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&mInputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  while (mInputBufferCount != mInputAvaliable.size()) {
    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mInputBufferCond, &mInputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, string(__func__) + " " + mName);
      omxErr = OMX_ErrorTimeout;
      break;
    }
  }

  pthread_mutex_unlock (&mInputMutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::waitOutputDone (long timeout /*=200*/) {

  auto omxErr = OMX_ErrorNone;

  pthread_mutex_lock (&mOutputMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  while (mOutputBufferCount != mOutputAvailable.size()) {
    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mOutputBufferCond, &mOutputMutex, &endtime) != 0) {
      if (timeout != 0)
        cLog::log (LOGERROR, string(__func__) + " " + mName);
      omxErr = OMX_ErrorTimeout;
      break;
      }
    }

  pthread_mutex_unlock (&mOutputMutex);
  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::freeOutputBuffer (OMX_BUFFERHEADERTYPE* omxBuffer) {

  auto omxErr = OMX_FreeBuffer (mHandle, mOutputPort, omxBuffer);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " " + mName);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::freeInputBuffers() {

  if (mInputBuffers.empty())
    return OMX_ErrorNone;

  mFlushInput = true;
  auto omxErr = disablePort (mInputPort, false);

  pthread_mutex_lock (&mInputMutex);
  pthread_cond_broadcast (&mInputBufferCond);

  for (size_t i = 0; i < mInputBuffers.size(); i++) {
    auto buf = mInputBuffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (mHandle, mInputPort, mInputBuffers[i]);
    if (mInputUseBuffers && buf)
      alignedFree(buf);
    if (omxErr)
      cLog::log (LOGERROR, string(__func__) + " deallocate " + mName);
    }
  pthread_mutex_unlock (&mInputMutex);

  omxErr = waitCommand (OMX_CommandPortDisable, mInputPort);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " disable " + mName);

  waitInputDone (1000);

  pthread_mutex_lock (&mInputMutex);
  assert (mInputBuffers.size() == mInputAvaliable.size());
  mInputBuffers.clear();
  while (!mInputAvaliable.empty())
    mInputAvaliable.pop();

  mInputAlignment = 0;
  mInputBufferSize = 0;
  mInputBufferCount = 0;

  pthread_mutex_unlock (&mInputMutex);
  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::freeOutputBuffers() {

  if(mOutputBuffers.empty())
    return OMX_ErrorNone;

  mFlushOutput = true;
  auto omxErr = disablePort (mOutputPort, false);
  pthread_mutex_lock (&mOutputMutex);
  pthread_cond_broadcast (&mOutputBufferCond);

  for (size_t i = 0; i < mOutputBuffers.size(); i++) {
    auto buf = mOutputBuffers[i]->pBuffer;
    omxErr = OMX_FreeBuffer (mHandle, mOutputPort, mOutputBuffers[i]);
    if (mOutputUseBuffers && buf)
      alignedFree (buf);
    if (omxErr)
      cLog::log (LOGERROR, string(__func__) + " deallocate " + mName);
    }
  pthread_mutex_unlock (&mOutputMutex);

  omxErr = waitCommand (OMX_CommandPortDisable, mOutputPort);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " disable " + mName);

  waitOutputDone (1000);

  pthread_mutex_lock (&mOutputMutex);
  assert (mOutputBuffers.size() == mOutputAvailable.size());
  mOutputBuffers.clear();
  while (!mOutputAvailable.empty())
    mOutputAvailable.pop();

  mOutputAlignment = 0;
  mOutputBufferSize = 0;
  mOutputBufferCount = 0;

  pthread_mutex_unlock (&mOutputMutex);
  return omxErr;
  }
//}}}

//{{{
void cOmxCore::flushInput() {

  if (!mHandle || mResourceError)
    return;

  if (OMX_SendCommand (mHandle, OMX_CommandFlush, mInputPort, nullptr))
    cLog::log (LOGERROR, string(__func__) + " send " + mName);

  if (waitCommand (OMX_CommandFlush, mInputPort))
    cLog::log (LOGERROR, string(__func__) + " wait " + mName);
  }
//}}}
//{{{
void cOmxCore::flushOutput() {

  if (!mHandle || mResourceError)
    return;

  if (OMX_SendCommand (mHandle, OMX_CommandFlush, mOutputPort, nullptr))
    cLog::log (LOGERROR, string(__func__) + " send " + mName);

  if (waitCommand (OMX_CommandFlush, mOutputPort))
    cLog::log (LOGERROR, string(__func__) + " wait " + mName);
  }
//}}}
//{{{
void cOmxCore::flushAll() {
  flushInput();
  flushOutput();
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::addEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  omxEvent event;
  event.eEvent = eEvent;
  event.nData1 = nData1;
  event.nData2 = nData2;

  pthread_mutex_lock (&mEventMutex);
  removeEvent (eEvent, nData1, nData2);
  mEvents.push_back (event);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mEventCond);
  pthread_mutex_unlock (&mEventMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
void cOmxCore::removeEvent (OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2) {

  for (auto it = mEvents.begin(); it != mEvents.end(); ) {
    auto event = *it;
    if (event.eEvent == eEvent && event.nData1 == nData1 && event.nData2 == nData2)
      it = mEvents.erase (it);
    else
      ++it;
    }
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::waitEvent (OMX_EVENTTYPE eventType, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&mEventMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  while (true) {
    for (auto it = mEvents.begin(); it != mEvents.end(); it++) {
      auto event = *it;
      if (event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1) {
        mEvents.erase (it);
        pthread_mutex_unlock (&mEventMutex);
        return OMX_ErrorNone;
        }
      else if (event.eEvent == OMX_EventError) {
        mEvents.erase (it);
        pthread_mutex_unlock (&mEventMutex);
        return (OMX_ERRORTYPE)event.nData1;
      }
      else if (event.eEvent == eventType) {
        mEvents.erase (it);
        pthread_mutex_unlock (&mEventMutex);
        return OMX_ErrorNone;
        }
      }

    if (mResourceError)
      break;
    if (pthread_cond_timedwait (&mEventCond, &mEventMutex, &endtime) != 0) {
      pthread_mutex_unlock (&mEventMutex);
      return OMX_ErrorTimeout;
      }
    }

  pthread_mutex_unlock (&mEventMutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::sendCommand (OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData) {

  auto omxErr = OMX_SendCommand (mHandle, cmd, cmdParam, cmdParamData);
  if (omxErr)
    cLog::log (LOGERROR, string(__func__) + " " + mName);

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::waitCommand (OMX_U32 command, OMX_U32 nData2, long timeout) {
// timeout in milliseconds

  pthread_mutex_lock (&mEventMutex);

  struct timespec endtime;
  clock_gettime (CLOCK_REALTIME, &endtime);
  addTimeSpec (endtime, timeout);

  while (true) {
    for (auto it = mEvents.begin(); it != mEvents.end(); it++) {
      auto event = *it;
      if (event.eEvent == OMX_EventError &&
          event.nData1 == (OMX_U32)OMX_ErrorSameState &&
          event.nData2 == 1) {
        mEvents.erase (it);
        pthread_mutex_unlock (&mEventMutex);
        return OMX_ErrorNone;
        }

      else if (event.eEvent == OMX_EventError) {
        mEvents.erase(it);
        pthread_mutex_unlock (&mEventMutex);
        return (OMX_ERRORTYPE)event.nData1;
        }

      else if (event.eEvent == OMX_EventCmdComplete &&
               event.nData1 == command && event.nData2 == nData2) {
        mEvents.erase (it);
        pthread_mutex_unlock (&mEventMutex);
        return OMX_ErrorNone;
        }
      }

    if (mResourceError)
      break;

    if (pthread_cond_timedwait (&mEventCond, &mEventMutex, &endtime) != 0) {
      cLog::log (LOGERROR, "%s %s wait timeout event.eEvent 0x%08x event.command 0x%08x event.nData2 %d",
                           __func__, mName.c_str(),
                           (int)OMX_EventCmdComplete, (int)command, (int)nData2);

      pthread_mutex_unlock (&mEventMutex);
      return OMX_ErrorTimeout;
      }
    }

  pthread_mutex_unlock (&mEventMutex);
  return OMX_ErrorNone;
  }
//}}}

//{{{
OMX_STATETYPE cOmxCore::getState() const {

  OMX_STATETYPE state;
  if (OMX_GetState (mHandle, &state))
    cLog::log (LOGERROR, string( __func__) + " " + mName);

  return state;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::setState (OMX_STATETYPE state) {

  OMX_STATETYPE state_actual = OMX_StateMax;
  if (state == state_actual)
    return OMX_ErrorNone;

  auto omxErr = OMX_SendCommand(mHandle, OMX_CommandStateSet, state, 0);
  if (omxErr) {
    if (omxErr == OMX_ErrorSameState) {
      cLog::log (LOGERROR, string( __func__) + " sameState " + mName);
      omxErr = OMX_ErrorNone;
      }
    else
      cLog::log (LOGERROR, string( __func__) + " setState" + mName);
    }
  else {
    omxErr = waitCommand (OMX_CommandStateSet, state);
    if (omxErr)
      cLog::log (LOGERROR, string( __func__) + " wait setState " + mName);
    }

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::getParam (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) const {

  auto omxErr = OMX_GetParameter (mHandle, paramIndex, paramStruct);
  if (omxErr)
    cLog::log (LOGERROR, string( __func__) + " " + mName + " getParam");

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::setParam (OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct) {

  auto omxErr = OMX_SetParameter (mHandle, paramIndex, paramStruct);
  if (omxErr)
    cLog::log (LOGERROR, string( __func__) + " " + mName + " setParam");

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::getConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) const {

  auto omxErr = OMX_GetConfig (mHandle, configIndex, configStruct);
  if (omxErr)
    cLog::log (LOGERROR, string( __func__) + " " + mName + " getConfig");

  return omxErr;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::setConfig (OMX_INDEXTYPE configIndex, OMX_PTR configStruct) {

  auto omxErr = OMX_SetConfig (mHandle, configIndex, configStruct);
  if (omxErr)
    cLog::log (LOGERROR, string( __func__) + mName + " setConfig ");

  return omxErr;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::decoderEventHandlerCallback (OMX_HANDLETYPE component,
    OMX_PTR appData, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventData) {

  if (!appData)
    return OMX_ErrorNone;

  auto OmxCore = static_cast<cOmxCore*>(appData);
  return OmxCore->decoderEventHandler (component, eEvent, nData1, nData2, eventData);
  }
//}}}
//{{{
// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
OMX_ERRORTYPE cOmxCore::decoderEmptyBufferDoneCallback (OMX_HANDLETYPE component,
                                                                 OMX_PTR appData,
                                                                 OMX_BUFFERHEADERTYPE* buffer) {
  if (!appData)
    return OMX_ErrorNone;

  auto OmxCore = static_cast<cOmxCore*>(appData);
  return OmxCore->decoderEmptyBufferDone (component, buffer);
  }
//}}}
//{{{
// DecoderFillBufferDone -- OMXCore output buffer has been filled
OMX_ERRORTYPE cOmxCore::decoderFillBufferDoneCallback (OMX_HANDLETYPE component,
                                                                OMX_PTR appData,
                                                                OMX_BUFFERHEADERTYPE* buffer) {
  if (!appData)
    return OMX_ErrorNone;

  auto OmxCore = static_cast<cOmxCore*>(appData);
  return OmxCore->decoderFillBufferDone (component, buffer);
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCore::decoderEmptyBufferDone (OMX_HANDLETYPE component,
                                                         OMX_BUFFERHEADERTYPE* buffer) {

  if (mExit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&mInputMutex);
  mInputAvaliable.push (buffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mInputBufferCond);
  pthread_mutex_unlock (&mInputMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::decoderFillBufferDone (OMX_HANDLETYPE component,
                                                        OMX_BUFFERHEADERTYPE* buffer) {

  if (mExit)
    return OMX_ErrorNone;

  pthread_mutex_lock (&mOutputMutex);
  mOutputAvailable.push (buffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast (&mOutputBufferCond);
  pthread_mutex_unlock (&mOutputMutex);

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCore::decoderEventHandler (OMX_HANDLETYPE component,
  OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventData) {

  // if the error is expected, then we can skip it
  if (eEvent == OMX_EventError && (OMX_S32)nData1 == mIgnoreError) {
    cLog::log (LOGINFO1, "%s %s Ignoring expected event: eEvent(0x%x), nData1(0x%x), nData2(0x%x), pEventData(0x%p)",
                          __func__, getName().c_str(), eEvent, nData1, nData2, eventData);
    mIgnoreError = OMX_ErrorNone;
    return OMX_ErrorNone;
    }

  addEvent (eEvent, nData1, nData2);

  switch (eEvent) {
    //{{{
    case OMX_EventBufferFlag:
      if (nData2 & OMX_BUFFERFLAG_EOS) {
        pthread_mutex_lock (&mEosMutex);
        mEos = true;
        pthread_mutex_unlock (&mEosMutex);
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
        pthread_cond_broadcast (&mEventCond);
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

    case OMX_EventCmdComplete:
    case OMX_EventPortSettingsChanged:
    case OMX_EventParamOrConfigChanged:
      break;

    default:
      cLog::log (LOGINFO1, "%s %s Unknown eEvent(0x%x), nData1(0x%x), port %d",
                            __func__, getName().c_str(), eEvent, nData1, (int)nData2);
    break;
    }

  return OMX_ErrorNone;
  }
//}}}

//{{{
void cOmxCore::resetEos() {

  pthread_mutex_lock (&mEosMutex);
  mEos = false;
  pthread_mutex_unlock (&mEosMutex);
  }
//}}}

// private
//{{{
void cOmxCore::transitionToStateLoaded() {

  if (getState() != OMX_StateLoaded && getState() != OMX_StateIdle)
    setState (OMX_StateIdle);

  if (getState() != OMX_StateLoaded)
    setState (OMX_StateLoaded);
  }
//}}}
