// cOmxCoreTunnel.cpp
//{{{  includes
#include "cOmxCoreTunnel.h"

#include "../shared/utils/cLog.h"
#include "cOmxClock.h"

using namespace std;
//}}}

cOmxCoreTunnel::cOmxCoreTunnel() { m_OMX = cOmx::getOMX(); }
cOmxCoreTunnel::~cOmxCoreTunnel() {}

//{{{
void cOmxCoreTunnel::init (cOmxCoreComponent* srcComponent, unsigned int srcPort,
                                 cOmxCoreComponent* dstComponent, unsigned int dstPort) {
  mSrcComponent = srcComponent;
  mSrcPort = srcPort;
  mDstComponent = dstComponent;
  mDstPort = dstPort;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreTunnel::establish (bool enable_ports /* = true */, bool disable_ports /* = false */) {

  if (!mSrcComponent || !mDstComponent)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;
  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if (mSrcComponent->getState() == OMX_StateLoaded) {
    //{{{
    omxErr = mSrcComponent->setStateForComponent (OMX_StateIdle);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  setting state to idle %s 0x%08x",
                 mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->disablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->disablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->waitForCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 mDstPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->waitForCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && mDstComponent->getComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mSrcComponent->getComponent(), mSrcPort,
                                 mDstComponent->getComponent(), mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish setup tunnel src %s port %d dst %s port %d 0x%08x\n",
                 mSrcComponent->getName().c_str(), mSrcPort,
                 mDstComponent->getName().c_str(), mDstPort, (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  else {
    cLog::log (LOGERROR, "cOmxCoreTunnel::Establish could not setup tunnel\n");
    return OMX_ErrorUndefined;
    }

  mTunnelSet = true;

  if (mSrcComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->enablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->enablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->waitForCommand(OMX_CommandPortEnable, mDstPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;

    if (mDstComponent->getState() == OMX_StateLoaded) {
      omxErr = mDstComponent->setStateForComponent (OMX_StateIdle);
      if (omxErr != OMX_ErrorNone) {
        cLog::log (LOGERROR, "cOmxCoreComponent::Establish  setting state to idle %s 0x%08x",
                             mSrcComponent->getName().c_str(), (int)omxErr);
        return omxErr;
        }
      }
    }
    //}}}
  if (mSrcComponent->getComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->waitForCommand (OMX_CommandPortEnable, mSrcPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;
    }
    //}}}

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreTunnel::deEstablish (bool noWait) {

  if (!mSrcComponent || !mDstComponent || !isInit())
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;

  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = mSrcComponent->disablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = mDstComponent->disablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = mSrcComponent->waitForCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                           mDstPort, mSrcComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = mDstComponent->waitForCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                           mDstPort, mDstComponent->getName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->getComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mSrcComponent->getComponent(), mSrcPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp src %s port %d 0x%08x\n",
                 mSrcComponent->getName().c_str(), mSrcPort, (int)omxErr);
    }
    //}}}
  if (mDstComponent->getComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mDstComponent->getComponent(), mDstPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log  (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp dst %s port %d 0x%08x\n",
                  mDstComponent->getName().c_str(), mDstPort, (int)omxErr);
    }
    //}}}

  mTunnelSet = false;
  return OMX_ErrorNone;
  }
//}}}
