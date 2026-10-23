#include "NxCrashGuard.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>

#include "AppSettings.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <csignal>
#include <cwchar>
#include <dbghelp.h>
#include <windows.h>
#endif

QGC_LOGGING_CATEGORY(NxCrashGuardLog, "NX.CrashGuard")

namespace {

constexpr const char* kGroup = "NxSession";
constexpr const char* kRunningKey = "running";
constexpr const char* kStartedKey = "started";
constexpr const char* kDumpPattern = "NX-Rover-crash-*.dmp";
constexpr int kDumpsKept = 5;

bool s_started = false;

QString dumpDirectory()
{
    return SettingsManager::instance()->appSettings()->crashSavePath();
}

#ifdef Q_OS_WIN

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// Everything the handlers use is prepared beforehand: a crashed process should not allocate or load libraries
MiniDumpWriteDumpFn s_writeDump = nullptr;
wchar_t s_dumpDir[MAX_PATH] = {};
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
volatile LONG s_dumping = 0;

void setDumpDirectory(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (native.empty() || (native.size() >= MAX_PATH)) {
        s_dumpDir[0] = L'\0';
        return;
    }
    (void) wcsncpy_s(s_dumpDir, native.c_str(), _TRUNCATE);
}

void writeDump(EXCEPTION_POINTERS* exception)
{
    // One dump per crash, even when several threads fail at once
    if (!s_writeDump || !s_dumpDir[0] || (InterlockedExchange(&s_dumping, 1) != 0)) {
        return;
    }

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t path[MAX_PATH + 64] = {};
    (void) _snwprintf_s(path, _TRUNCATE, L"%ls\\NX-Rover-crash-%04u%02u%02u-%02u%02u%02u.dmp", s_dumpDir,
                        static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
                        static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
                        static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond));

    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    // Stacks of all threads plus the memory they point at: a few megabytes, enough to find the place
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules);
    (void) s_writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, exception ? &info : nullptr, nullptr,
                       nullptr);
    (void) CloseHandle(file);
}

LONG WINAPI unhandledException(EXCEPTION_POINTERS* exception)
{
    writeDump(exception);
    // The application's own filter decides how the process ends
    return s_previousFilter ? s_previousFilter(exception) : EXCEPTION_EXECUTE_HANDLER;
}

// abort() (qFatal, std::terminate, a pure virtual call) ends the process without an exception
void abortSignal(int)
{
    writeDump(nullptr);
}

void installHandlers()
{
    if (!s_writeDump) {
        const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        if (dbghelp) {
            s_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
        }
        if (!s_writeDump) {
            qCWarning(NxCrashGuardLog) << "dbghelp.dll is not available; crashes will not be saved";
        }
    }
    s_previousFilter = SetUnhandledExceptionFilter(unhandledException);
    (void) std::signal(SIGABRT, abortSignal);
}

#endif  // Q_OS_WIN

/// Dump written since the run that started at `since`, or empty
QString dumpSince(const QDateTime& since)
{
    const QFileInfoList dumps =
        QDir(dumpDirectory()).entryInfoList(QStringList{QString::fromLatin1(kDumpPattern)}, QDir::Files, QDir::Time);
    for (const QFileInfo& dump : dumps) {
        if (!since.isValid() || (dump.lastModified() >= since)) {
            return dump.absoluteFilePath();
        }
    }
    return QString();
}

void removeOldDumps()
{
    const QFileInfoList dumps =
        QDir(dumpDirectory()).entryInfoList(QStringList{QString::fromLatin1(kDumpPattern)}, QDir::Files, QDir::Time);
    for (qsizetype i = kDumpsKept; i < dumps.size(); ++i) {
        (void) QFile::remove(dumps.at(i).absoluteFilePath());
    }
}

}  // namespace

void NxCrashGuard::start()
{
    if (s_started) {
        return;
    }
    s_started = true;

    const QString directory = dumpDirectory();
    (void) QDir().mkpath(directory);
#ifdef Q_OS_WIN
    setDumpDirectory(directory);
    (void) QObject::connect(SettingsManager::instance()->appSettings(), &AppSettings::savePathsChanged, qgcApp(), []() {
        const QString path = dumpDirectory();
        (void) QDir().mkpath(path);
        setDumpDirectory(path);
    });
    installHandlers();
#endif

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    const bool lastRunOpen = settings.value(QLatin1String(kRunningKey), false).toBool();
    const QDateTime lastStart = settings.value(QLatin1String(kStartedKey)).toDateTime();

    // Warnings, because only warnings reach the log file unless more is switched on
    qCWarning(NxCrashGuardLog) << "NX Rover" << QCoreApplication::applicationVersion() << "started";
    if (lastRunOpen) {
        const QString dump = dumpSince(lastStart);
        if (!dump.isEmpty()) {
            qCWarning(NxCrashGuardLog) << "The previous run crashed. Crash dump:" << dump;
            qgcApp()->showAppMessage(
                QCoreApplication::translate("NxCrashGuard",
                                            "NX Rover crashed last time. The crash report was saved to:\n%1\n\n"
                                            "Send this file together with the AppLog.log file from the Logs folder.")
                    .arg(QDir::toNativeSeparators(dump)));
        } else {
            qCWarning(NxCrashGuardLog) << "The previous run, started" << lastStart.toString(Qt::ISODate)
                                       << ", did not end with a normal exit";
            qgcApp()->showAppMessage(
                QCoreApplication::translate("NxCrashGuard",
                                            "NX Rover was not closed normally last time: it crashed, its task was "
                                            "ended, or the computer was turned off."));
        }
    }
    removeOldDumps();

    settings.setValue(QLatin1String(kRunningKey), true);
    settings.setValue(QLatin1String(kStartedKey), QDateTime::currentDateTime());
    settings.sync();
}

void NxCrashGuard::stop()
{
    if (!s_started) {
        return;
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(kRunningKey), false);
    settings.sync();
    qCWarning(NxCrashGuardLog) << "NX Rover closed normally";
}
