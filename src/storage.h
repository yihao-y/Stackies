#pragma once
#include "model.h"
#include <string>
#include <stdexcept>
#include <unordered_set>
struct sqlite3;
namespace sticky {
class DatabaseRevisionConflict : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class Storage {
    sqlite3 *db_=nullptr;
    std::string path_;
    bool openedOnce_=false;
public:
    explicit Storage(const std::string& path);
    ~Storage();
    Storage(const Storage&)=delete;
    Storage& operator=(const Storage&)=delete;
    Workspace load();
    bool hasSavedData();
    // Reopen on demand; closing while idle checkpoints WAL for folder sync.
    void release();
    // Empty string represents pre-revision databases until their first save.
    std::string revision();
    // Create a standalone, transactionally consistent database (including WAL contents).
    void backupTo(const std::string &destination);
    // Refuse to relocate to a damaged SQLite database.
    void verifyIntegrity();
    void save(const Workspace &workspace,
              const std::string *expectedRevision=nullptr);
    // Update only existing Note and Stack rows; structural changes use save().
    void saveChanged(const Workspace &workspace,
        const std::unordered_set<std::string> &noteIds,
        const std::unordered_set<std::string> &stackIds,
        const std::string *expectedRevision=nullptr);
private:
    void ensureOpen();
    void checkExpectedRevision(const std::string *expected);
    void stampRevision();
    void exec(const char *sql);
};
}
