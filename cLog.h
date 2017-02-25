#pragma once
#include <stdio.h>
#include <string>

enum eLogLevel { LOG_LEVEL_NONE = -1, LOG_LEVEL_NORMAL, LOG_LEVEL_DEBUG };
enum eLogCode  { LOGDEBUG = 0, LOGINFO, LOGINFO1, LOGNOTICE, LOGWARNING, LOGERROR } ;

class cLog {
public:
  cLog() {}
  virtual ~cLog() {}

  static bool Init (const char* path, enum eLogLevel logLevel);
  static void Close();

  static enum eLogLevel GetLogLevel();
  static void SetLogLevel (enum eLogLevel level);

  static void Log (enum eLogCode logCode, const char *format, ... ) __attribute__((format(printf,2,3)));
  static void MemDump (char *pData, int length);
  };
