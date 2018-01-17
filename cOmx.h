// cOmx.h - Omx singleton
//includes
#pragma once

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Index.h>
#include <IL/OMX_Image.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

class cOmx {
public:
  cOmx() { ::OMX_Init(); }
  virtual ~cOmx() { ::OMX_Deinit(); }
  };
