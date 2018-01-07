// cPcmRemap.cpp
//{{{  includes
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <cstdlib>
#include <cassert>
#include <climits>
#include <cmath>

#include "cPcmRemap.h"
#include "../shared/utils/cLog.h"

using namespace std;
//}}}
#define PCM_MAX_MIX 3

//{{{
enum PCMChannels PCMLayoutMap[PCM_MAX_LAYOUT][PCM_MAX_CH + 1] = {
  /* 2.0 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_INVALID},
  /* 2.1 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_LOW_FREQUENCY, PCM_INVALID},
  /* 3.0 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_INVALID},
  /* 3.1 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_LOW_FREQUENCY, PCM_INVALID},
  /* 4.0 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_INVALID},
  /* 4.1 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_LOW_FREQUENCY, PCM_INVALID},
  /* 5.0 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_INVALID},
  /* 5.1 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_LOW_FREQUENCY, PCM_INVALID},
  /* 7.0 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_SIDE_LEFT, PCM_SIDE_RIGHT, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_INVALID},
  /* 7.1 */ {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_SIDE_LEFT, PCM_SIDE_RIGHT, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_LOW_FREQUENCY, PCM_INVALID}
  };
//}}}
//{{{
const struct PCMMapInfo PCMDownmixTable[PCM_MAX_CH][PCM_MAX_MIX] = {
  /* PCM_FRONT_LEFT */       { {PCM_INVALID} },
  /* PCM_FRONT_RIGHT */      { {PCM_INVALID} },
  /* PCM_FRONT_CENTER */     { {PCM_FRONT_LEFT_OF_CENTER, 1.0}, {PCM_FRONT_RIGHT_OF_CENTER, 1.0}, {PCM_INVALID} },
  /* PCM_LOW_FREQUENCY recomends +10db but horrible clipping disabled we set this to 1.0 */
                             { {PCM_FRONT_LEFT, 1.0}, {PCM_FRONT_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_BACK_LEFT */        { {PCM_FRONT_LEFT, 1.0}, {PCM_INVALID} },
  /* PCM_BACK_RIGHT */       { {PCM_FRONT_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_FRONT_LEFT_OF_CENTER */ { {PCM_FRONT_LEFT, 1.0}, {PCM_FRONT_CENTER, 1.0, true}, {PCM_INVALID} },
  /* PCM_FRONT_RIGHT_OF_CENTER */ { {PCM_FRONT_RIGHT, 1.0}, {PCM_FRONT_CENTER, 1.0, true}, {PCM_INVALID} },
  /* PCM_BACK_CENTER */      { {PCM_BACK_LEFT, 1.0}, {PCM_BACK_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_SIDE_LEFT */        { {PCM_FRONT_LEFT, 1.0}, {PCM_BACK_LEFT, 1.0}, {PCM_INVALID} },
  /* PCM_SIDE_RIGHT */       { {PCM_FRONT_RIGHT, 1.0}, {PCM_BACK_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_FRONT_LEFT */   { {PCM_FRONT_LEFT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_FRONT_RIGHT */  { {PCM_FRONT_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_FRONT_CENTER */ { {PCM_TOP_FRONT_LEFT, 1.0}, {PCM_TOP_FRONT_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_CENTER */       { {PCM_TOP_FRONT_LEFT, 1.0}, {PCM_TOP_FRONT_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_BACK_LEFT */    { {PCM_BACK_LEFT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_BACK_RIGHT */   { {PCM_BACK_RIGHT, 1.0}, {PCM_INVALID} },
  /* PCM_TOP_BACK_CENTER */  { {PCM_TOP_BACK_LEFT, 1.0}, {PCM_TOP_BACK_RIGHT, 1.0}, {PCM_INVALID}  }
  };
//}}}

//{{{
int round_int (double x) {

  assert (x > static_cast<double>(INT_MIN / 2) - 1.0);
  assert (x < static_cast <double>(INT_MAX / 2) + 1.0);
  return floor (x + 0.5f);
  }
//}}}

//{{{
cPcmRemap::cPcmRemap() : 
    m_inSet(false), m_outSet(false),
    m_inChannels(0), m_outChannels(0), m_inSampleSize(0), m_ignoreLayout(false), m_buf(NULL),
    m_bufsize(0), m_attenuation (1.0), m_attenuationInc(0.0), m_attenuationMin(1.0),
    m_sampleRate  (48000.0), m_holdCounter (0), m_limiterEnabled(false) {

  Dispose();
  }
//}}}
//{{{
cPcmRemap::~cPcmRemap() {
  Dispose();
  }
//}}}

//{{{
void cPcmRemap::Reset() {

  m_inSet  = false;
  m_outSet = false;
  Dispose();
  }
//}}}
//{{{
void cPcmRemap::Dispose() {

  free (m_buf);
  m_buf = NULL;
  m_bufsize = 0;
  }
//}}}

//{{{
/* resolves the channels recursively and returns the new index of tablePtr */
struct PCMMapInfo* cPcmRemap::ResolveChannel (enum PCMChannels channel, float level, bool ifExists,
                                              vector<enum PCMChannels> path, struct PCMMapInfo *tablePtr) {

  if (channel == PCM_INVALID) 
    return tablePtr;

  /* if its a 1 to 1 mapping, return */
  if (m_useable[channel]) {
    tablePtr->channel = channel;
    tablePtr->level   = level;

    ++tablePtr;
    tablePtr->channel = PCM_INVALID;
    return tablePtr;
    }
  else if (ifExists)
    level /= 2;

  vector<enum PCMChannels>::iterator itt;
  for (auto info = PCMDownmixTable[channel]; info->channel != PCM_INVALID; ++info) {
    /* make sure we are not about to recurse into ourself */
    bool found = false;
    for(itt = path.begin(); itt != path.end(); ++itt)
      if (*itt == info->channel) {
        found = true;
        break;
        }

    if (found)
      continue;

    path.push_back(channel);
    float  l = (info->level * (level / 100)) * 100;
    tablePtr = ResolveChannel(info->channel, l, info->ifExists, path, tablePtr);
    path.pop_back();
    }

  return tablePtr;
  }
//}}}
//{{{
void cPcmRemap::ResolveChannels() {
// build lookup table without extra adjustments, useful if we simply
//  want to find out which channels are active. For final adjustments, BuildMap() is used.

  unsigned int in_ch, out_ch;
  bool hasSide = false;
  bool hasBack = false;

  memset(m_useable, 0, sizeof(m_useable));

  if (!m_outSet) {
    /* Output format is not known yet, assume the full configured map.
     * Note that m_ignoreLayout-using callers normally ignore the result of
     * this function when !m_outSet, when it is called only for an advice for
     * the caller of SetInputFormat about the best possible output map, and
     * they can still set their output format arbitrarily in their call to
     * SetOutputFormat. */
    for (enum PCMChannels *chan = PCMLayoutMap[m_channelLayout]; *chan != PCM_INVALID; ++chan)
         m_useable[*chan] = true;
  }
  else if (m_ignoreLayout) {
    for(out_ch = 0; out_ch < m_outChannels; ++out_ch)
      m_useable[m_outMap[out_ch]] = true;
  }
  else {
    /* figure out what channels we have and can use */
    for(enum PCMChannels *chan = PCMLayoutMap[m_channelLayout]; *chan != PCM_INVALID; ++chan) {
      for(out_ch = 0; out_ch < m_outChannels; ++out_ch)
        if (m_outMap[out_ch] == *chan) {
          m_useable[*chan] = true;
          break;
        }
    }
  }

  /* force mono audio to front left and front right */
  if (!m_ignoreLayout && m_inChannels == 1 && m_inMap[0] == PCM_FRONT_CENTER
      && m_useable[PCM_FRONT_LEFT] && m_useable[PCM_FRONT_RIGHT]) {
    cLog::log(LOGINFO1, "cPcmRemap: Mapping mono audio to front left and front right");
    m_useable[PCM_FRONT_CENTER] = false;
    m_useable[PCM_FRONT_LEFT_OF_CENTER] = false;
    m_useable[PCM_FRONT_RIGHT_OF_CENTER] = false;
  }

  /* see if our input has side/back channels */
  for(in_ch = 0; in_ch < m_inChannels; ++in_ch)
    switch(m_inMap[in_ch]) {
      case PCM_SIDE_LEFT:
      case PCM_SIDE_RIGHT:
        hasSide = true;
        break;

      case PCM_BACK_LEFT:
      case PCM_BACK_RIGHT:
        hasBack = true;
        break;

      default:;
    }

  /* if our input has side, and not back channels, and our output doesnt have side channels */
  if (hasSide && !hasBack && (!m_useable[PCM_SIDE_LEFT] || !m_useable[PCM_SIDE_RIGHT])) {
    cLog::log(LOGINFO1, "cPcmRemap: Forcing side channel map to back channels");
    for(in_ch = 0; in_ch < m_inChannels; ++in_ch)
           if (m_inMap[in_ch] == PCM_SIDE_LEFT ) m_inMap[in_ch] = PCM_BACK_LEFT;
      else if (m_inMap[in_ch] == PCM_SIDE_RIGHT) m_inMap[in_ch] = PCM_BACK_RIGHT;
  }

  /* resolve all the channels */
  struct PCMMapInfo table[PCM_MAX_CH + 1], *info, *dst;
  vector<enum PCMChannels> path;

  for (int i = 0; i < PCM_MAX_CH + 1; i++) {
    for (int j = 0; j < PCM_MAX_CH + 1; j++)
      m_lookupMap[i][j].channel = PCM_INVALID;
  }

  memset(m_counts, 0, sizeof(m_counts));
  for (in_ch = 0; in_ch < m_inChannels; ++in_ch) {
    for (int i = 0; i < PCM_MAX_CH + 1; i++)
      table[i].channel = PCM_INVALID;

    ResolveChannel(m_inMap[in_ch], 1.0f, false, path, table);
    for (info = table; info->channel != PCM_INVALID; ++info) {
      /* find the end of the table */
      for (dst = m_lookupMap[info->channel]; dst->channel != PCM_INVALID; ++dst);

      /* append it to the table and set its input offset */
      dst->channel   = m_inMap[in_ch];
      dst->in_offset = in_ch * 2;
      dst->level     = info->level;
      m_counts[dst->channel]++;
    }
  }
}
//}}}

//{{{
void cPcmRemap::BuildMap() {
// builds a lookup table to convert from the input mapping to the output
// mapping, this decreases the amount of work per sample to remap it

  struct PCMMapInfo *dst;
  unsigned int out_ch;
  if (!m_inSet || !m_outSet)
    return;

  m_inStride  = m_inSampleSize * m_inChannels ;
  m_outStride = m_inSampleSize * m_outChannels;

  /* see if we need to normalize the levels */
  bool dontnormalize = m_dontnormalize;
  cLog::log(LOGINFO1, "cPcmRemap: Downmix normalization is %s", (dontnormalize ? "disabled" : "enabled"));

  ResolveChannels();

  /* convert the levels into RMS values */
  float loudest    = 0.0;
  bool hasLoudest = false;

  for (out_ch = 0; out_ch < m_outChannels; ++out_ch) {
    float scale = 0;
    int count = 0;
    for(dst = m_lookupMap[m_outMap[out_ch]]; dst->channel != PCM_INVALID; ++dst) {
      dst->copy  = false;
      dst->level = dst->level / sqrt((float)m_counts[dst->channel]);
      scale     += dst->level;
      ++count;
      }

    /* if there is only 1 channel to mix, and the level is 1.0, then just copy the channel */
    dst = m_lookupMap[m_outMap[out_ch]];
    if (count == 1 && dst->level > 0.99 && dst->level < 1.01)
      dst->copy = true;

    /* normalize the levels if it is turned on */
    if (!dontnormalize)
      for(dst = m_lookupMap[m_outMap[out_ch]]; dst->channel != PCM_INVALID; ++dst) {
        dst->level /= scale;
        /* find the loudest output level we have that is not 1-1 */
        if (dst->level < 1.0 && loudest < dst->level) {
          loudest    = dst->level;
          hasLoudest = true;
          }
        }
    }

  /* adjust the channels that are too loud */
  for(out_ch = 0; out_ch < m_outChannels; ++out_ch) {
    string s = "", f;
    for(dst = m_lookupMap[m_outMap[out_ch]]; dst->channel != PCM_INVALID; ++dst) {
      if (hasLoudest && dst->copy) {
        dst->level = loudest;
        dst->copy  = false;
        }

      f = PCMChannelStr(dst->channel); // + dst->level, dst->copy ? "*" : "");
      s += f;
      }
    cLog::log(LOGINFO1, "cPcmRemap: %s = %s\n", PCMChannelStr(m_outMap[out_ch]).c_str(), s.c_str());
    }
  }
//}}}
//{{{
void cPcmRemap::DumpMap (string info, unsigned int channels, enum PCMChannels *channelMap) {

  if (channelMap == NULL) {
    cLog::log(LOGINFO, "cPcmRemap: %s channel map: NULL", info.c_str());
    return;
    }

  string mapping;
  for (unsigned int i = 0; i < channels; ++i)
    mapping += ((i == 0) ? "" : ",") + PCMChannelStr(channelMap[i]);

  cLog::log(LOGINFO, "cPcmRemap: %s channel map: %s\n", info.c_str(), mapping.c_str());
  }
//}}}

//{{{
enum PCMChannels* cPcmRemap::SetInputFormat (unsigned int channels, enum PCMChannels *channelMap,
                                             unsigned int sampleSize, unsigned int sampleRate,
                                             enum PCMLayout channelLayout, bool dontnormalize) {
// sets the input format, and returns the requested channel layout */

  m_inChannels   = channels;
  m_inSampleSize = sampleSize;
  m_sampleRate   = sampleRate;
  m_inSet        = channelMap != NULL;
  if (channelMap)
    memcpy(m_inMap, channelMap, sizeof(enum PCMChannels) * channels);

  /* get the audio layout, and count the channels in it */
  m_channelLayout = channelLayout;
  m_dontnormalize = dontnormalize;
  if (m_channelLayout >= PCM_MAX_LAYOUT) m_channelLayout = PCM_LAYOUT_2_0;

  DumpMap("I", channels, channelMap);
  BuildMap();

  /* now remove the empty channels from PCMLayoutMap;
   * we don't perform upmixing so we want the minimum amount of those */
  if (channelMap) {
    if (!m_outSet)
      ResolveChannels(); /* Do basic channel resolving to find out the empty channels;
                          * If m_outSet == true, this was done already by BuildMap() above */
    int i = 0;
    for (enum PCMChannels *chan = PCMLayoutMap[m_channelLayout]; *chan != PCM_INVALID; ++chan)
      if (m_lookupMap[*chan][0].channel != PCM_INVALID) {
        /* something is mapped here, so add the channel */
        m_layoutMap[i++] = *chan;
        }
    m_layoutMap[i] = PCM_INVALID;
    } 
  else
    memcpy(m_layoutMap, PCMLayoutMap[m_channelLayout], sizeof(PCMLayoutMap[m_channelLayout]));

  m_attenuation = 1.0;
  m_attenuationInc = 1.0;
  m_holdCounter = 0;

  return m_layoutMap;
  }
//}}}
//{{{
void cPcmRemap::SetOutputFormat (unsigned int channels, enum PCMChannels *channelMap, bool ignoreLayout/* = false */) {
/* sets the output format supported by the audio renderer */

  m_outChannels = channels;
  m_outSet= channelMap != NULL;
  m_ignoreLayout = ignoreLayout;
  if (channelMap)
    memcpy (m_outMap, channelMap, sizeof(enum PCMChannels) * channels);

  DumpMap ("O", channels, channelMap);
  BuildMap();

  m_attenuation = 1.0;
  m_attenuationInc = 1.0;
  m_holdCounter = 0;
  }
//}}}

//{{{
string cPcmRemap::PCMChannelStr (enum PCMChannels ename) {

  const char* PCMChannelName[] = { "FL",   "FR",   "CE",  "LFE", "BL",  "BR", 
                                   "FLOC", "FROC", "BC",  "SL"   "SR",
                                   "TFL",  "TFR",  "TFC", "TC",  "TBL", "TBR", "TBC" };

  int namepos = (int)ename;
  string namestr;
  if (namepos < 0 || namepos >= (int)(sizeof(PCMChannelName) / sizeof(const char*)))
    namestr = "UNKNOWN CHANNEL"; // namepos);
  else
    namestr = PCMChannelName[namepos];

  return namestr;
  }
//}}}
//{{{
void cPcmRemap::GetDownmixMatrix (float *downmix) {

  for (int i = 0; i < 8*8; i++)
    downmix[i] = 0.0f;

  for (unsigned int ch = 0; ch < m_outChannels; ch++) {
    struct PCMMapInfo *info = m_lookupMap[m_outMap[ch]];
    if (info->channel == PCM_INVALID)
      continue;

    for(; info->channel != PCM_INVALID; info++)
      downmix[8*ch + (info->in_offset>>1)] = info->level;
    }
  }
//}}}
