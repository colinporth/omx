// cOmxCoreTunnel.cpp
//{{{  includes
#include "cOmxCoreTunnel.h"

#include "cLog.h"
#include "cOmxClock.h"
//}}}

//{{{
cOmxCoreTunnel::cOmxCoreTunnel() {
  m_src_component = NULL;
  m_dst_component = NULL;
  m_src_port = 0;
  m_dst_port = 0;
  m_tunnel_set = false;
  m_OMX = cOmx::GetOMX();
  }
//}}}
cOmxCoreTunnel::~cOmxCoreTunnel() {}

//{{{
void cOmxCoreTunnel::Initialize (cOmxCoreComponent* src_component, unsigned int src_port,
                                 cOmxCoreComponent* dst_component, unsigned int dst_port) {
  m_src_component = src_component;
  m_src_port = src_port;
  m_dst_component = dst_component;
  m_dst_port = dst_port;
  }
//}}}

//{{{
OMX_ERRORTYPE cOmxCoreTunnel::Establish (bool enable_ports /* = true */, bool disable_ports /* = false */) {

  if (!m_src_component || !m_dst_component)
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if (m_src_component->GetState() == OMX_StateLoaded) {
    //{{{
    omx_err = m_src_component->SetStateForComponent (OMX_StateIdle);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish  setting state to idle %s 0x%08x",
                 m_src_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_src_component->GetComponent() && disable_ports) {
    //{{{
    omx_err = m_src_component->DisablePort (m_src_port, false);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
    }
    //}}}
  if (m_dst_component->GetComponent() && disable_ports) {
    //{{{
    omx_err = m_dst_component->DisablePort (m_dst_port, false);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish disable port %d %s 0x%08x",
                 m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
    }
    //}}}
  if (m_src_component->GetComponent() && disable_ports) {
    //{{{
    omx_err = m_src_component->WaitForCommand (OMX_CommandPortDisable, m_src_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 m_dst_port, m_src_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_dst_component->GetComponent() && disable_ports) {
    //{{{
    omx_err = m_dst_component->WaitForCommand (OMX_CommandPortDisable, m_dst_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish  WaitForCommand port %d %s 0x%08x",
                 m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_src_component->GetComponent() && m_dst_component->GetComponent()) {
    //{{{
    omx_err = m_OMX->OMX_SetupTunnel (m_src_component->GetComponent(), m_src_port,
                                      m_dst_component->GetComponent(), m_dst_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish setup tunnel src %s port %d dst %s port %d 0x%08x\n",
                 m_src_component->GetName().c_str(), m_src_port,
                 m_dst_component->GetName().c_str(), m_dst_port, (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  else {
    cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish could not setup tunnel\n");
    return OMX_ErrorUndefined;
    }

  m_tunnel_set = true;

  if (m_src_component->GetComponent() && enable_ports) {
    //{{{
    omx_err = m_src_component->EnablePort (m_src_port, false);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                 m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_dst_component->GetComponent() && enable_ports) {
    //{{{
    omx_err = m_dst_component->EnablePort (m_dst_port, false);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Establish  enable port %d %s 0x%08x",
                 m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_dst_component->GetComponent() && enable_ports) {
    //{{{
    omx_err = m_dst_component->WaitForCommand(OMX_CommandPortEnable, m_dst_port);
    if (omx_err != OMX_ErrorNone)
      return omx_err;

    if (m_dst_component->GetState() == OMX_StateLoaded) {
      omx_err = m_dst_component->SetStateForComponent (OMX_StateIdle);
      if (omx_err != OMX_ErrorNone) {
        cLog::Log (LOGERROR, "cOmxCoreComponent::Establish  setting state to idle %s 0x%08x",
                   m_src_component->GetName().c_str(), (int)omx_err);
        return omx_err;
        }
      }
    }
    //}}}
  if (m_src_component->GetComponent() && enable_ports) {
    //{{{
    omx_err = m_src_component->WaitForCommand (OMX_CommandPortEnable, m_src_port);
    if (omx_err != OMX_ErrorNone)
      return omx_err;
    }
    //}}}

  return OMX_ErrorNone;
  }
//}}}
//{{{
OMX_ERRORTYPE cOmxCoreTunnel::Deestablish (bool noWait) {

  if (!m_src_component || !m_dst_component || !IsInitialized())
    return OMX_ErrorUndefined;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if (m_src_component->GetComponent()) {
    //{{{
    omx_err = m_src_component->DisablePort (m_src_port, false);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                 m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
    }
    //}}}
  if (m_dst_component->GetComponent()) {
    //{{{
    omx_err = m_dst_component->DisablePort (m_dst_port, false);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Deestablish disable port %d %s 0x%08x",
                 m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
    }
    //}}}
  if (m_src_component->GetComponent()) {
    //{{{
    omx_err = m_src_component->WaitForCommand (OMX_CommandPortDisable, m_src_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                 m_dst_port, m_src_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_dst_component->GetComponent()) {
    //{{{
    omx_err = m_dst_component->WaitForCommand (OMX_CommandPortDisable, m_dst_port);
    if (omx_err != OMX_ErrorNone) {
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Deestablish WaitForCommand port %d %s 0x%08x",
                 m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
      return omx_err;
      }
    }
    //}}}
  if (m_src_component->GetComponent()) {
    //{{{
    omx_err = m_OMX->OMX_SetupTunnel (m_src_component->GetComponent(), m_src_port, NULL, 0);
    if (omx_err != OMX_ErrorNone)
      cLog::Log (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp src %s port %d 0x%08x\n",
                 m_src_component->GetName().c_str(), m_src_port, (int)omx_err);
    }
    //}}}
  if (m_dst_component->GetComponent()) {
    //{{{
    omx_err = m_OMX->OMX_SetupTunnel (m_dst_component->GetComponent(), m_dst_port, NULL, 0);
    if (omx_err != OMX_ErrorNone)
      cLog::Log  (LOGERROR, "cOmxCoreTunnel::Deestablish unset tunnel comp dst %s port %d 0x%08x\n",
                  m_dst_component->GetName().c_str(), m_dst_port, (int)omx_err);
    }
    //}}}

  m_tunnel_set = false;
  return OMX_ErrorNone;
  }
//}}}
