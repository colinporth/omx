// cOmx.h - OMX singleton wrapper
#pragma once

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Index.h>
#include <IL/OMX_Image.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

class cOmx {
public:
  static cOmx* GetOMX() {
    static cOmx static_dll_omx;
    return &static_dll_omx;
    }

  cOmx() { ::OMX_Init(); }
  virtual ~cOmx() { ::OMX_Deinit(); }

  OMX_ERRORTYPE OMX_GetHandle (OMX_HANDLETYPE *pHandle, OMX_STRING cComponentName,
                               OMX_PTR pAppData, OMX_CALLBACKTYPE *pCallBacks) {
    return ::OMX_GetHandle (pHandle, cComponentName, pAppData, pCallBacks);
    };

  OMX_ERRORTYPE OMX_FreeHandle (OMX_HANDLETYPE hComponent) {
    return ::OMX_FreeHandle (hComponent);
    };

  OMX_ERRORTYPE OMX_GetComponentsOfRole (OMX_STRING role, OMX_U32* pNumComps, OMX_U8** compNames) {
    return ::OMX_GetComponentsOfRole (role, pNumComps, compNames);
    };

  OMX_ERRORTYPE OMX_GetRolesOfComponent (OMX_STRING compName, OMX_U32* pNumRoles, OMX_U8** roles) {
    return ::OMX_GetRolesOfComponent (compName, pNumRoles, roles);
    };

  OMX_ERRORTYPE OMX_ComponentNameEnum (OMX_STRING cComponentName, OMX_U32 nNameLength, OMX_U32 nIndex) {
    return ::OMX_ComponentNameEnum (cComponentName, nNameLength, nIndex);
    };

  OMX_ERRORTYPE OMX_SetupTunnel (OMX_HANDLETYPE hOutput, OMX_U32 nPortOutput,
                                 OMX_HANDLETYPE hInput, OMX_U32 nPortInput) {
    return ::OMX_SetupTunnel (hOutput, nPortOutput, hInput, nPortInput);
    };

  };
