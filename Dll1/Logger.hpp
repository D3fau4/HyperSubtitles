#pragma once

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

class Logger {
public:
    static void init()
    {
#ifdef _DEBUG
        fopen_s(&logFile, "log.txt", "a");
#endif
    }

    static void close()
    {
#ifdef _DEBUG
        if (logFile) {
            fclose(logFile);
            logFile = nullptr;
        }
#ifdef _WIN32
        if (ownsConsole) {
            FreeConsole();
            ownsConsole = false;
            consoleEnabled = false;
        }
#endif
#endif
    }

    static void enableConsole(bool allocateIfNeeded = true)
    {
#if defined(_DEBUG) && defined(_WIN32)
        if (consoleEnabled) {
            return;
        }

        if (!GetConsoleWindow()) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS) && allocateIfNeeded) {
                ownsConsole = AllocConsole() == TRUE;
            }
        }

        if (GetConsoleWindow()) {
            FILE* out = nullptr;
            FILE* err = nullptr;
            freopen_s(&out, "CONOUT$", "w", stdout);
            freopen_s(&err, "CONOUT$", "w", stderr);
            consoleEnabled = true;
        }
#endif
    }

    static void log(const char* format, ...)
    {
#ifdef _DEBUG
        va_list args;
        va_start(args, format);

        if (logFile) {
            vfprintf(logFile, format, args);
            fputc('\n', logFile);
            fflush(logFile);
        }

        va_end(args);

#ifdef _WIN32
        if (consoleEnabled) {
            va_start(args, format);
            vfprintf(stdout, format, args);
            fputc('\n', stdout);
            fflush(stdout);
            va_end(args);
        }
#endif
#endif
    }

private:
    inline static FILE* logFile = nullptr;
#ifdef _DEBUG
    inline static bool consoleEnabled = false;
    inline static bool ownsConsole = false;
#endif
};
