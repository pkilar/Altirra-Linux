// Linux tchar.h stub
// On Windows, tchar.h provides TCHAR macros for Unicode/ANSI abstraction.
// On Linux, we don't need this — just provide minimal stubs.

#pragma once

#ifndef _TCHAR_H_LINUX_STUB
#define _TCHAR_H_LINUX_STUB

typedef char TCHAR;

#define _T(x) x
#define _tcslen strlen
#define _tcscpy strcpy
#define _tcscat strcat
#define _tcscmp strcmp
#define _tcsicmp strcasecmp
#define _tprintf printf
#define _tscanf scanf
#define _tfopen fopen
#define _ttoi atoi
#define _ttol atol

#endif // _TCHAR_H_LINUX_STUB
