// SingleLock.h: interface for the CSingleLock class.
#pragma once
#include <pthread.h>

class cCriticalSection {
public:
  inline cCriticalSection() {
    pthread_mutexattr_t mta;
    pthread_mutexattr_init (&mta);
    pthread_mutexattr_settype (&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (&m_lock, &mta);
    }

  inline ~cCriticalSection() { pthread_mutex_destroy (&m_lock); }
  inline void Lock() { pthread_mutex_lock (&m_lock); }
  inline void Unlock() { pthread_mutex_unlock (&m_lock); }

private:
  cCriticalSection (cCriticalSection &other) = delete;
  cCriticalSection& operator = (const cCriticalSection&) = delete;

protected:
  pthread_mutex_t m_lock;
  };


class cSingleLock {
public:
  inline cSingleLock (cCriticalSection& cs) : m_section(cs) { m_section.Lock(); }
  inline ~cSingleLock() { m_section.Unlock(); }

protected:
  cCriticalSection &m_section;
  };
