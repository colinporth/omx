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
void cOmxCoreTunnel::initialize (cOmxCoreComponent* srcComponent, unsigned int srcPort,
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

  if (mSrcComponent->GetState() == OMX_StateLoaded) {
    //{{{
    omxErr = mSrcComponent->SetStateForComponent (OMX_StateIdle);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  setting state to idle %s 0x%08x",
                 mSrcComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->GetComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->DisablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 mSrcPort, mSrcComponent->GetName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->GetComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->DisablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 mDstPort, mDstComponent->GetName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->GetComponent() && disable_ports) {
    //{{{
    omxErr = mSrcComponent->WaitForCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 mDstPort, mSrcComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->GetComponent() && disable_ports) {
    //{{{
    omxErr = mDstComponent->WaitForCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 mDstPort, mDstComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->GetComponent() && mDstComponent->GetComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mSrcComponent->GetComponent(), mSrcPort,
                                 mDstComponent->GetComponent(), mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish setup tunnel src %s port %d dst %s port %d 0x%08x\n",
                 mSrcComponent->GetName().c_str(), mSrcPort,
                 mDstComponent->GetName().c_str(), mDstPort, (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  else {
    cLog::log (LOGERROR, "cOmxCoreTunnel::Establish could not setup tunnel\n");
    return OMX_ErrorUndefined;
    }

  mTunnelSet = true;

  if (mSrcComponent->GetComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->EnablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->GetComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->EnablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                           mDstPort, mDstComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->GetComponent() && enable_ports) {
    //{{{
    omxErr = mDstComponent->WaitForCommand(OMX_CommandPortEnable, mDstPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;

    if (mDstComponent->GetState() == OMX_StateLoaded) {
      omxErr = mDstComponent->SetStateForComponent (OMX_StateIdle);
      if (omxErr != OMX_ErrorNone) {
        cLog::log (LOGERROR, "cOmxCoreComponent::Establish  setting state to idle %s 0x%08x",
                             mSrcComponent->GetName().c_str(), (int)omxErr);
        return omxErr;
        }
      }
    }
    //}}}
  if (mSrcComponent->GetComponent() && enable_ports) {
    //{{{
    omxErr = mSrcComponent->WaitForCommand (OMX_CommandPortEnable, mSrcPort);
    if (omxErr != OMX_ErrorNone)
      return omxErr;
    }
    //}}}

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreTunnel::deEstablish (bool noWait) {

  if (!mSrcComponent || !mDstComponent || !isInitialized())
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omxErr = OMX_ErrorNone;

  if (mSrcComponent->GetComponent()) {
    //{{{
    omxErr = mSrcComponent->DisablePort (mSrcPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                           mSrcPort, mSrcComponent->GetName().c_str(), (int)omxErr);
    }
    //}}}
  if (mDstComponent->GetComponent()) {
    //{{{
    omxErr = mDstComponent->DisablePort (mDstPort, false);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                           mDstPort, mDstComponent->GetName().c_str(), (int)omxErr);
    }
    //}}}
  if (mSrcComponent->GetComponent()) {
    //{{{
    omxErr = mSrcComponent->WaitForCommand (OMX_CommandPortDisable, mSrcPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                           mDstPort, mSrcComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mDstComponent->GetComponent()) {
    //{{{
    omxErr = mDstComponent->WaitForCommand (OMX_CommandPortDisable, mDstPort);
    if (omxErr != OMX_ErrorNone) {
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                           mDstPort, mDstComponent->GetName().c_str(), (int)omxErr);
      return omxErr;
      }
    }
    //}}}
  if (mSrcComponent->GetComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mSrcComponent->GetComponent(), mSrcPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp src %s port %d 0x%08x\n",
                 mSrcComponent->GetName().c_str(), mSrcPort, (int)omxErr);
    }
    //}}}
  if (mDstComponent->GetComponent()) {
    //{{{
    omxErr = m_OMX->setupTunnel (mDstComponent->GetComponent(), mDstPort, NULL, 0);
    if (omxErr != OMX_ErrorNone)
      cLog::log  (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp dst %s port %d 0x%08x\n",
                  mDstComponent->GetName().c_str(), mDstPort, (int)omxErr);
    }
    //}}}

  mTunnelSet = false;
  return OMX_ErrorNone;
  }
//}}}
