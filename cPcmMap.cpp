// cPcmMap.cpp
//{{{  includes
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <cstdlib>
#include <cassert>
#include <climits>
#include <cmath>

#include "cPcmMap.h"
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
void cPcmMap::getDownmixMatrix (float *downmix) {

  for (int i = 0; i < 8*8; i++)
    downmix[i] = 0.0f;

  for (unsigned int ch = 0; ch < mOutChannels; ch++) {
    struct PCMMapInfo* info = mLookupMap[mOutMap[ch]];
    if (info->channel == PCM_INVALID)
      continue;

    for(; info->channel != PCM_INVALID; info++)
      downmix[8*ch + (info->in_offset>>1)] = info->level;
    }
  }
//}}}

//{{{
enum PCMChannels* cPcmMap::setInputFormat (unsigned int channels, enum PCMChannels *channelMap,
                                           unsigned int sampleSize, unsigned int sampleRate,
                                           enum PCMLayout channelLayout, bool dontnormalize) {
// sets the input format, and returns the requested channel layout */

  mInChannels = channels;
  mInSet = (channelMap != NULL);
  if (channelMap)
    memcpy(mInMap, channelMap, sizeof(enum PCMChannels) * channels);

  // get the audio layout, and count the channels in it
  mChannelLayout = channelLayout;
  mDontNormalize = dontnormalize;
  if (mChannelLayout >= PCM_MAX_LAYOUT) mChannelLayout = PCM_LAYOUT_2_0;

  dumpMap ("in", channels, channelMap);
  buildMap();

  // now remove the empty channels from PCMLayoutMap;
  // we don't perform upmixing so we want the minimum amount of those
  if (channelMap) {
    // Do basic channel resolving to find out the empty channels;
    // If m_outSet == true, this was done already by BuildMap() above */
    if (!mOutSet)
      resolveChannels();

    int i = 0;
    for (enum PCMChannels *chan = PCMLayoutMap[mChannelLayout]; *chan != PCM_INVALID; ++chan)
      if (mLookupMap[*chan][0].channel != PCM_INVALID) {
        // something is mapped here, so add the channel
        mLayoutMap[i++] = *chan;
        }
    mLayoutMap[i] = PCM_INVALID;
    }
  else
    memcpy (mLayoutMap, PCMLayoutMap[mChannelLayout], sizeof(PCMLayoutMap[mChannelLayout]));

  return mLayoutMap;
  }
//}}}
//{{{
void cPcmMap::setOutputFormat (unsigned int channels, enum PCMChannels *channelMap, bool ignoreLayout) {
/* sets the output format supported by the audio renderer */

  mOutChannels = channels;
  mOutSet = (channelMap != NULL);
  mIgnoreLayout = ignoreLayout;

  if (channelMap)
    memcpy (mOutMap, channelMap, sizeof(enum PCMChannels) * channels);

  dumpMap ("out", channels, channelMap);
  buildMap();
  }
//}}}

//{{{
void cPcmMap::reset() {

  mInSet  = false;
  mOutSet = false;
  }
//}}}

// private
//{{{
/* resolves the channels recursively and returns the new index of tablePtr */
struct PCMMapInfo* cPcmMap::resolveChannel (enum PCMChannels channel, float level, bool ifExists,
                                            vector<enum PCMChannels> path, struct PCMMapInfo *tablePtr) {

  if (channel == PCM_INVALID)
    return tablePtr;

  // if its a 1 to 1 mapping, return
  if (mUseable[channel]) {
    tablePtr->channel = channel;
    tablePtr->level = level;

    ++tablePtr;
    tablePtr->channel = PCM_INVALID;
    return tablePtr;
    }
  else if (ifExists)
    level /= 2;

  vector<enum PCMChannels>::iterator itt;
  for (auto info = PCMDownmixTable[channel]; info->channel != PCM_INVALID; ++info) {
    // make sure we are not about to recurse into ourself
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
    tablePtr = resolveChannel (info->channel, l, info->ifExists, path, tablePtr);
    path.pop_back();
    }

  return tablePtr;
  }
//}}}
//{{{
void cPcmMap::resolveChannels() {
// build lookup table without extra adjustments, useful if we simply
//  want to find out which channels are active. For final adjustments, BuildMap() is used.

  unsigned int inCh;
  unsigned int outCh;
  bool hasSide = false;
  bool hasBack = false;

  memset (mUseable, 0, sizeof(mUseable));

  if (!mOutSet) {
    // Output format is not known yet, assume the full configured map.
    // Note that m_ignoreLayout-using callers normally ignore the result of
    // this function when !m_outSet, when it is called only for an advice for
    // the caller of SetInputFormat about the best possible output map, and
    // they can still set their output format arbitrarily in their call to SetOutputFormat
    for (enum PCMChannels *chan = PCMLayoutMap[mChannelLayout]; *chan != PCM_INVALID; ++chan)
      mUseable[*chan] = true;
    }
  else if (mIgnoreLayout) {
    for (outCh = 0; outCh < mOutChannels; ++outCh)
      mUseable[mOutMap[outCh]] = true;
    }
  else {
    // figure out what channels we have and can use
    for (enum PCMChannels *chan = PCMLayoutMap[mChannelLayout]; *chan != PCM_INVALID; ++chan) {
      for (outCh = 0; outCh < mOutChannels; ++outCh)
        if (mOutMap[outCh] == *chan) {
          mUseable[*chan] = true;
          break;
          }
      }
    }

  // force mono audio to front left and front right
  if (!mIgnoreLayout && mInChannels == 1 &&
      mInMap[0] == PCM_FRONT_CENTER &&
      mUseable[PCM_FRONT_LEFT] && mUseable[PCM_FRONT_RIGHT]) {
    cLog::log (LOGINFO1, "cPcmMap - Mapping mono audio to front left and front right");
    mUseable[PCM_FRONT_CENTER] = false;
    mUseable[PCM_FRONT_LEFT_OF_CENTER] = false;
    mUseable[PCM_FRONT_RIGHT_OF_CENTER] = false;
    }

  // see if our input has side/back channels
  for(inCh = 0; inCh < mInChannels; ++inCh)
    switch (mInMap[inCh]) {
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

  // if our input has side, and not back channels, and our output doesnt have side channels
  if (hasSide && !hasBack && (!mUseable[PCM_SIDE_LEFT] || !mUseable[PCM_SIDE_RIGHT])) {
    cLog::log (LOGINFO1, "cPcmMap - Forcing side channel map to back channels");
    for(inCh = 0; inCh < mInChannels; ++inCh)
           if (mInMap[inCh] == PCM_SIDE_LEFT ) mInMap[inCh] = PCM_BACK_LEFT;
      else if (mInMap[inCh] == PCM_SIDE_RIGHT) mInMap[inCh] = PCM_BACK_RIGHT;
    }

  //* resolve all the channels
  struct PCMMapInfo table[PCM_MAX_CH + 1], *info, *dst;
  vector<enum PCMChannels> path;

  for (int i = 0; i < PCM_MAX_CH + 1; i++) {
    for (int j = 0; j < PCM_MAX_CH + 1; j++)
      mLookupMap[i][j].channel = PCM_INVALID;
    }

  memset (mCounts, 0, sizeof(mCounts));
  for (inCh = 0; inCh < mInChannels; ++inCh) {
    for (int i = 0; i < PCM_MAX_CH + 1; i++)
      table[i].channel = PCM_INVALID;

    resolveChannel(mInMap[inCh], 1.0f, false, path, table);
    for (info = table; info->channel != PCM_INVALID; ++info) {
      // find the end of the table
      for (dst = mLookupMap[info->channel]; dst->channel != PCM_INVALID; ++dst);

      // append it to the table and set its input offset
      dst->channel = mInMap[inCh];
      dst->in_offset = inCh * 2;
      dst->level = info->level;
      mCounts[dst->channel]++;
      }
    }
  }
//}}}

//{{{
void cPcmMap::buildMap() {
// builds a lookup table to convert from the input mapping to the output
// mapping, this decreases the amount of work per sample to remap it

  struct PCMMapInfo *dst;
  unsigned int outCh;
  if (!mInSet || !mOutSet)
    return;

  /* see if we need to normalize the levels */
  bool dontnormalize = mDontNormalize;
  cLog::log(LOGINFO1, "cPcmMap - Downmix normalization is %s",
                      (dontnormalize ? "disabled" : "enabled"));

  resolveChannels();

  /* convert the levels into RMS values */
  float loudest    = 0.0;
  bool hasLoudest = false;

  for (outCh = 0; outCh < mOutChannels; ++outCh) {
    float scale = 0;
    int count = 0;
    for(dst = mLookupMap[mOutMap[outCh]]; dst->channel != PCM_INVALID; ++dst) {
      dst->copy = false;
      dst->level = dst->level / sqrt((float)mCounts[dst->channel]);
      scale += dst->level;
      ++count;
      }

    /* if there is only 1 channel to mix, and the level is 1.0, then just copy the channel */
    dst = mLookupMap[mOutMap[outCh]];
    if (count == 1 && dst->level > 0.99 && dst->level < 1.01)
      dst->copy = true;

    /* normalize the levels if it is turned on */
    if (!dontnormalize)
      for(dst = mLookupMap[mOutMap[outCh]]; dst->channel != PCM_INVALID; ++dst) {
        dst->level /= scale;
        /* find the loudest output level we have that is not 1-1 */
        if (dst->level < 1.0 && loudest < dst->level) {
          loudest = dst->level;
          hasLoudest = true;
          }
        }
    }

  /* adjust the channels that are too loud */
  for(outCh = 0; outCh < mOutChannels; ++outCh) {
    string s = "", f;
    for(dst = mLookupMap[mOutMap[outCh]]; dst->channel != PCM_INVALID; ++dst) {
      if (hasLoudest && dst->copy) {
        dst->level = loudest;
        dst->copy  = false;
        }

      f = pcmChannelStr(dst->channel); // + dst->level, dst->copy ? "*" : "");
      s += f;
      }
    cLog::log (LOGINFO1, "cPcmMap - %s = %s", pcmChannelStr(mOutMap[outCh]).c_str(), s.c_str());
    }
  }
//}}}
//{{{
string cPcmMap::pcmChannelStr (enum PCMChannels ename) {

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
void cPcmMap::dumpMap (string info, unsigned int channels, enum PCMChannels *channelMap) {

  if (channelMap == NULL) {
    cLog::log (LOGINFO, "cPcmMap - empty");
    return;
    }

  string mapping;
  for (auto i = 0u; i < channels; ++i)
    mapping += ((i == 0) ? "" : ",") + pcmChannelStr (channelMap[i]);

  cLog::log (LOGINFO, "cPcmMap - " + info + " "  + mapping);
  }
//}}}
