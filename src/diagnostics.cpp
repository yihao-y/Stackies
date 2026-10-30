#include "diagnostics.h"
#include "datapath.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>
#include <string>
#endif

namespace sticky {
namespace {
constexpr qint64 kMaxLogBytes=1024*1024;
QString logFile;
bool debugEnabled=false;
QMutex logMutex;
void rotateLogs(){
    if(QFile(logFile).size()<kMaxLogBytes)return;
    QFile::remove(logFile+".2");
    QFile::rename(logFile+".1",logFile+".2");
    QFile::rename(logFile,logFile+".1");
}
void logMessage(QtMsgType type,const QMessageLogContext &,const QString &message){
    // Normal operation records warnings/errors only. --debug also records
    // informational and performance messages for troubleshooting.
    if((type==QtDebugMsg||type==QtInfoMsg)&&!debugEnabled)return;
    if(logFile.isEmpty())return;
    QMutexLocker guard(&logMutex);
    rotateLogs();
    const char *level="INFO";
    if(type==QtDebugMsg)level="DEBUG";
    else if(type==QtWarningMsg)level="WARN";
    else if(type==QtCriticalMsg)level="ERROR";
    else if(type==QtFatalMsg)level="FATAL";
    const QString line=QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        +" "+QString::fromLatin1(level)+" "+message+"\n";
    QFile file(logFile);
    if(file.open(QIODevice::WriteOnly|QIODevice::Append))
        file.write(line.toUtf8());
}
#ifdef Q_OS_WIN
std::wstring crashDirectory;
LONG WINAPI unhandledCrash(EXCEPTION_POINTERS *exception){
    // Avoid Qt, mutexes, allocations or application callbacks while corrupted.
    if(crashDirectory.empty())return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME stamp{};
    GetLocalTime(&stamp);
    wchar_t path[MAX_PATH]={};
    if(swprintf_s(path,MAX_PATH,
        L"%ls\\crash_%04u%02u%02u_%02u%02u%02u_%lu.dmp",
        crashDirectory.c_str(),stamp.wYear,stamp.wMonth,stamp.wDay,
        stamp.wHour,stamp.wMinute,stamp.wSecond,GetCurrentProcessId())<0)
        return EXCEPTION_CONTINUE_SEARCH;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                            CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId=GetCurrentThreadId();
        info.ExceptionPointers=exception;
        info.ClientPointers=FALSE;
        MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,
                          MiniDumpNormal,&info,nullptr,nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void pruneDumps(const QString &dir){
    QDir d(dir);
    const auto list=d.entryInfoList({"crash_*.dmp"},QDir::Files,QDir::Time);
    for(int i=3;i<list.size();++i)QFile::remove(list.at(i).absoluteFilePath());
}
#endif
}
QString diagnosticsDirectory(){
#ifdef Q_OS_WIN
    // Machine-local diagnostics; notes remain in the Roaming data directory.
    return collapseDuplicateStackiesDirectory(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
#elif defined(Q_OS_LINUX)
    // XDG state is separate from XDG data (and does not require Qt 6.7).
    const QString state=qEnvironmentVariable("XDG_STATE_HOME");
    const QString root=QDir::isAbsolutePath(state)?state:
        QDir::home().filePath(".local/state");
    return QDir(root).filePath("Stackies");
#else
    return collapseDuplicateStackiesDirectory(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
#endif
}
void installDiagnostics(){
    const QString base=diagnosticsDirectory();
    if(base.isEmpty())return;
    QDir().mkpath(base+"/logs");
    logFile=base+"/logs/app.log";
    debugEnabled=(qEnvironmentVariable("STICKY_STACK_DEBUG_LOG")=="1"||
                  QCoreApplication::arguments().contains("--debug"));
#ifdef Q_OS_WIN
    const QString crashPath=base+"/crashes";
    if(QDir().mkpath(crashPath)){
        pruneDumps(crashPath);
        crashDirectory=QDir::toNativeSeparators(crashPath).toStdWString();
        SetUnhandledExceptionFilter(&unhandledCrash);
    }
#endif
    qInstallMessageHandler(&logMessage);
}
}
