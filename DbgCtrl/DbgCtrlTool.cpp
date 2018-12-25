#include "DbgCtrlTool.h"
#include <ComLib/ComLib.h>
#include <ComStatic/ComStatic.h>

using namespace std;

unsigned short CalPortFormUnique(const wstring &unique) {
    DWORD port = RegGetDWORDFromRegW(HKEY_LOCAL_MACHINE, REG_VDEBUG_CACHE, unique.c_str(), 0);
    if (port)
    {
        return (unsigned short)port;
    }

    string p = WtoA(unique);
    unsigned long crc = crc32(p.c_str(), p.size(), 0xffffffff);
    port = (6300 + crc % 500);
    RegSetDWORDValueW(HKEY_LOCAL_MACHINE, REG_VDEBUG_CACHE, unique.c_str(), port);
    return (unsigned short)port;
}