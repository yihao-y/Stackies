#pragma once
#include "model.h"
#include "storage.h"
#include <QObject>
#include <QString>
#include <QPointer>
#include <QTimer>
#include <QPoint>
#include <QRect>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
class QWidget;
class QColor;
class QSystemTrayIcon;
class QMenu;
class StickyWindow;
class OverviewOverlay;
namespace sticky {
class Controller : public QObject {
    Q_OBJECT
    Workspace workspace_;
    Workspace lastSavedWorkspace_;
    std::unique_ptr<Storage> storage_;
    std::unordered_map<std::string,StickyWindow*> windows_;
    QPointer<OverviewOverlay> overview_;
    QWidget *preview_=nullptr;
    QWidget *tabDragPreview_=nullptr;
    std::string tabDragNoteId_;
    QTimer saveTimer_;
    QTimer revisionTimer_;
    std::string databaseRevision_;
    bool applyingExternalUpdate_=false;
    std::unordered_set<std::string> externallyChangedStacks_;
    // Retain user-edited note IDs across successful debounce saves, until the
    // next external revision is reconciled. Needed when external sync races
    // a note that the local editor has already auto-saved.
    std::unordered_set<std::string> locallyEditedNotes_;
    // UI edits to individual rows remain incremental; membership/trash and
    // create/delete operations promote the pending batch to a full snapshot.
    bool fullSaveRequested_=false;
    std::unordered_set<std::string> dirtyNoteIds_;
    std::unordered_set<std::string> dirtyStackIds_;
    QSystemTrayIcon *tray_=nullptr;
    QMenu *trayMenu_=nullptr;
    bool syncing_=false;
    bool quitting_=false;
    bool overviewSourceHidden_=false;
    std::string overviewStackId_;
    bool temporaryDragTopmost_=false;
    std::string dragging_;
    std::string hoverTarget_;
    std::string previewTarget_;
    qint64 enteredAt_=0;
public:
    explicit Controller(QObject *parent=nullptr);
    ~Controller() override;
    const Workspace &workspace()const{return workspace_;}
    Workspace &workspace(){return workspace_;}
    const Note &note(const std::string &id)const{return workspace_.note(id);}
    const Stack &stack(const std::string &id)const{return workspace_.stack(id);}
    StickyWindow *window(const std::string &id) const;
    void initialize();
    void syncWindows();
    void createNote(const std::string &targetStackId={});
    void deleteNote(const std::string& id);
    void showRecentlyDeleted();
    void bringVisibleWindowsForward();
    void activateFromSecondInstance();
    void activate(const std::string &stackId,const std::string &noteId);
    void setNoteTitle(const std::string& noteId,const QString &title);
    void renameTab(const std::string& noteId);
    void setFollowTitle(const std::string& noteId,bool follow);
    void setNoteColor(const std::string& noteId,const QColor &color);
    void addColorActions(QMenu *menu,const std::string& noteId);
    void recolorNote(const std::string& noteId);
    void setStackOpacity(const std::string &stackId,int percent);
    void reorderNotes(const std::string& stackId,int from,int to);
    void updateHtml(const std::string &noteId,const QString &html);
    void markInteractiveEdit(const std::string &noteId);
    void saveGeometry(const std::string &stackId,const QRect &rect);
    void hideStack(const std::string &id);
    void minimizeStack(const std::string &id);
    void toggleTopmost(const std::string &id);
    void showOverview(const std::string &id);
    void setOverviewStackPresentation(const std::string &id,bool glassIsOpen);
    bool requestOverviewTaskbarRestore(const std::string &id);
    void beginWindowDrag(const std::string& source);
    void windowDragged(const std::string& source,const QPoint &global);
    void finishWindowDrag(const std::string& source,const QPoint &global,bool dragged);
    void tabDragged(const std::string& source,const std::string &noteId,const QPoint &global);
    void tabReleased(const std::string& source,const std::string &noteId,const QPoint &global,bool dragged);
    void detachFromOverview(const std::string &source,const std::string &noteId,const QRect &rect);
    void scheduleSave();
    bool saveNow();
    // Public for deterministic tests; normally invoked by a 30-second timer.
    void checkForExternalUpdates();
    void quitSafely();
    void prepareForShutdown();
    bool isQuitting() const { return quitting_; }
private:
    void createTray();
    void changeDataLocation();
    void scheduleNoteSave(const std::string &noteId);
    void scheduleStackSave(const std::string &stackId);
    void scheduleAllStacksSave();
    std::string targetAt(const QPoint&global,const std::string& exclude) const;
    void showPreview(const QRect &rect,bool ready);
    void hidePreview();
    void clearTabDragPreview();
    QRect tabDragRect(const std::string& source,const QPoint& global) const;
};
}
