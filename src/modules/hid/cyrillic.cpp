#include "cyrillic.h"

namespace HidCyrillic {

uint32_t utf8Next(const char** p)
{
    const uint8_t* s = (const uint8_t*)*p;
    uint32_t cp;
    if      (*s < 0x80) { cp = *s++; }
    else if (*s < 0xC0) { cp = '?'; s++; }
    else if (*s < 0xE0) { cp = (*s++ & 0x1F) << 6;  cp |= (*s++ & 0x3F); }
    else if (*s < 0xF0) { cp = (*s++ & 0x0F) << 12; cp |= (*s++ & 0x3F) << 6; cp |= (*s++ & 0x3F); }
    else                { cp = (*s++ & 0x07) << 18; cp |= (*s++ & 0x3F) << 12; cp |= (*s++ & 0x3F) << 6; cp |= (*s++ & 0x3F); }
    *p = (const char*)s;
    return cp;
}

char cyrillicKey(uint32_t cp)
{
    static const char tbl[32] = {
        'f', ',', 'd', 'u', 'l', 't', ';', 'p', 'b', 'q', 'r', 'k',
        'v', 'y', 'j', 'g', 'h', 'c', 'n', 'e', 'a', '[', 'w', 'x',
        'i', 'o', ']', 's', 'm', '\'','.', 'z'
    };
    if (cp >= 0x430 && cp <= 0x44F) return tbl[cp - 0x430];

    if (cp == 0x456) return 's';
    if (cp == 0x457) return ']';
    if (cp == 0x454) return '\'';
    if (cp == 0x491) return '`';
    if (cp == 0x451) return '`';
    return 0;
}

char shiftKey(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    switch (c) {
        case ',':  return '<';
        case '.':  return '>';
        case ';':  return ':';
        case '\'': return '"';
        case '[':  return '{';
        case ']':  return '}';
        case '`':  return '~';
        default:   return c;
    }
}

bool isUpperCyrillic(uint32_t cp)
{
    return (cp >= 0x410 && cp <= 0x42F)
        || cp == 0x406
        || cp == 0x407
        || cp == 0x404
        || cp == 0x490
        || cp == 0x401;
}

uint32_t toLowerCyrillic(uint32_t cp)
{
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp == 0x406) return 0x456;
    if (cp == 0x407) return 0x457;
    if (cp == 0x404) return 0x454;
    if (cp == 0x490) return 0x491;
    if (cp == 0x401) return 0x451;
    return cp;
}

}
