#pragma once

#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

//{{{
#ifndef PRId64
  #ifdef _MSC_VER
    #define PRId64 "I64d"
  #else
    #if __WORDSIZE == 64
      #define PRId64 "ld"
    #else
     #define PRId64 "lld"
    #endif
  #endif
#endif
//}}}
//{{{
#ifndef PRIu64
  #ifdef _MSC_VER
    #define PRIu64 "I64u"
  #else
    #if __WORDSIZE == 64
      #define PRIu64 "lu"
    #else
      #define PRIu64 "llu"
    #endif
  #endif
#endif
//}}}
//{{{
#ifndef PRIx64
  #ifdef _MSC_VER
    #define PRIx64 "I64x"
  #else
    #if __WORDSIZE == 64
      #define PRIx64 "lx"
    #else
      #define PRIx64 "llx"
    #endif
  #endif
#endif
//}}}
//{{{
#ifndef PRIdS
  #define PRIdS "zd"
#endif
//}}}
//{{{
#ifndef PRIuS
  #define PRIuS "zu"
#endif
//}}}

#define CONST   const
#define FALSE   0
#define TRUE    1

#define PIXEL_ASHIFT 24
#define PIXEL_RSHIFT 16
#define PIXEL_GSHIFT 8
#define PIXEL_BSHIFT 0
#define AMASK 0xff000000
#define RMASK 0x00ff0000
#define GMASK 0x0000ff00
#define BMASK 0x000000ff

#define XXLog(a,b) printf("%s", (b))

#define _fdopen fdopen
#define _vsnprintf vsnprintf
#define _stricmp  strcasecmp
#define stricmp   strcasecmp
#define strcmpi strcasecmp
#define strnicmp  strncasecmp
#define _atoi64(x) atoll(x)
#define CopyMemory(dst,src,size) memmove(dst, src, size)
#define ZeroMemory(dst,size) memset(dst, 0, size)

#define VOID    void
#define byte    unsigned char
#define __int8    char
#define __int16   short
#define __int32   int
#define __int64   long long
#define __uint64  unsigned long long

#define __stdcall   __attribute__((__stdcall__))
#define __cdecl
#define WINBASEAPI
#define NTAPI       __stdcall
#define CALLBACK    __stdcall
#define WINAPI      __stdcall
#define WINAPIV     __cdecl
#define APIENTRY    WINAPI
#define APIPRIVATE  __stdcall
#define IN
#define OUT
#define OPTIONAL
#define _declspec(X)
#define __declspec(X)

#define __try try
#define EXCEPTION_EXECUTE_HANDLER ...
//NOTE: dont try to define __except because it breaks g++ (already uses it).

typedef pthread_t ThreadIdentifier;

struct CXHandle; // forward declaration
typedef CXHandle* HANDLE;

typedef void* HINSTANCE;
typedef void* HMODULE;

typedef unsigned int  DWORD;
typedef unsigned short  WORD;
typedef unsigned char   BYTE;
typedef char        CHAR;
typedef unsigned char UCHAR;
typedef wchar_t     WCHAR;
typedef int         BOOL;
typedef BYTE        BOOLEAN;
typedef short       SHORT;
typedef unsigned short  USHORT;
typedef int         INT;
typedef unsigned int  UINT;
typedef unsigned int  UINT32;
typedef long long     INT64;
typedef unsigned long long    UINT64;
typedef long        LONG;
typedef long long     LONGLONG;
typedef unsigned long   ULONG;
typedef float         FLOAT;
typedef size_t        SIZE_T;
typedef void*         PVOID;
typedef void*         LPVOID;
#define INVALID_HANDLE_VALUE     ((HANDLE)~0U)
typedef HANDLE        HDC;
typedef void*       HWND;
typedef LONG        HRESULT;
typedef BYTE*       LPBYTE;
typedef DWORD*        LPDWORD;
typedef CONST CHAR*   LPCSTR;
typedef CONST WCHAR*    LPCWSTR;
typedef CHAR*     LPTSTR;
typedef WCHAR         *PWSTR,      *LPWSTR,    *NWPSTR;
typedef CHAR            *PSTR,       *LPSTR,     *NPSTR;
typedef LONG        *PLONG, *LPLONG;

#ifdef UNICODE
  typedef LPCWSTR       LPCTSTR;
#else
  typedef LPCSTR      LPCTSTR;
#endif

typedef unsigned __int64 ULONGLONG;
typedef long        LONG_PTR;
typedef unsigned long   ULONG_PTR;
typedef ULONG_PTR     DWORD_PTR;
typedef __int64     __time64_t;
typedef intptr_t (*FARPROC)(void);

#define MAXWORD   0xffff
#define MAXDWORD  0xffffffff

typedef DWORD LCID;
typedef WORD* LPWORD;
typedef BOOL* LPBOOL;
typedef CHAR* LPCHAR;
typedef CHAR* PCHAR;
typedef const void* LPCVOID;

//{{{
typedef union _LARGE_INTEGER
{
  struct {
    DWORD LowPart;
    int32_t HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
//}}}
//{{{
typedef union _ULARGE_INTEGER {
 struct {
     DWORD LowPart;
     DWORD HighPart;
 } u;
 ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;
//}}}

#define MAKELONG(low,high)  ((LONG)(((WORD)((DWORD_PTR)(low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(high) & 0xFFFF))) << 16))
LONGLONG Int32x32To64(LONG Multiplier, LONG Multiplicand);

void OutputDebugString(LPCTSTR lpOuputString);

// Date / Time
//{{{
typedef struct _SYSTEMTIME
{
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
//}}}
//{{{
typedef struct _TIME_ZONE_INFORMATION {
  LONG Bias;
  WCHAR StandardName[32];
  SYSTEMTIME StandardDate;
  LONG StandardBias;
  WCHAR DaylightName[32];
  SYSTEMTIME DaylightDate;
  LONG DaylightBias;
  } TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;
//}}}

#define TIME_ZONE_ID_INVALID    ((DWORD)0xFFFFFFFF)
#define TIME_ZONE_ID_UNKNOWN    0
#define TIME_ZONE_ID_STANDARD   1
#define TIME_ZONE_ID_DAYLIGHT   2

// Thread
#define THREAD_BASE_PRIORITY_LOWRT  15
#define THREAD_BASE_PRIORITY_MAX    2
#define THREAD_BASE_PRIORITY_MIN   -2
#define THREAD_BASE_PRIORITY_IDLE  -15
#define THREAD_PRIORITY_LOWEST          THREAD_BASE_PRIORITY_MIN
#define THREAD_PRIORITY_BELOW_NORMAL    (THREAD_PRIORITY_LOWEST+1)
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_HIGHEST         THREAD_BASE_PRIORITY_MAX
#define THREAD_PRIORITY_ABOVE_NORMAL    (THREAD_PRIORITY_HIGHEST-1)
#define THREAD_PRIORITY_ERROR_RETURN    (0x7fffffff)
#define THREAD_PRIORITY_TIME_CRITICAL   THREAD_BASE_PRIORITY_LOWRT
#define THREAD_PRIORITY_IDLE            THREAD_BASE_PRIORITY_IDLE

// Network
#define SOCKADDR_IN struct sockaddr_in
#define IN_ADDR struct in_addr
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (~0)
#define closesocket(s)  close(s)
#define ioctlsocket(s, f, v) ioctl(s, f, v)
#define WSAGetLastError() (errno)
#define WSASetLastError(e) (errno = e)
#define WSAECONNRESET ECONNRESET
#define WSAHOST_NOT_FOUND ENOENT
#define WSAETIMEDOUT  ETIMEDOUT
#define WSAEADDRINUSE EADDRINUSE
#define WSAECANCELLED EINTR
#define WSAECONNREFUSED ECONNREFUSED
#define WSAECONNABORTED ECONNABORTED
#define WSAETIMEDOUT ETIMEDOUT

typedef int SOCKET;

// Thread
typedef int (*LPTHREAD_START_ROUTINE)(void *);

// File
#define O_BINARY 0
#define O_TEXT   0
#define _O_TRUNC O_TRUNC
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _off_t off_t

#define __stat64 stat64

//{{{
struct _stati64 {
  dev_t st_dev;
  ino_t st_ino;
  unsigned short st_mode;
  short          st_nlink;
  short          st_uid;
  short          st_gid;
  dev_t st_rdev;
  __int64  st_size;
  time_t _st_atime;
  time_t _st_mtime;
  time_t _st_ctime;
  };
//}}}
//{{{
typedef struct _FILETIME
{
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
//}}}

#define _stat stat

// Audio stuff
//{{{
typedef struct tWAVEFORMATEX
{
WORD    wFormatTag;
WORD    nChannels;
DWORD   nSamplesPerSec;
DWORD   nAvgBytesPerSec;
WORD    nBlockAlign;
WORD    wBitsPerSample;
WORD    cbSize;
} __attribute__((__packed__)) WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
//}}}

#define WAVE_FORMAT_UNKNOWN           0x0000
#define WAVE_FORMAT_PCM               0x0001
#define WAVE_FORMAT_ADPCM             0x0002
#define WAVE_FORMAT_IEEE_FLOAT        0x0003
#define WAVE_FORMAT_EXTENSIBLE        0xFFFE

#define SPEAKER_FRONT_LEFT            0x00001
#define SPEAKER_FRONT_RIGHT           0x00002
#define SPEAKER_FRONT_CENTER          0x00004
#define SPEAKER_LOW_FREQUENCY         0x00008
#define SPEAKER_BACK_LEFT             0x00010
#define SPEAKER_BACK_RIGHT            0x00020
#define SPEAKER_FRONT_LEFT_OF_CENTER  0x00040
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x00080
#define SPEAKER_BACK_CENTER           0x00100
#define SPEAKER_SIDE_LEFT             0x00200
#define SPEAKER_SIDE_RIGHT            0x00400
#define SPEAKER_TOP_CENTER            0x00800
#define SPEAKER_TOP_FRONT_LEFT        0x01000
#define SPEAKER_TOP_FRONT_CENTER      0x02000
#define SPEAKER_TOP_FRONT_RIGHT       0x04000
#define SPEAKER_TOP_BACK_LEFT         0x08000
#define SPEAKER_TOP_BACK_CENTER       0x10000
#define SPEAKER_TOP_BACK_RIGHT        0x20000

//{{{
typedef struct tGUID
{
  DWORD Data1;
  WORD  Data2, Data3;
  BYTE  Data4[8];
} __attribute__((__packed__)) GUID;
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_UNKNOWN = {
  WAVE_FORMAT_UNKNOWN,
  0x0000, 0x0000,
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_PCM = {
  WAVE_FORMAT_PCM,
  0x0000, 0x0010,
  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
//}}}
//{{{
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
  WAVE_FORMAT_IEEE_FLOAT,
  0x0000, 0x0010,
  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
//}}}
//{{{
typedef struct tWAVEFORMATEXTENSIBLE
{
  WAVEFORMATEX Format;
  union
  {
    WORD wValidBitsPerSample;
    WORD wSamplesPerBlock;
    WORD wReserved;
  } Samples;
  DWORD dwChannelMask;
  GUID SubFormat;
} __attribute__((__packed__)) WAVEFORMATEXTENSIBLE;
//}}}
