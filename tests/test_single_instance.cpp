#include "singleinstance.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

#define VERIFY(x) do { if(!(x))throw std::runtime_error("single-instance check failed: " #x); } while(0)
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir data;
        VERIFY(data.isValid());
        int wakeCount=0;
        {
            sticky::SingleInstance first(data.path());
            VERIFY(first.start([&]{++wakeCount;})==
                   sticky::SingleInstance::Result::Primary);
            sticky::SingleInstance second(data.path());
            VERIFY(second.start({})==sticky::SingleInstance::Result::Forwarded);
            QElapsedTimer elapsed;
            elapsed.start();
            while(wakeCount==0 && elapsed.elapsed()<1500){
                QCoreApplication::processEvents(QEventLoop::AllEvents,100);
                QThread::msleep(10);
            }
            VERIFY(wakeCount==1);
            sticky::SingleInstance unrelated(data.path()+"/other");
            VERIFY(unrelated.start({})==sticky::SingleInstance::Result::Primary);
        }
        sticky::SingleInstance relaunched(data.path());
        VERIFY(relaunched.start({})==sticky::SingleInstance::Result::Primary);
        std::cout<<"PASS: duplicate launch forwards wake-up, separate data is independent\n";
        return 0;
    }catch(const std::exception &e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
