#pragma once
#include <QAbstractScrollArea>
#include <QFrame>
#include <QColor>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QVector>
#include <QTextDocument>
#include <QTimer>
#include <QTextCursor>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <string>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QHBoxLayout;
class QVBoxLayout;
class QToolButton;
class QMouseEvent;
class QEnterEvent;
class QLineEdit;
class QPropertyAnimation;
class QMenu;
namespace sticky { class Controller; }

class ReadPane : public QAbstractScrollArea {
    Q_OBJECT
    QTextDocument doc_;
    QString lastHtml_;
    int anchor_=-1;
    int cursor_=-1;
public:
    explicit ReadPane(QWidget *parent=nullptr);
    void setHtml(const QString &html,bool forceReset=false);
    void setPaperColor(const QColor &color);
    QSize sizeHint() const override { return {280,220}; }
    void copySelection();
signals:
    void editRequested();
    void checklistToggled(int blockPosition);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QColor paper_;
    void reflow();
    int hitAt(const QPoint &p) const;
};

class DragHeader : public QWidget {
    Q_OBJECT
    bool dragging_=false;
    QPoint startGlobal_;
    QPoint startTopLeft_;
    QWidget *titleTarget_=nullptr;
public:
    explicit DragHeader(QWidget *parent=nullptr);
    void setTitleTarget(QWidget *title){titleTarget_=title;}
signals:
    void moving(const QPoint &globalPoint);
    void finished(const QPoint &globalPoint,bool dragged);
    void titleDoubleClicked();
protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
};

class TabButton : public QPushButton {
    Q_OBJECT
    std::string noteId_;
    bool moved_=false;
    bool active_=false;
    int reach_=0;
    QPoint pressGlobal_;
    QVariantAnimation reachAnimation_;
    void animateReach(int target);
public:
    explicit TabButton(const std::string &noteId,QWidget *parent=nullptr);
    const std::string &noteId()const{return noteId_;}
    void setActive(bool active);
    void setIndexText(const QString &name);
signals:
    void releasedAt(QString id,QPoint global,bool dragged);
    void moving(QString id,QPoint global);
protected:
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
};

class StickyWindow : public QWidget {
    Q_OBJECT
    sticky::Controller *controller_;
    std::string stackId_;
    QString showingId_;
    bool editing_=false;
    bool settingUp_=true;
    bool forcedClose_=false;
    QColor pageColor_;
    QFrame *paper_=nullptr;
    DragHeader *header_=nullptr;
    QLabel *title_=nullptr;
    QLineEdit *titleEdit_=nullptr;
    bool titleEditing_=false;
    QWidget *seam_=nullptr;
    QPushButton *scrollUp_=nullptr;
    QPushButton *scrollDown_=nullptr;
    QVector<QWidget*> tabSlots_;
    QVector<QPoint> tabBasePositions_;
    QStringList tabOrder_;
    int firstVisibleTab_=0;
    int visibleTabCount_=1;
    bool tabLayoutReady_=false;
    bool tabRailInitialized_=false;
    bool tabRailShown_=false;
    bool adjustingTabs_=false;
    QString draggedTabStyle_;
    std::string movingTabId_;
    int insertionIndex_=-1;
    QPushButton *editButton_=nullptr;
    QPushButton *pinButton_=nullptr;
    QWidget *toolBar_=nullptr;
    QStackedWidget *pages_=nullptr;
    ReadPane *reader_=nullptr;
    QTextEdit *editor_=nullptr;
    QTimer editorVisualTimer_;
    QTimer editorSyncTimer_;
    QScrollArea *tabScroll_=nullptr;
    QWidget *tabRoot_=nullptr;

public:
    StickyWindow(sticky::Controller *controller,const std::string &stackId);
    const std::string& stackId()const{return stackId_;}
    void refresh();
    void beginEdit();
    void finishEdit();
    bool hasActiveEditor()const{return editing_||titleEditing_;}
    void forceClose();
    int tabInsertionAt(const QPoint &global) const;
    void previewTabReorder(const std::string &noteId,const QPoint &global);
    void clearTabReorder();
    QRect tabBarGlobalRect() const;
protected:
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &,void *,qintptr *) override;
#endif
    void moveEvent(QMoveEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;
private:
    void rebuildTabs();
    void positionTabs(bool ensureActive=false);
    void scrollTabs(int amount);
    int visibleTabCapacity() const;
    void setPaletteColor(const QColor &color);
    void onTabReleased(QString id,QPoint global,bool moved);
    void beginTitleEdit();
    void finishTitleEdit(bool cancel=false);
    void updateSeam();
    void updateTabOverflow();
    void showQuickColorMenu(const QPoint &global);
};
