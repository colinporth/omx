// cOmxCoreTunnel.cpp
//{{{  includes
#include "cOmxTunnel.h"

#include "../shared/utils/cLog.h"
#include "cOmxClock.h"

using namespace std;
//}}}

//{{{
void cOmxTunnel::init (cOmxCore* srcComponent, int srcPort, cOmxCore* dstComponent, int dstPort) {

  mSrcComponent = srcComponent;
  mSrcPort = srcPort;
  mDstComponent = dstComponent;
  mDstPort = dstPort;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxTunnel::establish (bool enable_ports /* = true */, bool disable_ports /* = false */) {

  if (!mSrcComponent || !mDstComponent)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;
  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if (mSrcComponent->getState() == OMX_StateLoaded) {
    //{{{
    omxErr = mSrcComponent->setState (OMX_StateIdle);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish  setting state to idle %s 0x%08x",
                 mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->disablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxTunnel::Establish disable port %d %s 0x%08x",
                 mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->disablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxTunnel::Establish disable port %d %s 0x%08x",
                 mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->waitCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish  waitCommand port %d %s 0x%08x",
                 mDstPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->waitCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish  waitCommand port %d %s 0x%08x",
                 mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && mDstComponent->getComponent()) {
    //{{{
    omxErr = OMX_SetupTunnel (mSrcComponent->getComponent(), mSrcPort,
                              mDstComponent->getComponent(), mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish setup tunnel src %s port %d dst %s port %d 0x%08x\n",
                 mSrcComponent->getName().c_str(), mSrcPort,
                 mDstComponent->getName().c_str(), mDstPort, (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  else {
    cLog::log (LOGERROR, "cOmxTunnel::Establish could not setup tunnel\n");
    return OMX_ErrorUndefined;
    }

  mTunnelSet = true;

  if (mSrcComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->enablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish  enable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->enablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Establish  enable port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->waitCommand(OMX_CommandPortEnable, mDstPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;

    if (mDstComponent->getState() == OMX_StateLoaded) {
      omxErr = mDstComponent->setState (OMX_StateIdle);
      if (omxErr != OMX_ErrorNone) {
        cLog::log (LOGERROR, "cOmxCore::Establish  setting state to idle %s 0x%08x",
                             mSrcComponent->getName().c_str(), (int)omxErr);
        return omxErr;
        }
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->waitCommand (OMX_CommandPortEnable, mSrcPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;
    }
    //}}}

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxTunnel::deEstablish (bool noWait) {

  if (!mSrcComponent || !mDstComponent || !isInit())
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;

  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = mSrcComponent->disablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxTunnel::Deestablish disable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = mDstComponent->disablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxTunnel::Deestablish disable port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = mSrcComponent->waitCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Deestablish waitCommand port %d %s 0x%08x",
                           mDstPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = mDstComponent->waitCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxTunnel::Deestablish waitCommand port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = OMX_SetupTunnel (mSrcComponent->getComponent(), mSrcPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxTunnel::Deestablish unset tunnel comp src %s port %d 0x%08x\n",
                 mSrcComponent->getName().c_str(), mSrcPort, (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = OMX_SetupTunnel (mDstComponent->getComponent(), mDstPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log  (LOGERROR, "cOmxTunnel::Deestablish unset tunnel comp dst %s port %d 0x%08x\n",
                  mDstComponent->getName().c_str(), mDstPort, (int)omxErr);
    }
    //}}}

  mTunnelSet = false;
  return OMX_ErrorNone;
  }
//}}}
