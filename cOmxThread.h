#pragma once
#include <pthread.h>

class cOmxThread {
public:
  cOmxThread();
  ~cOmxThread();

  pthread_t ThreadHandle();

  bool Create();
  bool Running() { return m_running; }
  bool StopThread();

  virtual void Process() = 0;

protected:
  void Lock();
  void UnLock();

  pthread_attr_t      m_tattr;
  struct sched_param  m_sched_param;
  pthread_mutex_t     m_lock;
  pthread_t           m_thread;
  volatile bool       m_running;
  volatile bool       m_bStop;

private:
  static void* Run (void *arg);
  };
