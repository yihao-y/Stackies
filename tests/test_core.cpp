#include "model.h"
#include "storage.h"
#include <stdexcept>
#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error("Test failed at line " + std::to_string(__LINE__) + ": " #condition); } while(0)
#include <cstdio>
#include <sqlite3.h>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <exception>
using namespace sticky;
int runCoreTests(){
    Workspace w;
    auto a=w.addNote("Alpha"),b=w.addNote("Beta"),c=w.addNote("Gamma"),d=w.addNote("Delta");
    auto sa=w.owner(a).id,sb=w.owner(b).id,sc=w.owner(c).id,sd=w.owner(d).id;
    w.note(a).html="<p><b>alpha</b> and unicode: 中文</p>";
    REQUIRE(w.note(a).followTitle && w.note(a).tab=="Alpha");
    w.renameTitle(a,"Alpha revised");
    REQUIRE(w.note(a).tab=="Alpha revised");
    w.renameTab(a,"Short A");
    REQUIRE(!w.note(a).followTitle);
    w.renameTitle(a,"Alpha complete");
    REQUIRE(w.note(a).tab=="Short A");
    w.setFollowTitle(a,true);
    REQUIRE(w.note(a).tab=="Alpha complete");
    w.setFollowTitle(a,false);
    w.renameTitle(a,"Alpha kept");
    REQUIRE(w.note(a).tab=="Alpha complete");
    w.renameTab(b,"Short B");
    w.stack(sa).opacity=50;
    w.stack(sb).opacity=90;
    w.mergeStacks(sb,sa);
    REQUIRE(w.stack(sa).opacity==50);w.mergeStacks(sc,sa);
    REQUIRE(w.stacks.size()==2);REQUIRE((w.stack(sa).noteIds==std::vector<std::string>{c,b,a}));
    w.reorder(sa,2,0);REQUIRE((w.stack(sa).noteIds==std::vector<std::string>{a,c,b}));
    w.activate(sa,b);
    auto detached=w.detach(b,{250,250,320,280});
    REQUIRE(w.stack(sa).activeId==c);REQUIRE((w.stack(sa).noteIds==std::vector<std::string>{a,c}));
    REQUIRE(w.stack(detached).noteIds.size()==1);
    REQUIRE(w.stack(detached).opacity==50);
    w.moveNote(b,sd);REQUIRE(w.stack(sd).activeId==b);REQUIRE(w.stacks.size()==2);
    w.validate();
    // Detaching a non-active card must not disturb the current selection.
    // Detaching the active card must choose another remaining card.
    Workspace inactive;
    const auto first=inactive.addNote("First");
    const auto second=inactive.addNote("Second");
    const auto third=inactive.addNote("Third");
    const auto source=inactive.owner(first).id;
    inactive.moveNote(second,source);
    inactive.moveNote(third,source);
    inactive.activate(source,first);
    const auto secondStack=inactive.detach(second,{300,220,320,260});
    REQUIRE(inactive.stack(source).activeId==first);
    REQUIRE((inactive.stack(source).noteIds==std::vector<std::string>{first,third}));
    REQUIRE(inactive.stack(secondStack).activeId==second);
    inactive.activate(source,third);
    inactive.detach(third,{420,260,320,260});
    REQUIRE(inactive.stack(source).activeId==first);
    inactive.validate();
    // Deletion retains complete formatting and original index. Restoring into
    // the surviving stack should restore order and activate that note.
    Workspace recycle;
    auto r1=recycle.addNote("One"),r2=recycle.addNote("Two"),r3=recycle.addNote("Three");
    const auto recycleStack=recycle.owner(r1).id;
    recycle.moveNote(r2,recycleStack);recycle.moveNote(r3,recycleStack);
    recycle.note(r2).html="<p style='color:#123456'><b>saved</b></p>";
    recycle.stack(recycleStack).opacity=90;
    recycle.deleteNote(r2,12345);
    REQUIRE(recycle.trash.size()==1);
    REQUIRE((recycle.stack(recycleStack).noteIds==std::vector<std::string>{r1,r3}));
    REQUIRE(recycle.restoreNote(r2)==recycleStack);
    REQUIRE((recycle.stack(recycleStack).noteIds==std::vector<std::string>{r1,r2,r3}));
    REQUIRE(recycle.note(r2).html=="<p style='color:#123456'><b>saved</b></p>");
    recycle.deleteNote(r1,12346);
    recycle.deleteNote(r2,12347);
    recycle.deleteNote(r3,12348);
    REQUIRE(recycle.stacks.empty()&&recycle.notes.empty()&&recycle.trash.size()==3);
    const auto revived=recycle.restoreNote(r2);
    REQUIRE(recycle.stack(revived).noteIds==std::vector<std::string>{r2});
    REQUIRE(recycle.stack(revived).opacity==90);
    recycle.permanentlyDelete(r1);
    REQUIRE(recycle.trash.size()==1);
    recycle.validate();

    // Merge a diverged copy: unchanged IDs are deduplicated, edited versions
    // and trashed conflicts survive, and imported stack IDs are remapped.
    Workspace original;
    const auto common=original.addNote("Shared");
    const auto gone=original.addNote("Trashed");
    const auto sharedStack=original.owner(common).id;
    original.deleteNote(gone,1234);
    Workspace diverged=original;
    diverged.note(common).html="<p>Edited on another computer</p>";
    const auto extra=diverged.addNote("New on second computer");
    diverged.trash.front().note.title="Different deleted version";
    auto together=mergeWorkspaces(original,diverged);
    together.validate();
    REQUIRE(together.notes.size()==3);
    REQUIRE(together.trash.size()==2);
    REQUIRE(together.note(common).html==original.note(common).html);
    REQUIRE(together.note(extra).title=="New on second computer");
    REQUIRE(together.stack(sharedStack).noteIds==original.stack(sharedStack).noteIds);
    REQUIRE(together.stacks.size()==3);
    REQUIRE(std::any_of(together.notes.begin(),together.notes.end(),[&](const Note &n){
        return n.id!=common && n.html==diverged.note(common).html;
    }));
    REQUIRE(std::any_of(together.trash.begin(),together.trash.end(),[&](const DeletedNote &n){
        return n.note.id!=gone && n.note.title=="Different deleted version";
    }));
    // The target stays untouched, and the merged workspace survives SQLite.
    const auto mergedDb=(std::filesystem::temp_directory_path() /
        ("sticky-stack-merge-"+Workspace::newId()+".sqlite")).string();
    {
        Storage test(mergedDb);
        test.save(together);
        test.verifyIntegrity();
        const auto loaded=test.load();
        REQUIRE(loaded.notes.size()==3 && loaded.trash.size()==2);
        REQUIRE(loaded.stack(sharedStack).noteIds==original.stack(sharedStack).noteIds);
    }
    std::remove(mergedDb.c_str());
    std::remove((mergedDb+"-wal").c_str());
    std::remove((mergedDb+"-shm").c_str());

    // Overwrite uses an online SQLite backup, including when the destination
    // is already initialized. The separately saved old destination survives.
    const auto oldDb=(std::filesystem::temp_directory_path() /
        ("sticky-overwrite-target-"+Workspace::newId()+".sqlite")).string();
    const auto newDb=(std::filesystem::temp_directory_path() /
        ("sticky-overwrite-source-"+Workspace::newId()+".sqlite")).string();
    const auto safeCopy=oldDb+".backup";
    {
        Storage existing(oldDb);
        existing.save(original);
        existing.backupTo(safeCopy);
    }
    {
        Storage incoming(newDb);
        incoming.save(diverged);
        incoming.backupTo(oldDb);
    }
    {
        Storage after(oldDb),backup(safeCopy);
        after.verifyIntegrity();
        backup.verifyIntegrity();
        REQUIRE(after.load().note(extra).title=="New on second computer");
        REQUIRE(backup.load().notes.size()==original.notes.size());
        REQUIRE(backup.load().note(common).html==original.note(common).html);
    }
    for(const auto &p:{oldDb,newDb,safeCopy}){
        std::remove(p.c_str());
        std::remove((p+"-wal").c_str());
        std::remove((p+"-shm").c_str());
    }

    // Every successful write changes a persisted revision, including a partial
    // update. A stale writer must be rejected under BEGIN IMMEDIATE.
    const auto revisionDb=(std::filesystem::temp_directory_path() /
        ("sticky-revision-"+Workspace::newId()+".sqlite")).string();
    {
        Workspace first;
        const auto id=first.addNote("Initial");
        Storage writer(revisionDb);
        writer.save(first);
        const std::string initial=writer.revision();
        REQUIRE(!initial.empty());
        writer.release();
        Storage other(revisionDb);
        REQUIRE(other.revision()==initial);
        first.note(id).html="<p>Second revision</p>";
        other.saveChanged(first,{id},{},&initial);
        const std::string newest=other.revision();
        REQUIRE(newest!=initial);
        other.release();
        bool staleRejected=false;
        try{writer.save(first,&initial);}
        catch(const DatabaseRevisionConflict &){staleRejected=true;}
        REQUIRE(staleRejected);
        REQUIRE(writer.revision()==newest);
        writer.release();
        REQUIRE(other.revision()==newest);
        other.release();
    }
    // Reopening an established database must not silently recreate it if a
    // cloud-drive client temporarily removes it during a replacement.
    {
        Storage established(revisionDb);
        established.release();
        const auto missing=revisionDb+".temporarily-missing";
        std::filesystem::rename(revisionDb,missing);
        bool refusedToCreate=false;
        try{established.revision();}
        catch(const std::exception &){refusedToCreate=true;}
        REQUIRE(refusedToCreate);
        REQUIRE(!std::filesystem::exists(revisionDb));
        std::filesystem::rename(missing,revisionDb);
    }
    std::remove(revisionDb.c_str());
    std::remove((revisionDb+"-wal").c_str());
    std::remove((revisionDb+"-shm").c_str());

    // An already-auto-saved local note still needs its own conflict copy
    // if the external writer later changes that same note.
    {
        Workspace base;
        const auto id=base.addNote("Live edit");
        Workspace alreadySaved=base, incoming=base;
        alreadySaved.note(id).html="<p>Locally auto-saved</p>";
        incoming.note(id).html="<p>New remote version</p>";
        const std::unordered_set<std::string> edited={id};
        REQUIRE(appendConflictCopies(incoming,alreadySaved,alreadySaved,
                                     " (conflict copy)",&edited)==1);
        REQUIRE(incoming.note(id).html=="<p>New remote version</p>");
        REQUIRE(incoming.notes.size()==2);
        const auto &copy=incoming.notes.back();
        REQUIRE(copy.id!=id);
        REQUIRE(copy.html=="<p>Locally auto-saved</p>");
        REQUIRE(incoming.owner(copy.id).noteIds.size()==1);
        REQUIRE(incoming.owner(copy.id).visible);
        incoming.validate();
        Workspace unrelated=alreadySaved;
        const auto other=unrelated.addNote("Different note");
        unrelated.note(other).html="<p>Remote-only</p>";
        REQUIRE(appendConflictCopies(unrelated,alreadySaved,alreadySaved,
                                     " (conflict copy)",&edited)==0);
        REQUIRE(unrelated.notes.size()==2);
    }
    // A stale save while the editor is open must preserve both notes that
    // were previously auto-saved and notes with *new* changes since the last
    // save (even when the latter did not use the text editor).
    {
        Workspace base;
        const auto typed=base.addNote("Typed");
        const auto renamed=base.addNote("Renamed");
        base.note(typed).html="<p>Before</p>";
        Workspace saved=base;
        saved.note(typed).html="<p>Already auto-saved</p>";
        Workspace local=saved;
        local.renameTitle(renamed,"Locally renamed");
        Workspace external=saved;
        external.note(typed).html="<p>External typed</p>";
        external.note(renamed).html="<p>External renamed</p>";
        const std::unordered_set<std::string> edited={typed};
        REQUIRE(appendConflictCopies(external,saved,local,
                    " (conflict copy)",&edited)==2);
        REQUIRE(external.note(typed).html=="<p>External typed</p>");
        REQUIRE(external.note(renamed).html=="<p>External renamed</p>");
        bool keptAutosave=false,keptRename=false;
        for(const auto &note:external.notes){
            if(note.id!=typed && note.html=="<p>Already auto-saved</p>")
                keptAutosave=external.owner(note.id).noteIds.size()==1;
            if(note.id!=renamed && note.title=="Locally renamed (conflict copy)")
                keptRename=external.owner(note.id).noteIds.size()==1;
        }
        REQUIRE(keptAutosave && keptRename);
        external.validate();
    }
    // A revision conflict keeps the external note and copies only locally
    // edited/new notes into independent visible stacks.
    {
        Workspace base;
        const auto shared=base.addNote("Original",{160,100,340,270});
        const auto unchanged=base.addNote("Unchanged");
        base.note(shared).html="<p>Base</p>";
        Workspace remote=base,local=base;
        remote.note(shared).html="<p>Remote version</p>";
        local.note(shared).html="<p>Local version</p>";
        local.note(shared).followTitle=false;
        local.note(shared).tab="Personal tab";
        const auto extra=local.addNote("Added locally");
        local.note(extra).html="<p>New content</p>";
        REQUIRE(appendConflictCopies(remote,base,local," (conflict copy)")==2);
        REQUIRE(remote.notes.size()==4 && remote.stacks.size()==4);
        REQUIRE(remote.note(shared).html=="<p>Remote version</p>");
        REQUIRE(remote.note(unchanged).title=="Unchanged");
        int matches=0;
        for(const auto &n:remote.notes){
            if(n.html=="<p>Local version</p>"){
                ++matches;
                REQUIRE(n.id!=shared);
                REQUIRE(n.title=="Original (conflict copy)");
                REQUIRE(n.tab=="Personal tab");
                const auto &group=remote.owner(n.id);
                REQUIRE(group.visible && group.noteIds.size()==1);
                REQUIRE(group.rect.x==192);
            }
            if(n.html=="<p>New content</p>"){
                ++matches;
                REQUIRE(n.id!=extra);
                REQUIRE(remote.owner(n.id).noteIds.size()==1);
            }
        }
        REQUIRE(matches==2);
        // No-op when local and remote versions are already identical.
        Workspace equal=base;
        equal.note(shared).html="<p>Identical</p>";
        REQUIRE(appendConflictCopies(equal,base,equal," (conflict copy)")==0);
        REQUIRE(equal.notes.size()==2);
        remote.validate();
    }
    const auto db = (std::filesystem::temp_directory_path() / ("sticky-stack-test-" + Workspace::newId() + ".sqlite")).string();
    {
        Storage s(db);REQUIRE(!s.hasSavedData());s.save(w);REQUIRE(s.hasSavedData());auto v=s.load();v.validate();
        REQUIRE(v.note(a).html==w.note(a).html);REQUIRE(v.owner(c).id==sa);
        REQUIRE(!v.note(a).followTitle && v.note(a).tab=="Alpha complete");
        REQUIRE(!v.note(b).followTitle && v.note(b).tab=="Short B");
        REQUIRE(v.note(c).followTitle);
        REQUIRE(v.stack(sa).noteIds==w.stack(sa).noteIds);
        REQUIRE(v.stack(sa).opacity==50);
        REQUIRE(v.stack(sd).opacity==100);
        REQUIRE(v.stack(sd).noteIds==w.stack(sd).noteIds);
        v.stack(sd).topmost=true;v.stack(sd).visible=false;s.save(v);
        auto u=s.load();REQUIRE(u.stack(sd).topmost&&!u.stack(sd).visible);
        REQUIRE(u.stack(sa).visible && u.stack(sa).opacity==50);
        // Text/metadata updates must leave memberships and unrelated rows.
        const std::string originalOtherHtml=v.note(c).html;
        v.note(a).html="<p>incremental 中文 &amp; checklist</p>";
        v.note(a).title="Incrementally renamed";
        v.note(a).tab="Incremental tab";
        v.note(a).color="#AABBCC";
        v.note(a).followTitle=false;
        v.stack(sa).rect={80,90,420,320};
        v.stack(sa).visible=false;
        v.stack(sa).topmost=true;
        v.stack(sa).opacity=75;
        v.stack(sa).activeId=a;
        s.saveChanged(v,{a},{sa});
        auto partial=s.load();
        REQUIRE(partial.note(a).html==v.note(a).html);
        REQUIRE(partial.note(a).title==v.note(a).title);
        REQUIRE(partial.note(a).tab==v.note(a).tab);
        REQUIRE(partial.note(a).color==v.note(a).color);
        REQUIRE(!partial.note(a).followTitle);
        REQUIRE(partial.note(c).html==originalOtherHtml);
        REQUIRE(partial.stack(sa).noteIds==w.stack(sa).noteIds);
        REQUIRE(partial.stack(sa).activeId==a);
        REQUIRE(partial.stack(sa).rect.x==80);
        REQUIRE(!partial.stack(sa).visible&&partial.stack(sa).topmost);
        REQUIRE(partial.stack(sa).opacity==75);
        // A failed partial transaction must roll back earlier row updates.
        v.note(a).html="<p>after failed transaction</p>";
        bool failed=false;
        try{s.saveChanged(v,{a},{"nonexistent-stack-id"});}
        catch(const std::exception&){failed=true;}
        REQUIRE(failed);
        REQUIRE(s.load().note(a).html==partial.note(a).html);
        s.saveChanged(v,{a},{});
        REQUIRE(s.load().note(a).html==v.note(a).html);
        // An online SQLite backup includes committed data even while the source is WAL.
        const std::string copied=db+".backup";
        s.backupTo(copied);
        {Storage b(copied); b.verifyIntegrity(); REQUIRE(b.load().note(a).html==v.note(a).html);}
        std::remove(copied.c_str());
        // Full snapshot stays authoritative for structural mutations.
        s.save(recycle);
        auto restored=s.load();
        REQUIRE(restored.trash.size()==1);
        REQUIRE(restored.trash.front().note.title=="Three");
        REQUIRE(restored.trash.front().sourceOpacity==90);
        REQUIRE(restored.note(r2).html=="<p style='color:#123456'><b>saved</b></p>");
        REQUIRE(restored.restoreNote(r3).size()>0);restored.validate();
    }
    std::remove(db.c_str());std::remove((db+"-wal").c_str());std::remove((db+"-shm").c_str());
    std::cout<<"PASS: merge/reorder/detach/move/title-follow/stack-opacity/SQLite persistence\n";
    return 0;
}

int main(){
    try { return runCoreTests(); }
    catch(const std::exception &e){ std::cerr << "core tests failed: " << e.what() << '\n'; return 1; }
    catch(...){ std::cerr << "core tests failed: unknown exception\n"; return 1; }
}
