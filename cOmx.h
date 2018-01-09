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
  static cOmx* getOMX() {
    static cOmx mDllOmx;
    return &mDllOmx;
    }

  cOmx() { ::OMX_Init(); }
  virtual ~cOmx() { ::OMX_Deinit(); }

  OMX_ERRORTYPE getHandle (OMX_HANDLETYPE* handle, OMX_STRING componentName,
                           OMX_PTR appData, OMX_CALLBACKTYPE* callBacks) {
    return ::OMX_GetHandle (handle, componentName, appData, callBacks);
    };

  OMX_ERRORTYPE freeHandle (OMX_HANDLETYPE hComponent) {
    return ::OMX_FreeHandle (hComponent);
    };

  OMX_ERRORTYPE getComponentsOfRole (OMX_STRING role, OMX_U32* numComps, OMX_U8** compNames) {
    return ::OMX_GetComponentsOfRole (role, numComps, compNames);
    };

  OMX_ERRORTYPE getRolesOfComponent (OMX_STRING compName, OMX_U32* numRoles, OMX_U8** roles) {
    return ::OMX_GetRolesOfComponent (compName, numRoles, roles);
    };

  OMX_ERRORTYPE componentNameEnum (OMX_STRING componentName, OMX_U32 nameLength, OMX_U32 index) {
    return ::OMX_ComponentNameEnum (componentName, nameLength, index);
    };

  OMX_ERRORTYPE setupTunnel (OMX_HANDLETYPE output, OMX_U32 portOutput,
                             OMX_HANDLETYPE input, OMX_U32 portInput) {
    return ::OMX_SetupTunnel (output, portOutput, input, portInput);
    };

  };
