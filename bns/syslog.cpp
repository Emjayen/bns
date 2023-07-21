/*
 * syslog.cpp
 *
 */
#include "syslog.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>




// Globals
static HANDLE hLog;




/*
 * SyslogStartup
 *
 */
uib SyslogStartup(const char* pFilename)
{
    if((hLog = CreateFile("bns.log", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE)
        return FALSE;

    if(!SetFilePointerEx(hLog, { 0,0 }, NULL, FILE_END))
        return FALSE;

#ifdef ENABLE_CONOUT
    AllocConsole();
#endif

    return TRUE;
}



/*
 * Log
 *
 */
void Log(uib level, const char* pstr, ...)
{
    SYSTEMTIME st;
    DWORD Result;
    char str[1024];
    char* pd = str;


    static const char* LOG_LEVEL[] =
    {
        "ERROR",
        "WARN ",
        "INFO ",
        "DEBUG",
    };

    GetLocalTime(&st);

    pd += strfmt(pd, sizeof(str), "\r\n" "%02u-%02u-%04u %02u:%02u:%02u | %s | ", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, LOG_LEVEL[level]);
    pd += strfmtv(pd, sizeof(str), pstr, (va_list) &pstr+sizeof(void*));

    if(level == LOG_ERR)
    {
        static HMODULE hNT;

        if(!hNT)
            hNT = GetModuleHandle("ntdll.dll");

        uid Code = GetLastError();

        pd += strfmt(pd, sizeof(str), " (0x%X: ", Code);
        pd += FormatMessage(Code & 0xFF000000 ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM, Code & 0xFF000000 ? hNT : NULL, Code, NULL, pd, sizeof(str)-(pd-str), NULL);

        if(*(pd-2) == '\r')
            pd -= 2;

        *pd++ = ')';
        *pd = '\0';
    }

    WriteFile(hLog, str, pd-str, &Result, NULL);

#ifdef ENABLE_CONOUT
    WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), str, pd-str, &Result, NULL);
#endif
}