// cOmxCoreTunnel.h
//{{{  includes
#pragma once
#include "cOmxCoreComponent.h"
//}}}

class cOmxCoreTunnel {
public:
  cOmxCoreTunnel();
  ~cOmxCoreTunnel();

  void initialize (cOmxCoreComponent* srcComponent, unsigned int srcPort,
                   cOmxCoreComponent* dstComponent, unsigned int dstPort);
  bool isInitialized() const { return mTunnelSet; }

  OMX_ERRORTYPE establish (bool enablePorts = true, bool disablePorts = false);
  OMX_ERRORTYPE deEstablish (bool noWait = false);

private:
  cOmx* m_OMX;

  cOmxCoreComponent* mSrcComponent = nullptr;
  cOmxCoreComponent* mDstComponent = nullptr;

  unsigned int mSrcPort = 0;
  unsigned int mDstPort = 0;
  bool mTunnelSet = false;
  };
