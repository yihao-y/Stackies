#include "model.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace sticky {
std::string Workspace::newId() {
    static std::atomic<uint64_t> counter{0};
    static std::mt19937_64 generator{std::random_device{}()};
    const auto ticks=static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto rand=generator(); // all calls from UI thread in current implementation
    std::ostringstream s;
    s << std::hex << ticks << '-' << rand << '-' << ++counter;
    return s.str();
}
Note &Workspace::note(const std::string& id) {
    auto it=std::find_if(notes.begin(),notes.end(),[&](const Note &n){return n.id==id;});
    if(it==notes.end())throw std::out_of_range("Note not found: "+id);
    return *it;
}
const Note &Workspace::note(const std::string& id) const {return const_cast<Workspace*>(this)->note(id);}
Stack &Workspace::stack(const std::string& id) {
    auto it=std::find_if(stacks.begin(),stacks.end(),[&](const Stack &s){return s.id==id;});
    if(it==stacks.end())throw std::out_of_range("Stack not found: "+id);
    return *it;
}
const Stack &Workspace::stack(const std::string& id) const {return const_cast<Workspace*>(this)->stack(id);}
Stack &Workspace::owner(const std::string& noteId) {
    auto it=std::find_if(stacks.begin(),stacks.end(),[&](const Stack &s){return std::find(s.noteIds.begin(),s.noteIds.end(),noteId)!=s.noteIds.end();});
    if(it==stacks.end())throw std::out_of_range("Note has no stack: "+noteId);
    return *it;
}
const Stack &Workspace::owner(const std::string& id) const {return const_cast<Workspace*>(this)->owner(id);}
std::string Workspace::addNote(const std::string& title,const Rect& rect){
    Note n; n.id=newId(); n.title=title; n.tab=title;
    const auto noteId=n.id;
    notes.push_back(std::move(n));
    Stack s; s.id=newId(); s.noteIds.push_back(noteId);s.activeId=noteId;s.rect=rect;
    stacks.push_back(std::move(s));
    return noteId;
}
void Workspace::renameTitle(const std::string& id,const std::string& newTitle){
    auto &n=note(id);
    n.title=newTitle;
    if(n.followTitle)n.tab=newTitle;
}
void Workspace::renameTab(const std::string& id,const std::string& newTab){
    auto &n=note(id);
    n.tab=newTab;
    n.followTitle=false;
}
void Workspace::setFollowTitle(const std::string& id,bool follow){
    auto &n=note(id);
    n.followTitle=follow;
    if(follow)n.tab=n.title;
}
void Workspace::activate(const std::string& stackId,const std::string& noteId){
    auto &s=stack(stackId);
    if(std::find(s.noteIds.begin(),s.noteIds.end(),noteId)==s.noteIds.end())throw std::invalid_argument("note not in stack");
    s.activeId=noteId;
}
void Workspace::moveNote(const std::string& noteId,const std::string& targetStackId,int index){
    // Find indexes first; vector references may be invalidated by erasing source stack.
    const auto sourceId=owner(noteId).id;
    stack(targetStackId);
    if(sourceId==targetStackId)throw std::invalid_argument("moveNote target same as source");
    Stack &source=stack(sourceId);
    const auto where=std::find(source.noteIds.begin(),source.noteIds.end(),noteId);
    auto next=source.noteIds.erase(where);
    if(source.activeId==noteId){
        if(!source.noteIds.empty())source.activeId=(next==source.noteIds.end()?source.noteIds.back():*next);
        else source.activeId.clear();
    }
    if(source.noteIds.empty()){
        stacks.erase(std::remove_if(stacks.begin(),stacks.end(),[&](const Stack &s){return s.id==sourceId;}),stacks.end());
    }
    Stack &target=stack(targetStackId);
    auto count=static_cast<int>(target.noteIds.size());
    if(index<0||index>count)index=count;
    target.noteIds.insert(target.noteIds.begin()+index,noteId);
    target.activeId=noteId;
}
std::string Workspace::detach(const std::string& noteId,const Rect& rect){
    const auto &s=owner(noteId);
    if(s.noteIds.size()==1){stack(s.id).rect=rect;return s.id;}
    const auto sourceId=s.id;
    Stack next;next.id=newId();next.rect=rect; next.activeId=noteId; next.noteIds={noteId};
    next.opacity=s.opacity;
    Stack &source=stack(sourceId);
    auto it=std::find(source.noteIds.begin(),source.noteIds.end(),noteId);
    auto following=source.noteIds.erase(it);
    if(source.activeId==noteId)source.activeId=(following==source.noteIds.end()?source.noteIds.back():*following);
    stacks.push_back(std::move(next));
    return stacks.back().id;
}
void Workspace::mergeStacks(const std::string& sourceId,const std::string& targetId){
    if(sourceId==targetId)return;
    const auto ids=stack(sourceId).noteIds;
    const auto selected=stack(sourceId).activeId;
    int index=0;
    for(const auto &id:ids)moveNote(id,targetId,index++);
    stack(targetId).activeId=selected;
}
void Workspace::reorder(const std::string& stackId,int from,int to){
    auto &v=stack(stackId).noteIds;
    if(from<0||to<0||from>=static_cast<int>(v.size())||to>=static_cast<int>(v.size()))throw std::out_of_range("reorder index");
    auto item=v.at(from);v.erase(v.begin()+from);v.insert(v.begin()+to,item);
}
void Workspace::deleteNote(const std::string &noteId,std::int64_t timestamp){
    const auto &n=note(noteId);
    const auto &s=owner(noteId);
    const auto it=std::find(s.noteIds.begin(),s.noteIds.end(),noteId);
    DeletedNote deleted;
    deleted.note=n;
    deleted.sourceStackId=s.id;
    deleted.sourceIndex=static_cast<int>(it-s.noteIds.begin());
    deleted.sourceRect=s.rect;
    deleted.sourceTopmost=s.topmost;
    deleted.sourceOpacity=s.opacity;
    deleted.deletedAt=timestamp;
    trash.insert(trash.begin(),std::move(deleted)); // newest first, including before restart
    const std::string stackId=s.id;
    auto &ids=stack(stackId).noteIds;
    ids.erase(ids.begin()+trash.front().sourceIndex);
    if(ids.empty()){
        stacks.erase(std::remove_if(stacks.begin(),stacks.end(),
            [&stackId](const Stack &s){return s.id==stackId;}),stacks.end());
    }else if(stack(stackId).activeId==noteId){
        stack(stackId).activeId=ids.at(std::min<int>(trash.front().sourceIndex,static_cast<int>(ids.size())-1));
    }
    notes.erase(std::remove_if(notes.begin(),notes.end(),
        [&noteId](const Note &n){return n.id==noteId;}),notes.end());
}
std::string Workspace::restoreNote(const std::string &noteId){
    const auto it=std::find_if(trash.begin(),trash.end(),
        [&noteId](const DeletedNote &d){return d.note.id==noteId;});
    if(it==trash.end())throw std::out_of_range("Deleted note not found");
    const DeletedNote deleted=*it;
    notes.push_back(deleted.note);
    trash.erase(it);
    auto target=std::find_if(stacks.begin(),stacks.end(),
        [&deleted](const Stack &s){return s.id==deleted.sourceStackId;});
    if(target==stacks.end()){
        Stack s;
        s.id=std::any_of(stacks.begin(),stacks.end(),[&deleted](const Stack &item){
            return item.id==deleted.sourceStackId;})?newId():deleted.sourceStackId;
        s.rect=deleted.sourceRect;
        s.topmost=deleted.sourceTopmost;
        s.opacity=deleted.sourceOpacity;
        s.activeId=noteId;
        s.noteIds.push_back(noteId);
        stacks.push_back(std::move(s));
        return stacks.back().id;
    }
    target->noteIds.insert(target->noteIds.begin()+
        std::clamp(deleted.sourceIndex,0,static_cast<int>(target->noteIds.size())),noteId);
    target->activeId=noteId;
    target->visible=true;
    return target->id;
}
void Workspace::permanentlyDelete(const std::string &noteId){
    const auto oldSize=trash.size();
    trash.erase(std::remove_if(trash.begin(),trash.end(),
        [&noteId](const DeletedNote &d){return d.note.id==noteId;}),trash.end());
    if(oldSize==trash.size())throw std::out_of_range("Deleted note not found");
}
void Workspace::validate()const{
    std::unordered_set<std::string> all,used,groupIDs;
    for(const auto &n:notes){if(!all.insert(n.id).second)throw std::logic_error("duplicate note ID");}
    for(const auto &s:stacks){
        if(!groupIDs.insert(s.id).second)throw std::logic_error("duplicate stack ID");
        if(s.noteIds.empty())throw std::logic_error("empty stack");
        if(s.rect.w<100||s.rect.h<100)throw std::logic_error("invalid geometry");
        if(s.opacity<50||s.opacity>100)throw std::logic_error("invalid stack opacity");
        if(std::find(s.noteIds.begin(),s.noteIds.end(),s.activeId)==s.noteIds.end())throw std::logic_error("invalid active note");
        for(const auto &n:s.noteIds){
            if(!all.count(n)||!used.insert(n).second)throw std::logic_error("invalid or duplicate membership");
        }
    }
    if(all!=used)throw std::logic_error("unassigned note");
    for(const auto &item:trash){
        if(!all.insert(item.note.id).second)throw std::logic_error("duplicate trash ID");
        if(item.sourceRect.w<100||item.sourceRect.h<100)throw std::logic_error("invalid trash geometry");
        if(item.sourceOpacity<50||item.sourceOpacity>100)throw std::logic_error("invalid trash stack opacity");
    }
}

Workspace mergeWorkspaces(const Workspace &target,const Workspace &source){
    target.validate();
    source.validate();
    Workspace result=target;
    // Same ID + same content means the same note; differences become a
    // second note with a fresh ID, never an overwrite.
    auto sameNote=[](const Note &a,const Note &b){
        return a.title==b.title && a.tab==b.tab &&
               a.followTitle==b.followTitle && a.color==b.color &&
               a.html==b.html;
    };
    auto sameTrash=[&](const DeletedNote &a,const DeletedNote &b){
        return sameNote(a.note,b.note) && a.sourceStackId==b.sourceStackId &&
               a.sourceIndex==b.sourceIndex &&
               a.sourceRect.x==b.sourceRect.x && a.sourceRect.y==b.sourceRect.y &&
               a.sourceRect.w==b.sourceRect.w && a.sourceRect.h==b.sourceRect.h &&
               a.sourceTopmost==b.sourceTopmost && a.sourceOpacity==b.sourceOpacity &&
               a.deletedAt==b.deletedAt;
    };
    std::unordered_map<std::string,Note> targetNotes;
    std::unordered_map<std::string,DeletedNote> targetTrash;
    std::unordered_set<std::string> usedNoteIds,usedStackIds;
    for(const auto &n:target.notes){targetNotes.emplace(n.id,n);usedNoteIds.insert(n.id);}
    for(const auto &d:target.trash){targetTrash.emplace(d.note.id,d);usedNoteIds.insert(d.note.id);}
    for(const auto &s:target.stacks)usedStackIds.insert(s.id);
    auto unusedNoteId=[&]{
        std::string id;
        do{id=Workspace::newId();}while(!usedNoteIds.insert(id).second);
        return id;
    };
    auto unusedStackId=[&]{
        std::string id;
        do{id=Workspace::newId();}while(!usedStackIds.insert(id).second);
        return id;
    };
    std::unordered_map<std::string,std::string> importedNotes,importedStacks;
    for(const auto &note:source.notes){
        const auto it=targetNotes.find(note.id);
        if(it!=targetNotes.end()&&sameNote(it->second,note))continue;
        Note added=note;
        if(!usedNoteIds.insert(added.id).second)added.id=unusedNoteId();
        importedNotes.emplace(note.id,added.id);
        result.notes.push_back(std::move(added));
    }
    // Keep imported source stacks separate and retain their tab order.
    for(const auto &group:source.stacks){
        Stack added=group;
        added.noteIds.clear();
        for(const auto &oldId:group.noteIds){
            const auto it=importedNotes.find(oldId);
            if(it!=importedNotes.end())added.noteIds.push_back(it->second);
        }
        if(added.noteIds.empty())continue;
        if(!usedStackIds.insert(added.id).second)added.id=unusedStackId();
        const auto active=importedNotes.find(group.activeId);
        added.activeId=active!=importedNotes.end()?active->second:added.noteIds.front();
        importedStacks[group.id]=added.id;
        result.stacks.push_back(std::move(added));
    }
    for(const auto &item:source.trash){
        const auto it=targetTrash.find(item.note.id);
        if(it!=targetTrash.end()&&sameTrash(it->second,item))continue;
        DeletedNote added=item;
        if(!usedNoteIds.insert(added.note.id).second)added.note.id=unusedNoteId();
        auto group=importedStacks.find(added.sourceStackId);
        if(group==importedStacks.end()&&usedStackIds.count(added.sourceStackId)){
            const auto oldId=added.sourceStackId;
            const auto id=unusedStackId();
            group=importedStacks.emplace(oldId,id).first;
        }
        if(group!=importedStacks.end())added.sourceStackId=group->second;
        result.trash.push_back(std::move(added));
    }
    result.validate();
    return result;
}


std::size_t appendConflictCopies(Workspace &latest,const Workspace &lastSaved,
                                  const Workspace &local,const std::string &suffix,
                                  const std::unordered_set<std::string> *editedIds){
    latest.validate();
    lastSaved.validate();
    local.validate();
    auto sameContent=[](const Note &a,const Note &b){
        return a.title==b.title && a.tab==b.tab && a.color==b.color &&
               a.html==b.html && a.followTitle==b.followTitle;
    };
    std::unordered_map<std::string,const Note*> before,remote;
    std::unordered_set<std::string> ids,stacks;
    for(const auto &n:lastSaved.notes)before.emplace(n.id,&n);
    for(const auto &n:latest.notes){remote.emplace(n.id,&n);ids.insert(n.id);}
    for(const auto &n:latest.trash)ids.insert(n.note.id);
    for(const auto &s:latest.stacks)stacks.insert(s.id);
    std::size_t added=0;
    for(const auto &note:local.notes){
        const auto original=before.find(note.id);
        const bool changedSinceSave=
            original==before.end() || !sameContent(note,*original->second);
        // Combine both sources: the current unsaved delta AND real user edits
        // that were already auto-saved before the external revision arrived.
        // Never filter out a pending title/color change merely because it
        // did not originate in the rich-text editor.
        const bool editedEarlier=editedIds && editedIds->count(note.id)>0;
        if(!changedSinceSave && !editedEarlier)continue;
        auto remoteVersion=remote.find(note.id);
        if(remoteVersion!=remote.end()&&sameContent(note,*remoteVersion->second))
            continue; // Already on disk, no need to create a duplicate.
        Note copy=note;
        do{copy.id=Workspace::newId();}while(!ids.insert(copy.id).second);
        copy.title+=suffix;
        if(copy.followTitle)copy.tab=copy.title;
        const Stack &owner=local.owner(note.id);
        Stack independent;
        do{independent.id=Workspace::newId();}while(!stacks.insert(independent.id).second);
        independent.noteIds={copy.id};
        independent.activeId=copy.id;
        independent.rect=owner.rect;
        independent.rect.x+=32+static_cast<int>(added%5)*20;
        independent.rect.y+=32+static_cast<int>(added%5)*20;
        independent.visible=true;
        independent.opacity=owner.opacity;
        independent.topmost=owner.topmost;
        latest.notes.push_back(std::move(copy));
        latest.stacks.push_back(std::move(independent));
        ++added;
    }
    latest.validate();
    return added;
}

}
