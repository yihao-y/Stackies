#include "storage.h"
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace sticky {
namespace {
void check(int rc,sqlite3* db){if(rc!=SQLITE_OK&&rc!=SQLITE_DONE&&rc!=SQLITE_ROW)throw std::runtime_error(sqlite3_errmsg(db));}
class Stmt {
    sqlite3_stmt *st_=nullptr;sqlite3 *db_;
public:
    Stmt(sqlite3* db,const char *sql):db_(db){check(sqlite3_prepare_v2(db,sql,-1,&st_,nullptr),db);}
    ~Stmt(){sqlite3_finalize(st_);}
    sqlite3_stmt* get()const{return st_;}
    void reset(){check(sqlite3_reset(st_),db_);sqlite3_clear_bindings(st_);}
    void text(int index,const std::string &v){check(sqlite3_bind_text(st_,index,v.c_str(),-1,SQLITE_TRANSIENT),db_);}
    void number(int index,int v){check(sqlite3_bind_int(st_,index,v),db_);}
    void number64(int index,std::int64_t v){check(sqlite3_bind_int64(st_,index,v),db_);}
    void step(){check(sqlite3_step(st_),db_);}
};
std::string column(sqlite3_stmt* s,int col){const unsigned char* t=sqlite3_column_text(s,col);return t?reinterpret_cast<const char*>(t):"";}
}
Storage::Storage(const std::string& path):path_(path){ensureOpen();}
void Storage::ensureOpen(){
    if(db_)return;
    const int flags=SQLITE_OPEN_READWRITE | (openedOnce_?0:SQLITE_OPEN_CREATE);
    const int rc=sqlite3_open_v2(path_.c_str(),&db_,flags,nullptr);
    if(rc!=SQLITE_OK){std::string e=db_?sqlite3_errmsg(db_):"cannot open database";if(db_)sqlite3_close(db_);db_=nullptr;throw std::runtime_error(e);}
    try{
    sqlite3_busy_timeout(db_,3000);
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec("CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,title TEXT NOT NULL,tab TEXT NOT NULL,color TEXT NOT NULL,html TEXT NOT NULL,follow_title INTEGER NOT NULL DEFAULT 1);");
    exec("CREATE TABLE IF NOT EXISTS stacks(id TEXT PRIMARY KEY,x INTEGER NOT NULL,y INTEGER NOT NULL,w INTEGER NOT NULL,h INTEGER NOT NULL,visible INTEGER NOT NULL,topmost INTEGER NOT NULL,active TEXT NOT NULL,opacity INTEGER NOT NULL DEFAULT 100);");
    exec("CREATE TABLE IF NOT EXISTS memberships(stack_id TEXT NOT NULL REFERENCES stacks(id) ON DELETE CASCADE,note_id TEXT NOT NULL UNIQUE REFERENCES notes(id) ON DELETE CASCADE,ord INTEGER NOT NULL,PRIMARY KEY(stack_id,note_id));");
    exec("CREATE TABLE IF NOT EXISTS app_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);");
    exec("CREATE TABLE IF NOT EXISTS trash(id TEXT PRIMARY KEY,title TEXT NOT NULL,tab TEXT NOT NULL,color TEXT NOT NULL,html TEXT NOT NULL,follow_title INTEGER NOT NULL,source_stack TEXT NOT NULL,source_index INTEGER NOT NULL,x INTEGER NOT NULL,y INTEGER NOT NULL,w INTEGER NOT NULL,h INTEGER NOT NULL,topmost INTEGER NOT NULL,deleted_at INTEGER NOT NULL,source_opacity INTEGER NOT NULL DEFAULT 100);");
    }catch(...){
        sqlite3_close(db_);
        db_=nullptr;
        throw;
    }
    openedOnce_=true;
}
Storage::~Storage(){if(db_)sqlite3_close(db_);}
void Storage::release(){
    if(!db_)return;
    if(sqlite3_close(db_)!=SQLITE_OK)
        throw std::runtime_error("Cannot close SQLite database connection");
    db_=nullptr;
}
std::string Storage::revision(){
    ensureOpen();
    Stmt s(db_,"SELECT value FROM app_meta WHERE key='revision';");
    return sqlite3_step(s.get())==SQLITE_ROW?column(s.get(),0):std::string();
}
void Storage::checkExpectedRevision(const std::string *expected){
    if(expected && revision()!=*expected)
        throw DatabaseRevisionConflict("The database was modified outside Stackies; local changes were not saved");
}
void Storage::stampRevision(){
    Stmt s(db_,"INSERT OR REPLACE INTO app_meta(key,value) VALUES('revision',?);");
    s.text(1,Workspace::newId());
    s.step();
}
void Storage::exec(const char *sql){char* err=nullptr;const int rc=sqlite3_exec(db_,sql,nullptr,nullptr,&err);if(rc!=SQLITE_OK){std::string e=err?err:"database error";sqlite3_free(err);throw std::runtime_error(e);}}
bool Storage::hasSavedData(){
    ensureOpen();
    Stmt s(db_,"SELECT 1 FROM app_meta WHERE key='initialized' LIMIT 1;");
    return sqlite3_step(s.get())==SQLITE_ROW;
}
void Storage::backupTo(const std::string &destination){
    ensureOpen();
    sqlite3 *target=nullptr;
    const int openRc=sqlite3_open_v2(destination.c_str(),&target,
                                    SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr);
    if(openRc!=SQLITE_OK){
        const std::string error=target?sqlite3_errmsg(target):"cannot open backup";
        if(target)sqlite3_close(target);
        throw std::runtime_error("Database backup: "+error);
    }
    sqlite3_backup *backup=sqlite3_backup_init(target,"main",db_,"main");
    if(!backup){
        const std::string error=sqlite3_errmsg(target);
        sqlite3_close(target);
        throw std::runtime_error("Database backup: "+error);
    }
    const int stepRc=sqlite3_backup_step(backup,-1);
    const int finishRc=sqlite3_backup_finish(backup);
    const std::string error=sqlite3_errmsg(target);
    const int closeRc=sqlite3_close(target);
    if(stepRc!=SQLITE_DONE||finishRc!=SQLITE_OK||closeRc!=SQLITE_OK)
        throw std::runtime_error("Database backup: "+error);
}
void Storage::verifyIntegrity(){
    ensureOpen();
    Stmt check(db_,"PRAGMA quick_check;");
    if(sqlite3_step(check.get())!=SQLITE_ROW||
       column(check.get(),0)!="ok")
        throw std::runtime_error("SQLite database integrity check failed");
}
Workspace Storage::load(){
    ensureOpen();
    Workspace w;
    {
        Stmt s(db_,"SELECT id,title,tab,color,html,follow_title FROM notes ORDER BY rowid;");
        while(sqlite3_step(s.get())==SQLITE_ROW){Note n;auto*p=s.get();n.id=column(p,0);n.title=column(p,1);n.tab=column(p,2);n.color=column(p,3);n.html=column(p,4);n.followTitle=sqlite3_column_int(p,5)!=0;w.notes.push_back(std::move(n));}
    }
    {
        Stmt s(db_,"SELECT id,x,y,w,h,visible,topmost,active,opacity FROM stacks ORDER BY rowid;");
        while(sqlite3_step(s.get())==SQLITE_ROW){Stack n;auto*p=s.get();n.id=column(p,0);n.rect={sqlite3_column_int(p,1),sqlite3_column_int(p,2),sqlite3_column_int(p,3),sqlite3_column_int(p,4)};n.visible=sqlite3_column_int(p,5)!=0;n.topmost=sqlite3_column_int(p,6)!=0;n.activeId=column(p,7);n.opacity=sqlite3_column_int(p,8);w.stacks.push_back(std::move(n));}
    }
    {
        Stmt s(db_,"SELECT stack_id,note_id FROM memberships ORDER BY stack_id,ord;");
        while(sqlite3_step(s.get())==SQLITE_ROW)w.stack(column(s.get(),0)).noteIds.push_back(column(s.get(),1));
    }
    {
        Stmt s(db_,"SELECT id,title,tab,color,html,follow_title,source_stack,source_index,x,y,w,h,topmost,deleted_at,source_opacity FROM trash ORDER BY deleted_at DESC;");
        while(sqlite3_step(s.get())==SQLITE_ROW){
            auto *p=s.get();DeletedNote d;
            d.note.id=column(p,0);d.note.title=column(p,1);d.note.tab=column(p,2);
            d.note.color=column(p,3);d.note.html=column(p,4);
            d.note.followTitle=sqlite3_column_int(p,5)!=0;
            d.sourceStackId=column(p,6);d.sourceIndex=sqlite3_column_int(p,7);
            d.sourceRect={sqlite3_column_int(p,8),sqlite3_column_int(p,9),
                          sqlite3_column_int(p,10),sqlite3_column_int(p,11)};
            d.sourceTopmost=sqlite3_column_int(p,12)!=0;
            d.deletedAt=sqlite3_column_int64(p,13);
            d.sourceOpacity=sqlite3_column_int(p,14);
            w.trash.push_back(std::move(d));
        }
    }
    w.validate();return w;
}
void Storage::save(const Workspace &w,const std::string *expectedRevision){
    w.validate();
    ensureOpen();
    exec("BEGIN IMMEDIATE;");
    try{
        checkExpectedRevision(expectedRevision);
        exec("DELETE FROM memberships;");exec("DELETE FROM notes;");exec("DELETE FROM stacks;");exec("DELETE FROM trash;");
        Stmt n(db_,"INSERT INTO notes(id,title,tab,color,html,follow_title) VALUES(?,?,?,?,?,?);");
        for(const auto& v:w.notes){n.text(1,v.id);n.text(2,v.title);n.text(3,v.tab);n.text(4,v.color);n.text(5,v.html);n.number(6,v.followTitle?1:0);n.step();n.reset();}
        Stmt s(db_,"INSERT INTO stacks(id,x,y,w,h,visible,topmost,active,opacity) VALUES(?,?,?,?,?,?,?,?,?);");
        Stmt m(db_,"INSERT INTO memberships(stack_id,note_id,ord) VALUES(?,?,?);");
        for(const auto& v:w.stacks){s.text(1,v.id);s.number(2,v.rect.x);s.number(3,v.rect.y);s.number(4,v.rect.w);s.number(5,v.rect.h);s.number(6,v.visible?1:0);s.number(7,v.topmost?1:0);s.text(8,v.activeId);s.number(9,v.opacity);s.step();s.reset();
            for(size_t i=0;i<v.noteIds.size();++i){m.text(1,v.id);m.text(2,v.noteIds[i]);m.number(3,static_cast<int>(i));m.step();m.reset();}
        }
        Stmt t(db_,"INSERT INTO trash(id,title,tab,color,html,follow_title,source_stack,source_index,x,y,w,h,topmost,deleted_at,source_opacity) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        for(const auto &d:w.trash){
            t.text(1,d.note.id);t.text(2,d.note.title);t.text(3,d.note.tab);
            t.text(4,d.note.color);t.text(5,d.note.html);
            t.number(6,d.note.followTitle?1:0);t.text(7,d.sourceStackId);
            t.number(8,d.sourceIndex);t.number(9,d.sourceRect.x);
            t.number(10,d.sourceRect.y);t.number(11,d.sourceRect.w);
            t.number(12,d.sourceRect.h);t.number(13,d.sourceTopmost?1:0);
            t.number64(14,d.deletedAt);t.number(15,d.sourceOpacity);t.step();t.reset();
        }
        exec("INSERT OR REPLACE INTO app_meta(key,value) VALUES('initialized','1');");
        stampRevision();
        exec("COMMIT;");
    }catch(...){sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);throw;}
}

void Storage::saveChanged(const Workspace &w,
                          const std::unordered_set<std::string> &noteIds,
                          const std::unordered_set<std::string> &stackIds,
                          const std::string *expectedRevision){
    if(noteIds.empty()&&stackIds.empty())return;
    w.validate();
    ensureOpen();
    exec("BEGIN IMMEDIATE;");
    try{
        checkExpectedRevision(expectedRevision);
        Stmt n(db_,"UPDATE notes SET title=?,tab=?,color=?,html=?,follow_title=? WHERE id=?;");
        for(const auto &id:noteIds){
            const auto &v=w.note(id);
            n.text(1,v.title);n.text(2,v.tab);n.text(3,v.color);
            n.text(4,v.html);n.number(5,v.followTitle?1:0);
            n.text(6,id);n.step();
            if(sqlite3_changes(db_)!=1)
                throw std::runtime_error("Partial save: note missing in database: "+id);
            n.reset();
        }
        Stmt s(db_,"UPDATE stacks SET x=?,y=?,w=?,h=?,visible=?,topmost=?,active=?,opacity=? WHERE id=?;");
        for(const auto &id:stackIds){
            const auto &v=w.stack(id);
            s.number(1,v.rect.x);s.number(2,v.rect.y);
            s.number(3,v.rect.w);s.number(4,v.rect.h);
            s.number(5,v.visible?1:0);s.number(6,v.topmost?1:0);
            s.text(7,v.activeId);s.number(8,v.opacity);s.text(9,id);
            s.step();
            if(sqlite3_changes(db_)!=1)
                throw std::runtime_error("Partial save: stack missing in database: "+id);
            s.reset();
        }
        stampRevision();
        exec("COMMIT;");
    }catch(...){
        sqlite3_exec(db_,"ROLLBACK;",nullptr,nullptr,nullptr);
        throw;
    }
}
}
