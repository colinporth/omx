// cSingleLock.h - recursive mutex
#pragma once
#include <pthread.h>

class cCriticalSection {
public:
  inline cCriticalSection() {
    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init (&mutexAttr);
    pthread_mutexattr_settype (&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (&mLock, &mutexAttr);
    }
  inline ~cCriticalSection() { pthread_mutex_destroy (&mLock); }

  inline void Lock() { pthread_mutex_lock (&mLock); }
  inline void Unlock() { pthread_mutex_unlock (&mLock); }

protected:
  pthread_mutex_t mLock;

private:
  cCriticalSection (cCriticalSection &other) = delete;
  cCriticalSection& operator = (const cCriticalSection&) = delete;
  };


class cSingleLock {
public:
  inline cSingleLock (cCriticalSection& crtiticalSection) :
      mCrtiticalSection (crtiticalSection) {
    mCrtiticalSection.Lock();
    }
  inline ~cSingleLock() { mCrtiticalSection.Unlock(); }

protected:
  cCriticalSection& mCrtiticalSection;
  };
