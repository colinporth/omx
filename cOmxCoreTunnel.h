// cOmxCoreTunnel.h
#pragma once
#include "cOmxCoreComponent.h"

class cOmxCoreTunnel {
public:
  cOmxCoreTunnel();
  ~cOmxCoreTunnel();

  void Initialize (cOmxCoreComponent* src_component, unsigned int src_port,
                   cOmxCoreComponent* dst_component, unsigned int dst_port);
  bool IsInitialized() const { return m_tunnel_set; }

  OMX_ERRORTYPE Establish (bool enable_ports = true, bool disable_ports = false);
  OMX_ERRORTYPE Deestablish (bool noWait = false);

private:
  cOmx* m_OMX;

  cOmxCoreComponent* m_src_component = nullptr;
  cOmxCoreComponent* m_dst_component = nullptr;

  unsigned int m_src_port = 0;
  unsigned int m_dst_port = 0;
  bool m_tunnel_set = false;
  };
