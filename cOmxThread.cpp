// cOmxThread.cpp
//{{{  includes
#include "cOmxThread.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cLog.h"
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

//{{{
pthread_t cOmxThread::ThreadHandle() {
  return m_thread;
  }
//}}}

//{{{
bool cOmxThread::Create() {

  if (m_running) {
    cLog::Log (LOGERROR, "cOmxThread::Create already running");
    return false;
    }

  m_bStop = false;
  m_running = true;
  pthread_create (&m_thread, &m_tattr, &cOmxThread::Run, this);

  cLog::Log (LOGDEBUG, "cOmxThread::Create id:%d started", (int)m_thread);
  return true;
  }
//}}}
//{{{
bool cOmxThread::StopThread() {

  if (!m_running) {
    cLog::Log (LOGDEBUG, "cOmxThread::StopThread not running");
    return false;
    }

  m_bStop = true;
  pthread_join (m_thread, NULL);
  m_running = false;
  m_thread = 0;

  cLog::Log (LOGDEBUG, "cOmxThread::StopThread stopped");
  return true;
  }
//}}}

// protected
//{{{
void cOmxThread::Lock() {

  if (!m_running) {
    cLog::Log (LOGDEBUG, "cOmxThread::Lock not running");
    return;
    }

  pthread_mutex_lock (&m_lock);
  }
//}}}
//{{{
void cOmxThread::UnLock() {

  if (!m_running) {
    cLog::Log (LOGDEBUG, "cOmxThread::UnLock not running");
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

  cLog::Log (LOGDEBUG, "cOmxThread::Run exit id:%d", (int)thread->ThreadHandle());
  pthread_exit (NULL);
  }
//}}}
