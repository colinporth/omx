//{{{  includes
#pragma once

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
  void getDownmixMatrix (float* downmix);

  enum PCMChannels* setInputFormat (unsigned int channels, enum PCMChannels* channelMap,
                                    unsigned int sampleSize, unsigned int sampleRate,
                                    enum PCMLayout channelLayout, bool dontnormalize);
  void setOutputFormat (unsigned int channels, enum PCMChannels* channelMap, bool ignoreLayout);

  void reset();

private:
  struct PCMMapInfo* resolveChannel (enum PCMChannels channel, float level, bool ifExists, std::vector<enum PCMChannels> path, struct PCMMapInfo *tablePtr);
  void resolveChannels();

  void buildMap();
  void dumpMap (std::string info, int unsigned channels, enum PCMChannels *channelMap);

  std::string pcmChannelStr (enum PCMChannels ename);

  // vars
  bool m_inSet = false;
  bool m_outSet = false;
  enum PCMLayout m_channelLayout;
  unsigned int m_inChannels = 0;
  unsigned int m_outChannels = 0;

  enum PCMChannels m_inMap [PCM_MAX_CH];
  enum PCMChannels m_outMap[PCM_MAX_CH];
  enum PCMChannels m_layoutMap[PCM_MAX_CH + 1];

  bool m_ignoreLayout = false;
  bool m_useable [PCM_MAX_CH];
  struct PCMMapInfo  m_lookupMap[PCM_MAX_CH + 1][PCM_MAX_CH + 1];
  int m_counts[PCM_MAX_CH];

  bool m_dontnormalize = false;
  };
