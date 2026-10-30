// Optional standalone benchmark, NOT part of CTest. Uses a disposable
// SQLite database and reports measurements without machine-specific limits.
#include "model.h"
#include "storage.h"
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
using Clock=std::chrono::steady_clock;
double elapsedMs(Clock::time_point start){
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
void runCase(int notes){
    using sticky::Workspace;
    const auto path=std::filesystem::temp_directory_path()/
        ("stackies-bench-"+Workspace::newId()+".sqlite");
    const std::string db=path.string();
    // Rich HTML payload that makes rewrite cost visible on larger workspaces.
    const std::string paragraph="<p>Bench note: <b>text</b> with a task and "
        "several words of realistic rich-text content. The result is never "
        "written to the user's Stackies data directory.</p>";
    Workspace workspace;
    for(int i=0;i<notes;++i){
        const auto id=workspace.addNote("Benchmark "+std::to_string(i));
        for(int k=0;k<16;++k)workspace.note(id).html+=paragraph;
    }
    double saveTotal=0,partialTotal=0,loadTotal=0;
    {
        sticky::Storage storage(db);
        for(int trial=0;trial<5;++trial){
            const auto start=Clock::now();
            storage.save(workspace);
            saveTotal+=elapsedMs(start);
        }
        // Measure single-note editing after the initial full snapshot.
        const auto editedId=workspace.notes.front().id;
        for(int trial=0;trial<5;++trial){
            workspace.note(editedId).html+=paragraph;
            const auto start=Clock::now();
            storage.saveChanged(workspace,{editedId},{});
            partialTotal+=elapsedMs(start);
        }
        for(int trial=0;trial<3;++trial){
            const auto start=Clock::now();
            const auto restored=storage.load();
            loadTotal+=elapsedMs(start);
            if(static_cast<int>(restored.notes.size())!=notes ||
               restored.note(editedId).html!=workspace.note(editedId).html)
                throw std::runtime_error("Incorrect benchmark round-trip contents");
        }
    }
    std::cout<<"notes="<<notes<<" save_avg_ms="<<saveTotal/5
             <<" partial_avg_ms="<<partialTotal/5
             <<" load_avg_ms="<<loadTotal/3<<'\n';
    std::error_code ec;
    std::filesystem::remove(path,ec);
    std::filesystem::remove(db+"-wal",ec);
    std::filesystem::remove(db+"-shm",ec);
}
}
int main(){
    try{
        for(const int count:{10,100,500})runCase(count);
        return 0;
    }catch(const std::exception &e){
        std::cerr<<"Storage benchmark failed: "<<e.what()<<'\n';
        return 1;
    }
}
