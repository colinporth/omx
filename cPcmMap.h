// cPcmMap.h
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

class cPcmMap {
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
  bool mInSet = false;
  bool mOutSet = false;
  enum PCMLayout mChannelLayout;
  unsigned int mInChannels = 0;
  unsigned int mOutChannels = 0;

  enum PCMChannels mInMap [PCM_MAX_CH];
  enum PCMChannels mOutMap[PCM_MAX_CH];
  enum PCMChannels mLayoutMap[PCM_MAX_CH + 1];

  bool mIgnoreLayout = false;
  bool mUseable [PCM_MAX_CH];
  struct PCMMapInfo  mLookupMap[PCM_MAX_CH + 1][PCM_MAX_CH + 1];
  int mCounts[PCM_MAX_CH];

  bool mDontNormalize = false;
  };
