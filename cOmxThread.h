#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "../shared/utils/cLog.h"

class cOmxThread {
public:
  //{{{
  cOmxThread() {
    //pthread_mutex_init (&mThreadLock, NULL);
    pthread_attr_init (&mTattr);
    pthread_attr_setdetachstate (&mTattr, PTHREAD_CREATE_JOINABLE);
    }
  //}}}
  //{{{
  ~cOmxThread() {
    //pthread_mutex_destroy (&mThreadLock);
    pthread_attr_destroy (&mTattr);
    }
  //}}}

  //{{{
  bool Create() {

    mStop = false;
    mRunning = true;
    pthread_create (&mThread, &mTattr, &Run, this);

    cLog::log (LOGINFO1, "Create - thread id:%d started", (int)mThread);
    return true;
    }
  //}}}
  bool isRunning() { return mRunning; }
  bool isStopped() { return mStop; }
  pthread_t getThreadHandle() { return mThread; }

  //{{{
  bool StopThread() {

    mStop = true;
    pthread_join (mThread, NULL);
    mRunning = false;
    mThread = 0;

    cLog::log (LOGINFO1, "StopThread - thread id:%d stopped", (int)mThread);
    return true;
    }
  //}}}
  virtual void Process() = 0;

private:
  //{{{
  static void* Run (void* arg) {

    auto thread = static_cast<cOmxThread*>(arg);

    thread->Process();

    cLog::log (LOGINFO1, "Run - thread exit id:%d", (int)thread->getThreadHandle());
    pthread_exit (NULL);
    }
  //}}}

  pthread_t mThread = 0;
  pthread_attr_t mTattr;
  struct sched_param mSchedParam;

  volatile bool mStop = false;
  volatile bool mRunning = false;
  };
