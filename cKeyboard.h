#pragma once
#include "cOmxThread.h"
#include <termios.h>
#include <map>

class cKeyboard : public cOmxThread {
public:
  cKeyboard();
  ~cKeyboard();

  int getEvent();
  void setKeymap (std::map<int,int> keymap) { m_keymap = keymap; }

  void Process();
  void Sleep (unsigned int dwMilliSeconds);

protected:
  struct termios orig_termios;
  int orig_fl;
  int m_action;
  std::map<int,int> m_keymap;

private:
  void restore_term();
  };
