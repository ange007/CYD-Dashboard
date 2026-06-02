#ifndef __HID_CYRILLIC_H__
#define __HID_CYRILLIC_H__

#include <stdint.h>

namespace HidCyrillic {

uint32_t utf8Next(const char** p);
char     cyrillicKey(uint32_t cp);
char     shiftKey(char c);
bool     isUpperCyrillic(uint32_t cp);
uint32_t toLowerCyrillic(uint32_t cp);

}

#endif
