// cKeyboard.cpp
//{{{  includes
#include "cKeyboard.h"

#include <stdio.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <map>

#include "cLog.h"

using namespace std;
//}}}

//{{{
map<int, int> cKeyConfig::buildDefaultKeymap() {

  map<int,int> keymap;

  keymap['j'] = ACTION_PREVIOUS_AUDIO;
  keymap['k'] = ACTION_NEXT_AUDIO;
  keymap['n'] = ACTION_PREVIOUS_VIDEO;
  keymap['m'] = ACTION_NEXT_VIDEO;

  keymap['q'] = ACTION_EXIT;
  keymap[KEY_ESC] = ACTION_EXIT;

  keymap[' '] = ACTION_PLAYPAUSE;

  keymap['-'] = ACTION_DECREASE_VOLUME;
  keymap['+'] = ACTION_INCREASE_VOLUME;
  keymap['='] = ACTION_INCREASE_VOLUME;

  keymap[KEY_LEFT] = ACTION_SEEK_BACK_SMALL;
  keymap[KEY_RIGHT] = ACTION_SEEK_FORWARD_SMALL;
  keymap[KEY_DOWN] = ACTION_SEEK_BACK_LARGE;
  keymap[KEY_UP] = ACTION_SEEK_FORWARD_LARGE;

  return keymap;
  }
//}}}

//{{{
cKeyboard::cKeyboard() {

  if (isatty (STDIN_FILENO)) {
    struct termios new_termios;
    tcgetattr (STDIN_FILENO, &orig_termios);

    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO | ECHOCTL | ECHONL);
    new_termios.c_cflag |= HUPCL;
    new_termios.c_cc[VMIN] = 0;
    tcsetattr (STDIN_FILENO, TCSANOW, &new_termios);
    }
  else {
    orig_fl = fcntl (STDIN_FILENO, F_GETFL);
    fcntl (STDIN_FILENO, F_SETFL, orig_fl | O_NONBLOCK);
    }

  Create();
  m_action = -1;
  }
//}}}
//{{{
cKeyboard::~cKeyboard() {
  if (ThreadHandle())
    StopThread();
  restore_term();
  }
//}}}

//{{{
void cKeyboard::restore_term() {
  if (isatty (STDIN_FILENO))
    tcsetattr (STDIN_FILENO, TCSANOW, &orig_termios);
  else
    fcntl (STDIN_FILENO, F_SETFL, orig_fl);
  }
//}}}
//{{{
void cKeyboard::Sleep (unsigned int dwMilliSeconds) {
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;
  while ( nanosleep(&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
  }
//}}}
//{{{
void cKeyboard::Process() {

  while (!m_bStop) {
    int ch[8];
    int chnum = 0;

    while ((ch[chnum] = getchar()) != EOF)
      chnum++;
    if (chnum > 1)
      ch[0] = ch[chnum - 1] | (ch[chnum - 2] << 8);
    if (chnum > 0)
      cLog::Log (LOGINFO, "cKeyboard char 0x%x %c ", ch[0], ch[0]);
    if (m_keymap[ch[0]] != 0)
      m_action = m_keymap[ch[0]];
    else
      Sleep(20);
    }
  }
//}}}

//{{{
int cKeyboard::getEvent() {
  int ret = m_action;
  m_action = -1;
  return ret;
  }
//}}}
