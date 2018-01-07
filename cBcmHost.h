#pragma once
extern "C" {
  #include <bcm_host.h>
  }

class cBcmHost {
public:
  cBcmHost() { ::bcm_host_init(); }
  virtual ~cBcmHost() { ::bcm_host_deinit(); };

  //{{{
  int32_t graphics_get_display_size (const uint16_t display_number, uint32_t* width, uint32_t* height)
    { return ::graphics_get_display_size(display_number, width, height); };
  //}}}
  //{{{
  int vc_tv_hdmi_get_supported_modes_new (HDMI_RES_GROUP_T group, 
                                          TV_SUPPORTED_MODE_NEW_T* supported_modes,
                                          uint32_t max_supported_modes,
                                          HDMI_RES_GROUP_T* preferred_group,
                                          uint32_t* preferred_mode) {
    return ::vc_tv_hdmi_get_supported_modes_new (group, supported_modes, max_supported_modes, 
                                                 preferred_group, preferred_mode); };
  //}}}
  //{{{
  int vc_tv_hdmi_power_on_explicit_new (HDMI_MODE_T mode, HDMI_RES_GROUP_T group, uint32_t code)
    { return ::vc_tv_hdmi_power_on_explicit_new(mode, group, code); };
  //}}}
  //{{{

  int vc_tv_hdmi_set_property (const HDMI_PROPERTY_PARAM_T *property)
    { return ::vc_tv_hdmi_set_property(property); }
  //}}}

  //{{{

  int vc_tv_get_display_state (TV_DISPLAY_STATE_T* tvstate)
    { return ::vc_tv_get_display_state(tvstate); };
  //}}}
  //{{{
  int vc_tv_show_info (uint32_t show)
    { return ::vc_tv_show_info(show); };
  //}}}
  //{{{

  int vc_gencmd (char* response, int maxlen, const char* string)
    { return ::vc_gencmd(response, maxlen, string); };
  //}}}

  //{{{
  void vc_tv_register_callback (TVSERVICE_CALLBACK_T callback, void* callback_data)
    { ::vc_tv_register_callback(callback, callback_data); };
  //}}}
  //{{{
  void vc_tv_unregister_callback (TVSERVICE_CALLBACK_T callback)
    { ::vc_tv_unregister_callback(callback); };
  //}}}
  //{{{
  void vc_cec_register_callback (CECSERVICE_CALLBACK_T callback, void* callback_data)
    { ::vc_cec_register_callback(callback, callback_data); };
  //}}}
  //virtual void vc_cec_unregister_callback(CECSERVICE_CALLBACK_T callback)
  //  { ::vc_cec_unregister_callback(callback); };

  //{{{
  DISPMANX_DISPLAY_HANDLE_T vc_dispmanx_display_open (uint32_t device )
     { return ::vc_dispmanx_display_open(device); };
  //}}}
  //{{{

  DISPMANX_UPDATE_HANDLE_T vc_dispmanx_update_start (int32_t priority )
    { return ::vc_dispmanx_update_start(priority); };
  //}}}
  //{{{
  DISPMANX_ELEMENT_HANDLE_T vc_dispmanx_element_add (DISPMANX_UPDATE_HANDLE_T update, DISPMANX_DISPLAY_HANDLE_T display,
                                                             int32_t layer, const VC_RECT_T *dest_rect, DISPMANX_RESOURCE_HANDLE_T src,
                                                             const VC_RECT_T *src_rect, DISPMANX_PROTECTION_T protection,
                                                             VC_DISPMANX_ALPHA_T *alpha,
                                                             DISPMANX_CLAMP_T *clamp, DISPMANX_TRANSFORM_T transform )
    { return ::vc_dispmanx_element_add(update, display, layer, dest_rect, src, src_rect, protection, alpha, clamp, transform); };
  //}}}

  //{{{
  int vc_dispmanx_update_submit_sync (DISPMANX_UPDATE_HANDLE_T update )
    { return ::vc_dispmanx_update_submit_sync(update); };
  //}}}
  //{{{
  int vc_dispmanx_element_remove (DISPMANX_UPDATE_HANDLE_T update, DISPMANX_ELEMENT_HANDLE_T element )
    { return ::vc_dispmanx_element_remove(update, element); };
  //}}}

  //{{{
  int vc_dispmanx_display_close (DISPMANX_DISPLAY_HANDLE_T display )
    { return ::vc_dispmanx_display_close(display); };
  //}}}
  //{{{
  int vc_dispmanx_display_get_info (DISPMANX_DISPLAY_HANDLE_T display, DISPMANX_MODEINFO_T* pinfo )
    { return ::vc_dispmanx_display_get_info(display, pinfo); };
  //}}}
  //{{{
  int vc_dispmanx_display_set_background (DISPMANX_UPDATE_HANDLE_T update, 
                                          DISPMANX_DISPLAY_HANDLE_T display,
                                          uint8_t red, uint8_t green, uint8_t blue )
    { return ::vc_dispmanx_display_set_background(update, display, red, green, blue); };
  //}}}

  //{{{
  int vc_tv_hdmi_audio_supported (uint32_t audio_format, uint32_t num_channels,
                                  EDID_AudioSampleRate fs, uint32_t bitrate)
  { return ::vc_tv_hdmi_audio_supported(audio_format, num_channels, fs, bitrate); };
  //}}}
  };
