#pragma once
#include <string>
#include <cstddef>
#include <unordered_set>
#include <vector>
#include <stdexcept>
#include <cstdint>

namespace sticky {
struct Rect { int x=100, y=100, w=370, h=310; };
struct Note {
    std::string id;
    std::string title="Untitled";
    std::string tab="Note";
    bool followTitle=true;
    std::string color="#F6E6B7";
    std::string html="<p></p>";
};
struct DeletedNote {
    Note note;
    std::string sourceStackId;
    int sourceIndex=0;
    Rect sourceRect;
    bool sourceTopmost=false;
    int sourceOpacity=100;
    std::int64_t deletedAt=0;
};
struct Stack {
    std::string id;
    std::vector<std::string> noteIds;
    std::string activeId;
    Rect rect;
    bool visible=true;
    bool topmost=false;
    int opacity=100; // Paper alpha percentage, shared by every note in this stack.
};
class Workspace {
public:
    std::vector<Note> notes;
    std::vector<DeletedNote> trash;
    std::vector<Stack> stacks;
    static std::string newId();
    Note &note(const std::string& id);
    const Note &note(const std::string& id) const;
    Stack &stack(const std::string& id);
    const Stack &stack(const std::string& id) const;
    Stack &owner(const std::string& noteId);
    const Stack &owner(const std::string& noteId) const;
    std::string addNote(const std::string& title, const Rect &rect={});
    void activate(const std::string& stackId,const std::string& noteId);
    void renameTitle(const std::string& id,const std::string& newTitle);
    void renameTab(const std::string& id,const std::string& newTab);
    void setFollowTitle(const std::string& id,bool follow);
    void moveNote(const std::string& noteId,const std::string& targetStackId,int index=-1);
    std::string detach(const std::string& noteId,const Rect &rect);
    void mergeStacks(const std::string& sourceId,const std::string& targetId);
    void reorder(const std::string& stackId, int from, int to);
    void deleteNote(const std::string& noteId,std::int64_t timestamp);
    std::string restoreNote(const std::string& noteId);
    void permanentlyDelete(const std::string& noteId);
    void validate() const;
};
// One-time import of another workspace, preserving target content, note
// versions, stack membership and deleted notes.
Workspace mergeWorkspaces(const Workspace &target,const Workspace &source);
// Keep the latest database version and add locally modified notes as
// new standalone notes when a stale save is detected.
std::size_t appendConflictCopies(Workspace &latest,const Workspace &lastSaved,
                                  const Workspace &local,const std::string &suffix,
                                  const std::unordered_set<std::string> *editedIds=nullptr);
}
