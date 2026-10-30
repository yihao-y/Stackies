#pragma once
// Opt-in performance probes. Keep the normal interactive path free of
// noisy logs and Windows process-metric calls.
#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace sticky::perf {
inline bool enabled(){
    static const bool on=qEnvironmentVariableIntValue("STACKIES_PERF")==1;
    return on;
}
class Scope {
    const char *name_;
    QElapsedTimer timer_;
public:
    explicit Scope(const char *name):name_(name){
        if(enabled())timer_.start();
    }
    ~Scope(){
        if(timer_.isValid())
            qInfo().noquote()<<"PERF"<<name_<<timer_.nsecsElapsed()/1000000.0<<"ms";
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline void memory(const char *checkpoint){
    if(!enabled())return;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                            sizeof(counters))){
        constexpr double MiB=1024.0*1024.0;
        qInfo().noquote()<<"PERF memory"<<checkpoint
                         <<"working-set-MiB"<<counters.WorkingSetSize/MiB
                         <<"private-MiB"<<counters.PrivateUsage/MiB
                         <<"peak-working-set-MiB"<<counters.PeakWorkingSetSize/MiB;
    }else qWarning()<<"PERF memory query failed at"<<checkpoint;
#else
    Q_UNUSED(checkpoint);
#endif
}
} // namespace sticky::perf
