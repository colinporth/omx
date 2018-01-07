// cOmxThread.cpp
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cOmxThread.h"
#include "../shared/utils/cLog.h"
//}}}

//{{{
cOmxThread::cOmxThread() {

  pthread_mutex_init (&m_lock, NULL);
  pthread_attr_init (&m_tattr);
  pthread_attr_setdetachstate (&m_tattr, PTHREAD_CREATE_JOINABLE);

  m_thread = 0;
  m_bStop = false;
  m_running = false;
  }
//}}}
//{{{
cOmxThread::~cOmxThread() {
  pthread_mutex_destroy (&m_lock);
  pthread_attr_destroy (&m_tattr);
  }
//}}}

pthread_t cOmxThread::ThreadHandle() { return m_thread; }

//{{{
bool cOmxThread::Create() {

  if (m_running) {
    cLog::log (LOGERROR, "cOmxThread::Create already running");
    return false;
    }

  m_bStop = false;
  m_running = true;
  pthread_create (&m_thread, &m_tattr, &cOmxThread::Run, this);

  cLog::log (LOGINFO1, "cOmxThread::Create id:%d started", (int)m_thread);
  return true;
  }
//}}}
//{{{
bool cOmxThread::StopThread() {

  if (!m_running) {
    cLog::log (LOGINFO1, "cOmxThread::StopThread not running");
    return false;
    }

  m_bStop = true;
  pthread_join (m_thread, NULL);
  m_running = false;
  m_thread = 0;

  cLog::log (LOGINFO1, "cOmxThread::StopThread id:%d stopped", (int)m_thread);
  return true;
  }
//}}}

// protected
//{{{
void cOmxThread::Lock() {

  if (!m_running) {
    cLog::log (LOGINFO1, "cOmxThread::Lock not running");
    return;
    }

  pthread_mutex_lock (&m_lock);
  }
//}}}
//{{{
void cOmxThread::UnLock() {

  if (!m_running) {
    cLog::log (LOGINFO1, "cOmxThread::UnLock not running");
    return;
    }

  pthread_mutex_unlock (&m_lock);
  }
//}}}

// private
//{{{
void* cOmxThread::Run (void* arg) {

  auto thread = static_cast<cOmxThread*>(arg);
  thread->Process();

  cLog::log (LOGINFO1, "cOmxThread::Run exit id:%d", (int)thread->ThreadHandle());
  pthread_exit (NULL);
  }
//}}}
