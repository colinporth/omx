#pragma once
//{{{  includes
#include <stdint.h>
#include <vector>
#include <string>
//}}}

#define PCM_MAX_CH 18
//{{{
enum PCMChannels
{
  PCM_INVALID = -1,
  PCM_FRONT_LEFT,
  PCM_FRONT_RIGHT,
  PCM_FRONT_CENTER,
  PCM_LOW_FREQUENCY,
  PCM_BACK_LEFT,
  PCM_BACK_RIGHT,
  PCM_FRONT_LEFT_OF_CENTER,
  PCM_FRONT_RIGHT_OF_CENTER,
  PCM_BACK_CENTER,
  PCM_SIDE_LEFT,
  PCM_SIDE_RIGHT,
  PCM_TOP_FRONT_LEFT,
  PCM_TOP_FRONT_RIGHT,
  PCM_TOP_FRONT_CENTER,
  PCM_TOP_CENTER,
  PCM_TOP_BACK_LEFT,
  PCM_TOP_BACK_RIGHT,
  PCM_TOP_BACK_CENTER
};
//}}}
#define PCM_MAX_LAYOUT 10
//{{{
enum PCMLayout
{
  PCM_LAYOUT_2_0 = 0,
  PCM_LAYOUT_2_1,
  PCM_LAYOUT_3_0,
  PCM_LAYOUT_3_1,
  PCM_LAYOUT_4_0,
  PCM_LAYOUT_4_1,
  PCM_LAYOUT_5_0,
  PCM_LAYOUT_5_1,
  PCM_LAYOUT_7_0,
  PCM_LAYOUT_7_1
};
//}}}
//{{{
struct PCMMapInfo
{
  enum  PCMChannels channel;
  float level;
  bool  ifExists;
  int   in_offset;
  bool  copy;
};
//}}}

class cPcmRemap {
public:
  cPcmRemap();
  ~cPcmRemap();

  void GetDownmixMatrix (float* downmix);
  float GetCurrentAttenuation() { return m_attenuationMin; }

  enum PCMChannels* SetInputFormat (unsigned int channels, enum PCMChannels* channelMap,
                                    unsigned int sampleSize, unsigned int sampleRate,
                                    enum PCMLayout channelLayout, bool dontnormalize);
  void SetOutputFormat (unsigned int channels, enum PCMChannels* channelMap, bool ignoreLayout = false);

  void Reset();

protected:
  void Dispose();

  struct PCMMapInfo* ResolveChannel(enum PCMChannels channel, float level, bool ifExists, std::vector<enum PCMChannels> path, struct PCMMapInfo *tablePtr);
  void ResolveChannels(); //!< Partial BuildMap(), just enough to see which output channels are active
  void BuildMap();
  void DumpMap (std::string info, int unsigned channels, enum PCMChannels *channelMap);

  std::string PCMChannelStr (enum PCMChannels ename);
  std::string PCMLayoutStr (enum PCMLayout ename);

  void CheckBufferSize (int size);
  void ProcessInput (void* data, void* out, unsigned int samples, float gain);
  void AddGain (float* buf, unsigned int samples, float gain);
  void ProcessLimiter (unsigned int samples, float gain);
  void ProcessOutput (void* out, unsigned int samples, float gain);

  // vars
  bool m_inSet;
  bool m_outSet;
  enum PCMLayout m_channelLayout;
  unsigned int m_inChannels;
  unsigned int m_outChannels;
  unsigned int m_inSampleSize;
  enum PCMChannels m_inMap [PCM_MAX_CH];
  enum PCMChannels m_outMap[PCM_MAX_CH];
  enum PCMChannels m_layoutMap[PCM_MAX_CH + 1];

  bool m_ignoreLayout;
  bool m_useable [PCM_MAX_CH];
  int m_inStride;
  int m_outStride;
  struct PCMMapInfo  m_lookupMap[PCM_MAX_CH + 1][PCM_MAX_CH + 1];
  int m_counts[PCM_MAX_CH];

  float* m_buf;
  int m_bufsize;

  float m_attenuation;
  float m_attenuationInc;
  float m_attenuationMin; //lowest attenuation value during a call of Remap(), used for the codec info

  float m_sampleRate;
  unsigned int m_holdCounter;
  bool m_limiterEnabled;
  bool m_dontnormalize;
  };
