#pragma once
#include "cOmxThread.h"
#include <termios.h>
#include <map>

//{{{
class cKeyConfig {
public:
  //{{{
  enum {
    ACTION_PREVIOUS_AUDIO,
    ACTION_NEXT_AUDIO,
    ACTION_EXIT,
    ACTION_PLAYPAUSE,
    ACTION_DECREASE_VOLUME,
    ACTION_INCREASE_VOLUME,
    ACTION_SEEK_BACK_SMALL,
    ACTION_SEEK_FORWARD_SMALL,
    ACTION_SEEK_BACK_LARGE,
    ACTION_SEEK_FORWARD_LARGE,
    ACTION_STEP,
    ACTION_PAUSE,
    ACTION_PLAY6,
    ACTION_PREVIOUS_VIDEO,
    ACTION_NEXT_VIDEO,
    };
  //}}}
  #define KEY_LEFT 0x5b44
  #define KEY_RIGHT 0x5b43
  #define KEY_UP 0x5b41
  #define KEY_DOWN 0x5b42
  #define KEY_ESC 27

  static std::map<int, int> buildDefaultKeymap();
  };
//}}}

class cKeyboard : public cOmxThread {
public:
  cKeyboard();
  ~cKeyboard();

  void Process();
  void setKeymap (std::map<int,int> keymap);
  void Sleep (unsigned int dwMilliSeconds);
  int getEvent();

protected:
  struct termios orig_termios;
  int orig_fl;
  int m_action;
  std::map<int,int> m_keymap;

private:
  void restore_term();
  };
