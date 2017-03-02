// cLog.cpp - nice little logging class
//{{{  includes
#include "cLog.h"

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>

#include "PlatformDefs.h"
#include "StdString.h"

#define remove_utf8   remove
#define rename_utf8   rename
#define fopen64_utf8  fopen
#define stat64_utf8   stat64
//}}}
//{{{  const
const char* prefixFormat = "%02.2d:%02.2d:%02.2d.%06d%s ";

const char levelColours[][12] = { "\033[38;5;117m",   // debug  bluewhite
                                  "\033[38;5;220m",   // info   yellow
                                  "\033[38;5;112m",   // info11 yellow
                                  "\033[38;5;208m",   // note   orange
                                  "\033[38;5;207m",   // warn   mauve
                                  "\033[38;5;196m",   // error  light red
                                   };

const char* postfix =             "\033[m";

const char levelNames[][6] =    { " Deb ",
                                  " info",
                                  " Info",
                                  " note",
                                  " warn",
                                  " ERR ",
                                  };
//}}}

// vars
pthread_mutex_t m_log_mutex;
FILE* mFile = NULL;
enum eLogCode mLogLevel = LOGNONE;
std::string mRepeatLine = "";
int mRepeatLogCode = -1;
int mRepeatCount = 0;

//{{{
bool cLog::Init (const char* path, enum eLogCode logLevel) {

  pthread_mutex_init (&m_log_mutex, NULL);

  mLogLevel = logLevel;
  if (mLogLevel > LOGNONE) {
    if (path && !mFile) {
      CStdString strLogFile;
      strLogFile.Format ("%s/omxPlayer.log", path);

      CStdString strLogFileOld;
      strLogFileOld.Format ("%s/omxPlayer.old.log", path);

      struct stat info;
      if (stat (strLogFileOld.c_str(), &info) == 0 && remove (strLogFileOld.c_str()) != 0)
        return false;
      if (stat (strLogFile.c_str(), &info) == 0 && rename (strLogFile.c_str(), strLogFileOld.c_str()) != 0)
        return false;

      mFile = fopen (strLogFile.c_str(), "wb");
      }
    }

  return mFile != NULL;
  }
//}}}
//{{{
void cLog::Close() {

  if (mFile) {
    fclose (mFile);
    mFile = NULL;
    }

  mRepeatLine.clear();
  pthread_mutex_destroy (&m_log_mutex);
  }
//}}}

//{{{
enum eLogCode cLog::GetLogLevel() {
  return mLogLevel;
  }
//}}}
//{{{
void cLog::SetLogLevel (enum eLogCode logLevel) {
  if (logLevel > LOGNONE)
    cLog::Log (LOGNOTICE, "Log level changed to %d", logLevel);
  mLogLevel = logLevel;
  }
//}}}

//{{{
void cLog::Log (enum eLogCode logCode, const char *format, ... ) {

  pthread_mutex_lock (&m_log_mutex);

  if ((logCode <= mLogLevel) || (logCode >= LOGWARNING)) {
    //{{{  get usec time
    struct timeval now;
    gettimeofday (&now, NULL);
    SYSTEMTIME time;
    time.wHour = (now.tv_sec/3600) % 24;
    time.wMinute = (now.tv_sec/60) % 60;
    time.wSecond = now.tv_sec % 60;
    int usec = now.tv_usec;
    //}}}
    //{{{  get log str
    CStdString str;
    str.reserve (1024);

    va_list va;
    va_start (va, format);
    str.FormatV (format,va);
    va_end (va);

    //}}}

    // check for repeat
    CStdString prefixStr;
    if (logCode == mRepeatLogCode && str == mRepeatLine) {
      //{{{  repeated
      mRepeatCount++;
      pthread_mutex_unlock (&m_log_mutex);
      return;
      }
      //}}}
    else if (mRepeatCount) {
      //{{{  output repeated
      prefixStr.Format (prefixFormat, time.wHour, time.wMinute, time.wSecond, usec, levelColours[mRepeatLogCode]);
      str.Format ("Previous line repeats %d times\n", mRepeatCount);
      fputs (prefixStr.c_str(), stdout);
      fputs (str.c_str(), stdout);
      fputs (postfix, stdout);

      if (mFile) {
        prefixStr.Format (prefixFormat, time.wHour, time.wMinute, time.wSecond, usec, levelNames[mRepeatLogCode]);
        fputs (prefixStr.c_str(), mFile);
        fputs (str.c_str(), mFile);
        }

      mRepeatCount = 0;
      }
      //}}}

    mRepeatLogCode = logCode;
    mRepeatLine = str;

    unsigned int length = 0;
    while (length != str.length()) {
      //{{{  trim trailing crud
      length = str.length();
      str.TrimRight (" ");
      str.TrimRight ('\n');
      str.TrimRight ("\r");
      }
      //}}}
    if (!length) {
      //{{{  empty, return
      pthread_mutex_unlock (&m_log_mutex);
      return;
      }
      //}}}
    str += "\n";

    // console
    prefixStr.Format (prefixFormat, time.wHour, time.wMinute, time.wSecond, usec, levelColours[logCode]);
    fputs (prefixStr.c_str(), stdout);
    fputs (str.c_str(), stdout);
    fputs (postfix, stdout);

    if (mFile) {
      prefixStr.Format (prefixFormat, time.wHour, time.wMinute, time.wSecond, usec, levelNames[logCode]);
      fputs (prefixStr.c_str(), mFile);
      fputs (str.c_str(), mFile);
      fflush (mFile);
      }
    }

  pthread_mutex_unlock (&m_log_mutex);
  }
//}}}
