// cOmxTunnel.h
//{{{  includes
#pragma once
#include "cOmxCore.h"
//}}}

class cOmxTunnel {
public:
  cOmxTunnel();
  ~cOmxTunnel();

  void init (cOmxCore* srcComponent, unsigned int srcPort,
             cOmxCore* dstComponent, unsigned int dstPort);
  bool isInit() const { return mTunnelSet; }

  OMX_ERRORTYPE establish (bool enablePorts = true, bool disablePorts = false);
  OMX_ERRORTYPE deEstablish (bool noWait = false);

private:
  cOmxCore* mSrcComponent = nullptr;
  cOmxCore* mDstComponent = nullptr;

  unsigned int mSrcPort = 0;
  unsigned int mDstPort = 0;
  bool mTunnelSet = false;
  };
