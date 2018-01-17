// cOmxTunnel.h
//{{{  includes
#pragma once
#include "cOmxCore.h"
//}}}

class cOmxTunnel {
public:
  void init (cOmxCore* srcComponent, int srcPort, cOmxCore* dstComponent, int dstPort);
  bool isInit() const { return mTunnelSet; }

  OMX_ERRORTYPE establish (bool enablePorts = true, bool disablePorts = false);
  OMX_ERRORTYPE deEstablish (bool noWait = false);

private:
  cOmxCore* mSrcComponent = nullptr;
  cOmxCore* mDstComponent = nullptr;

  int mSrcPort = 0;
  int mDstPort = 0;
  bool mTunnelSet = false;
  };
