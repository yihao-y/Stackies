#include "controller.h"
#include "diagnostics.h"
#include "datapath.h"
#include "perf.h"
#include <QDebug>
#include "window.h"
#include "overview.h"
#include "colorpalette.h"
#include "i18n.h"
#include "platform.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QGridLayout>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QUuid>
#include <QSettings>
#include <QWidgetAction>
#include <QDateTime>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QShowEvent>
#include <QPaintEvent>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QIcon>
#include <algorithm>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QTextDocument>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>


namespace sticky {
namespace {
// QMenu may create/style its native presentation only on the first popup.
// Warm its layout after construction; measure the first actual show/paint
// to distinguish native popup startup from item rendering costs.
class TrayContextMenu : public QMenu {
    bool firstShow_=true;
    bool firstPaint_=true;
    QElapsedTimer popupTimer_;
protected:
    void showEvent(QShowEvent *event) override {
        QElapsedTimer timer;
        timer.start();
        QMenu::showEvent(event);
        if(firstShow_){
            firstShow_=false;
            popupTimer_.start();
            qInfo().noquote()<<"Tray first show event:"<<timer.elapsed()<<"ms";
        }
    }
    void paintEvent(QPaintEvent *event) override {
        if(firstPaint_){
            QElapsedTimer timer;
            timer.start();
            QMenu::paintEvent(event);
            firstPaint_=false;
            qInfo().noquote()<<"Tray first paint:"<<timer.elapsed()
                             <<"ms; show-to-paint:"<<
                             (popupTimer_.isValid()?popupTimer_.elapsed():-1)<<"ms";
        }else QMenu::paintEvent(event);
    }
};
QString qstr(const std::string &s){return QString::fromStdString(s);}
// A deliberate pause distinguishes overlapping notes from a merge gesture.
constexpr int kMergeHoverMs=800;
const QStringList &pastelPalette(){
    static const QStringList colors={
        "#FFF4D6","#FAECC0","#F6E6B7","#EED9A0","#F6E2C9","#EBC7A8","#FBE8E5","#F0C7CD",
        "#E6B9C6","#F4DAE8","#DDC1DF","#CFB4D5","#F0E8F8","#DCCFED","#C9C3E6","#D7E7F7",
        "#C4DBF2","#AFCEE9","#E8F4EC","#C3E4D5","#ADDBC9","#DFE8D0","#F8F5EF","#DCD8D0"
    };
    return colors;
}
const QStringList &quickPalette(){
    static const QStringList colors={
        "#F6E6B7","#F6D9D4","#E9DDF2","#D7E7F7",
        "#D8EDE1","#DFE8D0","#F6E2C9","#F0EDE7"
    };
    return colors;
}
QPushButton *swatch(const QColor &color,QWidget *parent,int size=27){
    auto *b=new QPushButton(parent);
    b->setFixedSize(size,size);
    b->setToolTip(color.name().toUpper());
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QString(
        "QPushButton{background:%1;border:1px solid #A4A9AC;border-radius:6px;}"
        "QPushButton:hover{border:2px solid #4F7A9D;}").arg(color.name()));
    return b;
}
void rememberColor(const QString &hex){
    QSettings settings;
    QStringList colors=settings.value("recentPaperColors").toStringList();
    colors.removeAll(hex);
    colors.prepend(hex);
    while(colors.size()>4)colors.removeLast();
    settings.setValue("recentPaperColors",colors);
}


class MergePreview : public QWidget {
    bool ready_=false;
    qreal opacity_=0;
    QVariantAnimation animation_;
public:
    MergePreview(){
        setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|
                       Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        animation_.setDuration(180);
        animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
            opacity_=v.toReal();update();
        });
    }
    void setReady(bool ready){
        ready_=ready;
        animation_.stop();
        animation_.setStartValue(opacity_);
        animation_.setEndValue(ready?1.0:0.62);
        animation_.start();
        update();
    }
    void reset(){
        animation_.stop();
        opacity_=0;
        ready_=false;
        hide();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(opacity_);
        const QRectF outline=QRectF(rect()).adjusted(12,12,-12,-12);
        // Glow follows the existing stack rather than creating a separate drop box.
        for(int i=8;i>=1;--i){
            const qreal spread=i*0.85;
            QPen glow(QColor(84,159,217,ready_?qMax(1,14-i):qMax(1,8-i/2)),1.6);
            p.setPen(glow);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(outline.adjusted(-spread,-spread,spread,spread),11+spread,11+spread);
        }
        p.setPen(QPen(QColor(104,170,221,ready_?205:125),ready_?2.0:1.35));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(outline,10,10);
        if(!ready_)return;
        QFont f=font();f.setPointSize(10);f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QString label=trUi("Release to merge");
        const int pillWidth=p.fontMetrics().horizontalAdvance(label)+40;
        const QRectF pill(outline.center().x()-pillWidth/2.0,
                          outline.center().y()-16.0,pillWidth,32.0);
        p.setBrush(QColor(236,247,255,232));
        p.setPen(QPen(QColor(136,187,222,190),1));
        p.drawRoundedRect(pill,16,16);
        const QRectF iconBox(pill.left()+12,pill.top()+10,12,12);
        p.setPen(QPen(QColor("#396D97"),1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(iconBox.adjusted(1,0,-1,-5),1,1);
        p.drawRoundedRect(iconBox.adjusted(0,4,0,-1),1,1);
        p.setPen(QColor("#396D97"));
        p.drawText(pill.adjusted(30,0,-9,0),Qt::AlignVCenter|Qt::AlignLeft,label);
    }
};

class FloatingNoteDrag : public QWidget {
    Note note_;
    QTextDocument document_;
public:
    FloatingNoteDrag(const Note &note,const QSize &size):note_(note){
        // A solid visual copy, not an interactive second editor.
        // On release the real independent note replaces this preview.
        setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|
                       Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        resize(size);
        document_.setDocumentMargin(0);
        document_.setDefaultFont(sticky::noteContentFont());
        document_.setHtml(qstr(note_.html));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor paper(qstr(note_.color));
        if(!paper.isValid())paper=QColor("#F6E6B7");
        p.setPen(QPen(QColor("#83765E"),1));
        p.setBrush(paper);
        p.drawRoundedRect(rect().adjusted(1,1,-2,-2),9,9);
        p.setPen(QColor("#38312A"));
        QFont heading("Segoe UI",10);
        heading.setBold(true);
        p.setFont(heading);
        const QRect headingRect(13,8,width()-26,32);
        p.drawText(headingRect,Qt::AlignVCenter|Qt::AlignLeft,
                   p.fontMetrics().elidedText(qstr(note_.title),Qt::ElideRight,headingRect.width()));
        p.save();
        const int bodyHeight=std::max(10,height()-55);
        p.setClipRect(QRect(12,44,width()-24,bodyHeight));
        p.translate(13,46);
        document_.setTextWidth(std::max(80,width()-26));
        document_.drawContents(&p,QRectF(0,0,width()-26,bodyHeight));
        p.restore();
    }
};
}

namespace {
// A selected directory may contain other user files. Move/remove only
// Stackies-owned entries, and delete the directory itself only if empty.
QString normalizedDirectory(const QString &path){
    const QFileInfo info(path);
    const QString canonical=info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty()?info.absoluteFilePath():canonical);
}
// Only the SQLite files belong to a database relocation. The source may
// be a parent or child of the destination; never delete the source recursively.
QString removeOldData(const QString &source){
    QStringList failures;
    for(const QString &name:{QStringLiteral("notes.sqlite"),
                              QStringLiteral("notes.sqlite-wal"),
                              QStringLiteral("notes.sqlite-shm")}){
        const QString file=QDir(source).filePath(name);
        if(QFileInfo::exists(file)&&!QFile::remove(file))failures<<file;
    }

    // Clean up recognizable logs left by older releases which stored
    // diagnostics beside notes. Do not remove unknown files or folders.
    if(normalizedDirectory(source)!=normalizedDirectory(diagnosticsDirectory())){
        const QString logs=QDir(source).filePath("logs");
        for(const QString &name:{QStringLiteral("app.log"),
                                  QStringLiteral("app.log.1"),
                                  QStringLiteral("app.log.2")}){
            const QString file=QDir(logs).filePath(name);
            if(QFileInfo::exists(file)&&!QFile::remove(file))failures<<file;
        }
        if(QDir(logs).exists())QDir().rmdir(logs);
        const QString crashes=QDir(source).filePath("crashes");
        for(const QString &name:QDir(crashes).entryList(
                {QStringLiteral("crash_*.dmp")},QDir::Files)){
            const QString file=QDir(crashes).filePath(name);
            if(!QFile::remove(file))failures<<file;
        }
        if(QDir(crashes).exists())QDir().rmdir(crashes);
    }
    // A nonempty source may legitimately contain the new child directory
    // or unrelated files. Leave it alone instead of reporting a failure.
    if(QDir(source).exists())QDir().rmdir(source);
    return failures.join("\n");
}
// Modal warnings and completion notices must remain above pinned note windows.
// A null-parent QMessageBox without this hint can be hidden behind a Stack.
void showLocationMessage(QMessageBox::Icon icon,const QString &text){
    QMessageBox box(icon,trUi("Change Data Location"),text,QMessageBox::Ok,
                    nullptr,Qt::Dialog|Qt::WindowStaysOnTopHint);
    box.exec();
}
}
void Controller::changeDataLocation(){
    const QString current=normalizedDirectory(stackiesDataDirectory());
    // Note windows can be permanently topmost. The application-modal dialog
    // must also be topmost so it never becomes inaccessible behind them.
    QDialog dialog(nullptr,Qt::Dialog|Qt::WindowStaysOnTopHint);
    dialog.setWindowTitle(trUi("Change Data Location"));
    auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(trUi("Data directory"),&dialog));
    auto *path=new QLineEdit(current,&dialog);
    path->setReadOnly(true);
    auto *choose=new QPushButton(trUi("Browse…"),&dialog);
    auto *selector=new QHBoxLayout;
    selector->addWidget(path,1);
    selector->addWidget(choose);
    layout->addLayout(selector);
    auto *restore=new QPushButton(trUi("Restore Default"),&dialog);
    layout->addWidget(restore,0,Qt::AlignLeft);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
    auto *apply=buttons->addButton(trUi("Apply"),QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(choose,&QPushButton::clicked,&dialog,[&]{
        const QString selected=QFileDialog::getExistingDirectory(
            &dialog,trUi("Choose data directory"),path->text());
        if(!selected.isEmpty())path->setText(normalizedDirectory(selected));
    });
    connect(restore,&QPushButton::clicked,&dialog,[&]{
        path->setText(normalizedDirectory(stackiesDefaultDataDirectory()));
    });
    connect(apply,&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    const QString target=normalizedDirectory(path->text());
    if(target==current)return;

    // Restoring the default location must work even after its old folder
    // was removed by a previous migration.
    const bool madeTarget=!QDir(target).exists();
    if(madeTarget&&!QDir().mkpath(target)){
        showLocationMessage(QMessageBox::Warning,
                             trUi("Cannot create the selected directory."));
        return;
    }
    const QString existingFile=QDir(target).filePath("notes.sqlite");
    const bool useExisting=QFileInfo::exists(existingFile);
    if(!useExisting){
        // Unrelated files in a chosen folder are fine, but never overwrite
        // another application's files with Stackies-managed names.
        for(const QString &name:{QStringLiteral("notes.sqlite-wal"),
                                 QStringLiteral("notes.sqlite-shm")}){
            if(QFileInfo::exists(QDir(target).filePath(name))){
                showLocationMessage(QMessageBox::Warning,
                    trUi("The selected folder contains files that conflict with Stackies data."));
                return;
            }
        }
    }

    enum class LocationChoice { Move, Merge, Switch, Overwrite };
    LocationChoice choice=LocationChoice::Move;
    if(useExisting){
        // Never offer to replace a corrupt or unrelated database.
        try{
            Storage candidate(existingFile.toStdString());
            candidate.verifyIntegrity();
            candidate.load();
            if(!candidate.hasSavedData())
                throw std::runtime_error("The selected database is not an initialized Stackies database");
        }catch(const std::exception &e){
            showLocationMessage(QMessageBox::Warning,
                trUi("Failed to change data location:\n%1").arg(QString::fromUtf8(e.what())));
            return;
        }
        QDialog options(nullptr,Qt::Dialog|Qt::WindowStaysOnTopHint);
        options.setWindowTitle(trUi("Change Data Location"));
        auto *optionsLayout=new QVBoxLayout(&options);
        optionsLayout->addWidget(new QLabel(trUi(
            "A Stackies database already exists in this folder. Choose how to proceed:"),&options));
        auto *merge=new QRadioButton(trUi("Merge — keep notes from both databases"),&options);
        auto *switchTo=new QRadioButton(trUi(
            "Switch — use the target database and keep the current one"),&options);
        auto *overwrite=new QRadioButton(trUi(
            "Overwrite — replace the target database with current notes"),&options);
        merge->setChecked(true);
        optionsLayout->addWidget(merge);
        optionsLayout->addWidget(switchTo);
        optionsLayout->addWidget(overwrite);
        auto *optionsButtons=new QDialogButtonBox(QDialogButtonBox::Cancel,&options);
        auto *proceed=optionsButtons->addButton(trUi("Continue"),QDialogButtonBox::AcceptRole);
        optionsLayout->addWidget(optionsButtons);
        connect(proceed,&QPushButton::clicked,&options,&QDialog::accept);
        connect(optionsButtons,&QDialogButtonBox::rejected,&options,&QDialog::reject);
        if(options.exec()!=QDialog::Accepted)return;
        choice=overwrite->isChecked()?LocationChoice::Overwrite:
               switchTo->isChecked()?LocationChoice::Switch:LocationChoice::Merge;
        if(choice==LocationChoice::Overwrite){
            QMessageBox warning(QMessageBox::Warning,trUi("Change Data Location"),
                trUi("Overwrite the existing database? A backup will be kept in the target folder."),
                QMessageBox::NoButton);
            warning.setWindowFlag(Qt::WindowStaysOnTopHint);
            auto *confirm=warning.addButton(trUi("Overwrite"),QMessageBox::DestructiveRole);
            warning.addButton(trUi("Cancel"),QMessageBox::RejectRole);
            warning.exec();
            if(warning.clickedButton()!=confirm)return;
        }
    }

    // Finish live editors and flush pending changes before changing databases.
    for(auto &[id,w]:windows_){Q_UNUSED(id);if(w)w->finishEdit();}
    saveTimer_.stop();
    if(!saveNow())return;
    // This operation must not copy a stale in-memory workspace over a newer
    // database revision that arrived after our last 30-second check.
    try{
        const auto diskRevision=storage_->revision();
        storage_->release();
        if(diskRevision!=databaseRevision_){
            showLocationMessage(QMessageBox::Warning,
                trUi("The database changed on another device. Wait for Stackies to reload before changing its location."));
            return;
        }
    }catch(const std::exception &e){
        showLocationMessage(QMessageBox::Warning,
            trUi("Failed to change data location:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }

    bool created=false;
    bool targetTouched=false;
    QString backupPath;
    // Move snapshots must be staged on the target filesystem for QFile::rename.
    // Merge rollback snapshots are short-lived; overwrite backups are retained.
    std::unique_ptr<QTemporaryDir> stage;
    try{
        if(choice==LocationChoice::Move || choice==LocationChoice::Merge){
            stage=std::make_unique<QTemporaryDir>(
                QDir(target).filePath(".stackies-relocate-XXXXXX"));
            if(!stage->isValid())
                throw std::runtime_error("Cannot create database staging directory");
        }
        if(choice==LocationChoice::Move){
            const QString snapshot=QDir(stage->path()).filePath("notes.sqlite");
            storage_->backupTo(snapshot.toStdString());
            {Storage verify(snapshot.toStdString());verify.verifyIntegrity();verify.load();}
            if(!QFile::rename(snapshot,existingFile))
                throw std::runtime_error("Cannot install migrated database");
            created=true;
        }else if(choice==LocationChoice::Merge){
            backupPath=QDir(stage->path()).filePath("original-target.sqlite");
            {
                Storage destination(existingFile.toStdString());
                destination.backupTo(backupPath.toStdString());
                const Workspace merged=mergeWorkspaces(destination.load(),workspace_);
                targetTouched=true;
                destination.save(merged); // One transaction; preserves target IDs and stack ordering.
                destination.verifyIntegrity();
            }
        }else if(choice==LocationChoice::Overwrite){
            backupPath=QDir(target).filePath(
                "notes.sqlite.backup-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")
                +"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".sqlite");
            {Storage destination(existingFile.toStdString());
             destination.backupTo(backupPath.toStdString());}
            targetTouched=true;
            storage_->backupTo(existingFile.toStdString());
        }
        // Switch simply reuses B; the other choices install data into B.
        {Storage verify(existingFile.toStdString());
         verify.verifyIntegrity();
         verify.load();
         if(!verify.hasSavedData())
             throw std::runtime_error("The selected database is not an initialized Stackies database");}

        QSettings settings;
        const QVariant previous=settings.value("storage/dataDirectory");
        if(target==normalizedDirectory(stackiesDefaultDataDirectory()))
            settings.setValue("storage/dataDirectory",QString());
        else settings.setValue("storage/dataDirectory",target);
        settings.sync();
        if(settings.status()!=QSettings::NoError){
            if(previous.isValid())settings.setValue("storage/dataDirectory",previous);
            else settings.remove("storage/dataDirectory");
            settings.sync();
            throw std::runtime_error("Cannot persist the new data directory setting");
        }

        // Do not save the now-stale source workspace into the new target at exit.
        quitting_=true;
        revisionTimer_.stop();
        storage_.reset();
        // Switch keeps A intact; merge/move/overwrite retire A after success.
        const QString leftovers=choice==LocationChoice::Switch?QString():removeOldData(current);
        QString result=trUi("Data location changed successfully. Restart Stackies to continue.");
        if(choice==LocationChoice::Overwrite)
            result+="\n\n"+trUi("Backup of the previous target database: %1").arg(backupPath);
        if(!leftovers.isEmpty())
            result+="\n\n"+trUi("Some old files could not be removed:\n%1").arg(leftovers);
        showLocationMessage(QMessageBox::Information,result);
        qApp->quit();
    }catch(const std::exception &e){
        QString failure=QString::fromUtf8(e.what());
        // If editing the existing target failed, restore its pre-operation
        // snapshot. A failed rollback never deletes the recovery backup.
        if(targetTouched&&!backupPath.isEmpty()){
            try{
                Storage previous(backupPath.toStdString());
                previous.backupTo(existingFile.toStdString());
            }catch(const std::exception &restoreError){
                failure+="\n"+trUi("Could not restore the target database from backup: %1")
                    .arg(QString::fromUtf8(restoreError.what()));
                failure+="\n"+backupPath;
            }
        }
        if(created){
            QFile::remove(existingFile);
            QFile::remove(existingFile+"-wal");
            QFile::remove(existingFile+"-shm");
        }
        if(madeTarget)QDir().rmdir(target);
        showLocationMessage(QMessageBox::Warning,
            trUi("Failed to change data location:\n%1").arg(failure));
    }
}

Controller::Controller(QObject *parent):QObject(parent){
    saveTimer_.setInterval(450);saveTimer_.setSingleShot(true);
    connect(&saveTimer_,&QTimer::timeout,this,&Controller::saveNow);
    revisionTimer_.setInterval(30000);
    connect(&revisionTimer_,&QTimer::timeout,this,&Controller::checkForExternalUpdates);
}
Controller::~Controller(){
    quitting_=true;
    saveTimer_.stop();
    revisionTimer_.stop();
    // All ordinary exit paths call saveNow before teardown. Never run an
    // unguarded workspace snapshot here after a conflict or a failed save.
    for(auto &[id,w]:windows_){Q_UNUSED(id);if(w)w->forceClose();}
    if(tray_)tray_->setContextMenu(nullptr);
    delete trayMenu_;
    delete preview_;
    clearTabDragPreview();
}
StickyWindow *Controller::window(const std::string &id) const {
    auto it=windows_.find(id);return it==windows_.end()?nullptr:it->second;
}
void Controller::initialize(){
    const QString data=stackiesDataDirectory();
    if(data.isEmpty())throw std::runtime_error("Cannot resolve application data directory");
    // The user migrates the existing database manually. Never silently
    // initialize fresh notes if an unmigrated database still exists under
    // Qt's previous .../Stackies/Stackies location.
    if(qgetenv("STACKIES_DATA_DIR").isEmpty()){
        const QString oldData=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if(QDir::cleanPath(oldData)!=QDir::cleanPath(data) &&
           QFileInfo::exists(oldData+"/notes.sqlite") &&
           !QFileInfo::exists(data+"/notes.sqlite")){
            throw std::runtime_error(
                QStringLiteral("Stackies data is still at the old location: %1. "
                               "Quit Stackies and copy the database to: %2")
                    .arg(oldData,data).toStdString());
        }
    }
    QDir().mkpath(data);
    storage_=std::make_unique<Storage>((data+"/notes.sqlite").toStdString());
    workspace_=storage_->load();
    if(!storage_->hasSavedData()&&workspace_.notes.empty()&&workspace_.trash.empty()){
        // A single short starter note is user-owned content after creation:
        // language changes must never overwrite anything the user edited.
        const QString title=initialWelcomeTitle();
        const auto id=workspace_.addNote(title.toUtf8().toStdString(),
                                         {160,120,375,260});
        workspace_.note(id).color="#F7E9BB";
        workspace_.note(id).html=initialWelcomeHtml().toUtf8().toStdString();
        storage_->save(workspace_);
    }
    databaseRevision_=storage_->revision();
    lastSavedWorkspace_=workspace_;
    storage_->release();
    revisionTimer_.start();
    preview_=new MergePreview();
    createTray();
    syncWindows();
    perf::memory("initialized");
    // Sample again after ordinary use. These timers exist only during
    // an explicitly enabled profiling run.
    if(perf::enabled()){
        QTimer::singleShot(30000,this,[]{perf::memory("elapsed-30s");});
        QTimer::singleShot(180000,this,[]{perf::memory("elapsed-180s");});
    }
    // Restore visibility without changing the saved hidden/topmost flags.
    QTimer::singleShot(0,this,[this]{bringVisibleWindowsForward();});
}
void Controller::createTray(){
    tray_=new QSystemTrayIcon(this);
    auto icon=qApp->windowIcon();
    tray_->setIcon(icon);
    tray_->setToolTip("Stackies");
    auto *menu=new TrayContextMenu();trayMenu_=menu;
    // Reduce the unused icon gutter while keeping enough room for checked
    // language items on the left and submenu arrows on the right.
    menu->setStyleSheet(
        "QMenu::item { padding: 5px 24px 5px 20px; }"
        "QMenu::item:selected:enabled { background:#DFE9F3; color:#263D50; }"
        "QMenu::separator { height: 1px; margin: 5px 8px; "
        "background: #B8BABB; }");
    auto *add=menu->addAction(trUi("New standalone note"));
    auto *showAll=menu->addAction(trUi("Show all notes"));
    auto *hideAll=menu->addAction(trUi("Hide all notes"));
    auto *recentlyDeleted=menu->addAction(trUi("Recently Deleted…"));
    menu->addSeparator();
    auto *location=menu->addAction(trUi("Change Data Location…"));
    menu->addSeparator();
    auto *languageMenu=menu->addMenu(trUi("Language"));
    auto *languageGroup=new QActionGroup(languageMenu);
    const QString selected=QSettings().value("ui/language","system").toString();
    const std::vector<std::pair<QString,QString>> choices={
        {"system",trUi("System default")},{"en",trUi("English")},
        {"zh_CN",trUi("简体中文")},{"zh_TW",trUi("繁體中文")},
        {"ja",trUi("日本語")},{"ko",trUi("한국어")},{"es",trUi("Español")}
    };
    const std::vector<QString> englishNames={
        QStringLiteral("System default"),QStringLiteral("English"),
        QStringLiteral("Chinese (Simplified)"),QStringLiteral("Chinese (Traditional)"),
        QStringLiteral("Japanese"),QStringLiteral("Korean"),QStringLiteral("Spanish")
    };
    for(size_t i=0;i<choices.size();++i){
        const auto &[key,nativeName]=choices[i];
        const QString label=readableLanguageLabel(nativeName,englishNames[i],
                                                    languageMenu->font());
        auto *action=languageMenu->addAction(label);
        action->setCheckable(true);
        action->setData(key);
        action->setChecked(key==selected);
        languageGroup->addAction(action);
        connect(action,&QAction::triggered,this,[key,label,languageGroup]{
            const QString code=key=="system"?systemUiLocale(QLocale::system()):key;
            if(!hasUiLanguageFont(code)){
                // QActionGroup checks the clicked action before 'triggered'.
                // Put the old checkmark back if the font guard rejects it.
                const QString previous=QSettings().value("ui/language","system").toString();
                for(QAction *item:languageGroup->actions()){
                    if(item->data().toString()==previous){
                        item->setChecked(true);
                        break;
                    }
                }
                QMessageBox::warning(
                    nullptr,trUi("Language"),
                    trUi("This system cannot display %1 with its installed fonts. "
                         "Install a compatible font (such as Noto Sans CJK for "
                         "Chinese, Japanese or Korean), then try again.").arg(label));
                return; // Preserve the previous setting and checked action.
            }
            QSettings().setValue("ui/language",key);
            QMessageBox::information(nullptr,trUi("Language"),
                                     trUi("Restart Stackies to apply the selected language."));
        });
    }
    menu->addSeparator();
    auto *quit=menu->addAction(trUi("Quit"));
    connect(location,&QAction::triggered,this,&Controller::changeDataLocation);
    connect(add,&QAction::triggered,this,[this]{createNote();});
    connect(showAll,&QAction::triggered,this,[this]{for(auto &s:workspace_.stacks)s.visible=true;syncWindows();bringVisibleWindowsForward();scheduleAllStacksSave();});
    connect(recentlyDeleted,&QAction::triggered,this,&Controller::showRecentlyDeleted);
    connect(hideAll,&QAction::triggered,this,[this]{for(auto &s:workspace_.stacks)s.visible=false;syncWindows();scheduleAllStacksSave();});
    connect(quit,&QAction::triggered,this,&Controller::quitSafely);
    // Force Qt stylesheet polish and action geometry calculation during
    // startup, rather than on the first tray right click.
    QElapsedTimer warmup;
    warmup.start();
    menu->ensurePolished();
    languageMenu->ensurePolished();
    (void)languageMenu->sizeHint();
    (void)menu->sizeHint();
    qInfo().noquote()<<"Tray menu prefetched in"<<warmup.elapsed()<<"ms";
    tray_->setContextMenu(menu);
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if(reason==QSystemTrayIcon::DoubleClick){for(auto &s:workspace_.stacks)s.visible=true;syncWindows();bringVisibleWindowsForward();scheduleAllStacksSave();}
    });
    if(QSystemTrayIcon::isSystemTrayAvailable())tray_->show();
}
void Controller::syncWindows(){
    if(syncing_)return;
    perf::Scope timing("Controller::syncWindows");
    syncing_=true;
    std::unordered_set<std::string> valid;
    for(const auto &s:workspace_.stacks)valid.insert(s.id);
    for(auto it=windows_.begin();it!=windows_.end();){
        if(!valid.count(it->first)){
            auto *w=it->second;
            w->forceClose();w->deleteLater();it=windows_.erase(it);
        }else ++it;
    }
    for(const auto &s:workspace_.stacks){
        auto *w=window(s.id);
        if(applyingExternalUpdate_ && w &&
           !externallyChangedStacks_.count(s.id))continue;
        if(!w){w=new StickyWindow(this,s.id);windows_.emplace(s.id,w);}
        if(applyingExternalUpdate_){
            const QRect target(s.rect.x,s.rect.y,s.rect.w,s.rect.h);
            if(w->geometry()!=target)w->setGeometry(target);
        }
        w->refresh();
        if(!s.visible){
            w->hide();
        }else if(overviewSourceHidden_ && overviewStackId_==s.id){
#ifdef Q_OS_LINUX
            // The X11 minimize/restore handshake is asynchronous; detaching
            // from Overview can immediately sync windows and leave the source
            // iconified. On composited X11 keep it mapped but fully
            // transparent; native Wayland falls back to hiding it.
            if(QGuiApplication::platformName()==QStringLiteral("xcb"))
                w->setWindowOpacity(0.0);
            else
                w->hide();
#else
            // Keep the Windows taskbar entry during Overview.
            if(!w->isMinimized())w->showMinimized();
#endif
        }else if(w->isHidden()){
            // Showing an explicitly hidden Stack restores it normally.
            w->showNormal();
        }else if(!w->isMinimized()){
            // Do not undo individual minimization during a workspace refresh.
            w->show();
        }
    }
    syncing_=false;
}
void Controller::activateFromSecondInstance(){
    if(workspace_.stacks.empty()){
        createNote();
        return;
    }
    if(overview_){
        overview_->show();
        overview_->raise();
        overview_->activateWindow();
        return;
    }
    // If every note was hidden, relaunching the app should restore at least
    // one window without discarding the user's other hidden flags.
    const bool anyVisible=std::any_of(workspace_.stacks.begin(),workspace_.stacks.end(),
        [](const Stack &s){return s.visible;});
    if(!anyVisible && !workspace_.stacks.empty()){
        workspace_.stacks.front().visible=true;
        const auto id=workspace_.stacks.front().id;
        syncWindows();
        scheduleStackSave(id);
    }
    bringVisibleWindowsForward();
}
void Controller::bringVisibleWindowsForward(){
    QWidget *last=nullptr;
    // Raise as a group, respecting the saved permanent topmost bit.
    for(const auto &s:workspace_.stacks){
        auto *w=window(s.id);
        if(!w||!s.visible ||
           (overviewSourceHidden_ && overviewStackId_==s.id))continue;
        if(w->isMinimized())w->showNormal();
        else w->show();
        platform::raiseStack(w,s.topmost);
        last=w;
    }
    if(last)last->activateWindow();
}
void Controller::deleteNote(const std::string &id){
    // Defer UI destruction out of tab/header context-menu event delivery.
    QTimer::singleShot(0,this,[this,id]{
        auto it=std::find_if(workspace_.notes.begin(),workspace_.notes.end(),
            [&id](const Note &n){return n.id==id;});
        if(it==workspace_.notes.end())return;
        const auto source=workspace_.owner(id).id;
        if(auto*w=window(source))w->finishEdit();
        workspace_.deleteNote(id,QDateTime::currentMSecsSinceEpoch());
        syncWindows();scheduleSave();
    });
}
void Controller::showRecentlyDeleted(){
    auto *dialog=new QDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(trUi("Recently Deleted — Stackies"));
    dialog->resize(480,350);
    auto *layout=new QVBoxLayout(dialog);
    auto *hint=new QLabel(trUi("Deleted notes stay here until you permanently remove them."),dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto *list=new QListWidget(dialog);layout->addWidget(list,1);
    auto *actions=new QHBoxLayout;
    auto *restore=new QPushButton(trUi("Restore"),dialog);
    auto *remove=new QPushButton(trUi("Delete permanently…"),dialog);
    auto *clear=new QPushButton(trUi("Empty trash…"),dialog);
    auto *close=new QPushButton(trUi("Close"),dialog);
    actions->addWidget(restore);actions->addWidget(remove);
    actions->addWidget(clear);actions->addStretch();actions->addWidget(close);
    layout->addLayout(actions);
    auto populate=[this,list,restore,remove,clear]{
        list->clear();
        for(const auto &entry:workspace_.trash){
            const auto date=QDateTime::fromMSecsSinceEpoch(entry.deletedAt).toString("yyyy-MM-dd HH:mm");
            auto *item=new QListWidgetItem(qstr(entry.note.title)+"  —  "+date,list);
            item->setData(Qt::UserRole,qstr(entry.note.id));
            item->setToolTip(qstr(entry.note.title));
        }
        restore->setEnabled(false);remove->setEnabled(false);
        clear->setEnabled(!workspace_.trash.empty());
    };
    populate();
    connect(list,&QListWidget::itemSelectionChanged,dialog,[list,restore,remove]{
        const bool selected=list->currentItem()!=nullptr;
        restore->setEnabled(selected);remove->setEnabled(selected);
    });
    connect(restore,&QPushButton::clicked,dialog,[this,list,populate]{
        if(!list->currentItem())return;
        const auto id=list->currentItem()->data(Qt::UserRole).toString().toStdString();
        const auto stackId=workspace_.restoreNote(id);
        syncWindows();scheduleSave();
        if(auto*w=window(stackId)){w->show();w->raise();w->activateWindow();}
        populate();
    });
    connect(remove,&QPushButton::clicked,dialog,[this,dialog,list,populate]{
        if(!list->currentItem())return;
        if(QMessageBox::question(dialog,trUi("Permanently delete note"),
              trUi("This note cannot be recovered after permanent deletion. Continue?"))!=QMessageBox::Yes)return;
        const auto id=list->currentItem()->data(Qt::UserRole).toString().toStdString();
        workspace_.permanentlyDelete(id);scheduleSave();populate();
    });
    connect(clear,&QPushButton::clicked,dialog,[this,dialog,populate]{
        if(QMessageBox::question(dialog,trUi("Empty trash"),
              trUi("Permanently delete all notes in Recently Deleted? This cannot be undone."))!=QMessageBox::Yes)return;
        workspace_.trash.clear();scheduleSave();populate();
    });
    connect(close,&QPushButton::clicked,dialog,&QDialog::close);
    dialog->show();dialog->raise();dialog->activateWindow();
}
void Controller::createNote(const std::string &targetStackId){
    Rect pos{190+static_cast<int>(workspace_.notes.size()%8)*27,145+static_cast<int>(workspace_.notes.size()%8)*24,360,295};
    if(!targetStackId.empty())pos=stack(targetStackId).rect;
    const auto noteId=workspace_.addNote("New Note",pos);
    workspace_.note(noteId).color="#F6E6B7";
    if(!targetStackId.empty())workspace_.moveNote(noteId,targetStackId,0);
    auto ownerId=workspace_.owner(noteId).id;
    workspace_.activate(ownerId,noteId);
    syncWindows();
    if(auto *w=window(ownerId)){w->show();w->raise();w->activateWindow();w->beginEdit();}
    scheduleSave();
}
void Controller::activate(const std::string& stackId,const std::string &id){
    if(auto*w=window(stackId))w->finishEdit();
    workspace_.activate(stackId,id);
    if(auto*w=window(stackId))w->refresh();
    scheduleStackSave(stackId);
}
void Controller::setNoteTitle(const std::string& id,const QString &title){
    const auto text=title.trimmed();
    if(text.isEmpty()||qstr(note(id).title)==text)return;
    workspace_.renameTitle(id,text.toStdString());
    syncWindows();scheduleNoteSave(id);
}
void Controller::renameTab(const std::string &id){
    bool ok=false;
    auto text=QInputDialog::getText(window(workspace_.owner(id).id),
        trUi("Rename index tab"),trUi("Short index label:"),QLineEdit::Normal,qstr(note(id).tab),&ok);
    if(!ok||text.trimmed().isEmpty())return;
    workspace_.renameTab(id,text.trimmed().toStdString());
    syncWindows();scheduleNoteSave(id);
}
void Controller::setFollowTitle(const std::string& id,bool follow){
    workspace_.setFollowTitle(id,follow);
    syncWindows();scheduleNoteSave(id);
}
void Controller::setNoteColor(const std::string& id,const QColor& color){
    if(!color.isValid())return;
    auto hex=color.name().toUpper();
    workspace_.note(id).color=hex.toStdString();
    rememberColor(hex);
    syncWindows();scheduleNoteSave(id);
}
void Controller::setStackOpacity(const std::string &id,int percent){
    const int clamped=std::clamp(percent,50,100);
    auto &s=workspace_.stack(id);
    if(s.opacity==clamped)return;
    s.opacity=clamped;
    if(auto*w=window(id))w->refresh();
    scheduleStackSave(id);
}
void Controller::recolorNote(const std::string &id){
    showColorPalette(window(workspace_.owner(id).id),false,
        QColor(qstr(note(id).color)),[this,id](const QColor &color){
            setNoteColor(id,color);
        });
}
void Controller::addColorActions(QMenu *menu,const std::string &id){
    auto *label=menu->addAction(trUi("Note color"));
    label->setEnabled(false);
    auto *widget=new QWidget(menu);
    auto *row=new QHBoxLayout(widget);
    row->setContentsMargins(8,4,8,4);
    row->setSpacing(4);
    for(const auto &hex:quickPalette()){
        auto *b=swatch(QColor(hex),widget,25);
        row->addWidget(b);
        connect(b,&QPushButton::clicked,menu,[this,id,hex,menu]{
            menu->close();setNoteColor(id,QColor(hex));
        });
    }
    auto *widgetAction=new QWidgetAction(menu);
    widgetAction->setDefaultWidget(widget);
    menu->addAction(widgetAction);
    auto *more=menu->addAction(trUi("More colors…"));
    connect(more,&QAction::triggered,this,[this,id]{recolorNote(id);});
    menu->addSeparator();
    const auto stackId=workspace_.owner(id).id;
    auto *opacityWidget=new QWidget(menu);
    auto *opacityLayout=new QVBoxLayout(opacityWidget);
    opacityLayout->setContentsMargins(12,6,12,9);
    opacityLayout->setSpacing(6);
    auto *opacityRow=new QHBoxLayout;
    auto *opacityLabel=new QLabel(trUi("Stack opacity"),opacityWidget);
    auto *opacityValue=new QLabel(QString::number(stack(stackId).opacity)+"%",opacityWidget);
    QFont opacityFont=opacityLabel->font();
    opacityFont.setPixelSize(12);
    opacityFont.setWeight(QFont::DemiBold);
    opacityLabel->setFont(opacityFont);
    opacityLabel->setStyleSheet("color:#435460;");
    opacityValue->setStyleSheet("color:#435460;font-size:12px;");
    opacityRow->addWidget(opacityLabel);
    opacityRow->addStretch();
    opacityRow->addWidget(opacityValue);
    opacityLayout->addLayout(opacityRow);
    auto *slider=new QSlider(Qt::Horizontal,opacityWidget);
    slider->setMinimumWidth(230);
    slider->setRange(50,100);
    slider->setSingleStep(1);
    slider->setPageStep(5);
    slider->setValue(stack(stackId).opacity);
    slider->setToolTip(trUi("Paper only — text stays opaque"));
    slider->setStyleSheet(
        "QSlider{min-height:20px;}"
        "QSlider::groove:horizontal{height:4px;background:#DAE3E9;border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#8BB6D3;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#F8FBFD;border:1.5px solid #6796B9;"
        "width:13px;height:13px;margin:-6px 0;border-radius:7px;}"
        "QSlider::handle:horizontal:hover{background:#EAF4FA;border-color:#477FA9;}");
    opacityLayout->addWidget(slider);
    auto *opacityAction=new QWidgetAction(menu);
    opacityAction->setDefaultWidget(opacityWidget);
    menu->addAction(opacityAction);
    connect(slider,&QSlider::valueChanged,menu,[this,stackId,opacityValue](int value){
        opacityValue->setText(QString::number(value)+"%");
        setStackOpacity(stackId,value);
    });

}
void Controller::reorderNotes(const std::string& id,int from,int to){
    if(from==to)return;
    workspace_.reorder(id,from,to);
    if(auto*w=window(id))w->refresh();
    scheduleSave();
}
void Controller::markInteractiveEdit(const std::string &id){
    locallyEditedNotes_.insert(id);
}
void Controller::updateHtml(const std::string &id,const QString &html){
    auto &n=workspace_.note(id);
    const auto value=html.toStdString();
    if(n.html==value)return;
    n.html=value;scheduleNoteSave(id);
}
void Controller::saveGeometry(const std::string &id,const QRect &rect){
    if(applyingExternalUpdate_)return;
    try{
        auto &s=workspace_.stack(id);
        if(s.rect.x==rect.x()&&s.rect.y==rect.y()&&s.rect.w==rect.width()&&s.rect.h==rect.height())return;
        s.rect={rect.x(),rect.y(),rect.width(),rect.height()};scheduleStackSave(id);
    }catch(const std::out_of_range&){/* destroyed during window teardown */}
}
void Controller::hideStack(const std::string& id){
    workspace_.stack(id).visible=false;
    if(auto*w=window(id))w->hide();
    scheduleStackSave(id);
    if(!QSystemTrayIcon::isSystemTrayAvailable()){
        // Without a tray, keep at least one accessible window.
        bool any=false;for(const auto& s:workspace_.stacks)any|=s.visible;
        if(!any){workspace_.stack(id).visible=true;if(auto*w=window(id))w->show();}
    }
}
void Controller::minimizeStack(const std::string &id){
    // Minimized is transient window presentation, not persisted visibility.
    if(auto *w=window(id);w && stack(id).visible)w->showMinimized();
}
void Controller::toggleTopmost(const std::string &id){
    if(auto *w=window(id))w->finishEdit();
    auto &s=workspace_.stack(id);s.topmost=!s.topmost;
    if(auto *w=window(id)){
        platform::setStackTopmost(w,s.topmost);
        w->refresh();
    }
    scheduleStackSave(id);
}
void Controller::showOverview(const std::string& id){
    if(overview_)return; // Exactly one expanded stack at a time.
    if(auto*w=window(id))w->finishEdit();
    auto *g=new OverviewOverlay(this,id);
    overview_=g;
    overviewStackId_=id;
    connect(g,&QObject::destroyed,this,[this,id]{
        overview_=nullptr;
        overviewSourceHidden_=false;
        overviewStackId_.clear();
        perf::memory("overview-destroyed");
        // Also handle abnormal dismissal without changing persisted visibility.
        if(auto*w=window(id);w && stack(id).visible){
#ifdef Q_OS_LINUX
            // Overview uses transparent or hidden Linux windows, never a
            // transient minimization. Recover even after abnormal dismissal.
            w->setWindowOpacity(1.0);
            if(w->isHidden()||w->isMinimized())w->showNormal();
            else w->show();
            if(QGuiApplication::platformName()!=QStringLiteral("offscreen")){
                w->raise();
                w->activateWindow();
            }
#else
            if(w->isMinimized())w->showNormal();
            else w->show();
#endif
        }
    });
    // OverviewOverlay takes over the source at its first painted frame.
    // Minimizing here created a visible gap before the queued animation.
    g->open();
}
void Controller::setOverviewStackPresentation(const std::string &id,bool glassIsOpen){
    if(!overview_||overviewStackId_!=id)return;
    overviewSourceHidden_=glassIsOpen;
    if(auto*w=window(id)){
        if(glassIsOpen){
#ifdef Q_OS_LINUX
            // Do not minimize X11 source windows: Linux WMs can process the
            // subsequent restore after syncWindows() and leave them iconified.
            // GNOME/KWin composite zero opacity without unmapping the window
            // or losing its taskbar identity. Wayland has no such guarantee.
            if(stack(id).visible){
                if(QGuiApplication::platformName()==QStringLiteral("xcb"))
                    w->setWindowOpacity(0.0);
                else
                    w->hide();
            }
#else
            // Keep the normal top-level window and its taskbar button alive.
            if(stack(id).visible && !w->isMinimized())w->showMinimized();
#endif
        }else if(stack(id).visible){
#ifdef Q_OS_LINUX
            w->setWindowOpacity(1.0);
#endif
            w->showNormal();
            w->raise();
        }
    }
}
bool Controller::requestOverviewTaskbarRestore(const std::string &id){
    if(!overviewSourceHidden_ || !overview_ || overviewStackId_!=id)
        return false;
    // Clicking the source Stack's taskbar button dismisses its Overview.
    overview_->closeAnimated();
    return true;
}
std::string Controller::targetAt(const QPoint &p,const std::string &exclude) const {
    std::vector<QWidget*> candidates;
    candidates.reserve(windows_.size());
    for(const auto &[id,w]:windows_)if(id!=exclude && w)candidates.push_back(w);
    QWidget *matched=platform::stackAt(p,candidates,window(exclude),preview_,tabDragPreview_);
    if(!matched)return {};
    for(const auto &[id,w]:windows_)if(w==matched)return id;
    return {};
}

void Controller::showPreview(const QRect &rect,bool ready){
    auto *p=static_cast<MergePreview*>(preview_);
    p->setGeometry(rect.adjusted(-12,-12,12,12));
    p->setReady(ready);
    p->show();p->raise();
}
void Controller::hidePreview(){
    if(preview_)static_cast<MergePreview*>(preview_)->reset();
    previewTarget_.clear();
}
void Controller::beginWindowDrag(const std::string& source){
    dragging_=source;
    temporaryDragTopmost_=false;
    if(auto *w=window(source))
        temporaryDragTopmost_=platform::raiseForDrag(w,stack(source).topmost);
    hoverTarget_.clear();
    enteredAt_=0;
    hidePreview();
}

void Controller::windowDragged(const std::string& source,const QPoint &global){
    if(dragging_!=source)beginWindowDrag(source);
    const auto target=targetAt(global,source);
    if(target.empty()){
        // Moving out (or into an occluding application) cancels merge readiness.
        hoverTarget_.clear();
        enteredAt_=0;
        hidePreview();
        return;
    }
    if(target!=hoverTarget_){
        hoverTarget_=target;
        enteredAt_=QDateTime::currentMSecsSinceEpoch();
        const qint64 hoverStartedAt=enteredAt_;
        hidePreview();
        if(auto*w=window(target))showPreview(w->frameGeometry(),false);
        QTimer::singleShot(kMergeHoverMs,this,[this,target,source,hoverStartedAt]{
            // A pause must be continuous on the *same* visible target.
            // Checking the timestamp also rejects stale timers after
            // leaving and re-entering the same target.
            if(dragging_!=source || hoverTarget_!=target ||
               enteredAt_!=hoverStartedAt)return;
            if(targetAt(QCursor::pos(),source)!=target){
                hoverTarget_.clear();
                enteredAt_=0;
                hidePreview();
                return;
            }
            if(auto*w=window(target)){
                previewTarget_=target;
                showPreview(w->frameGeometry(),true);
            }
        });
    }
}
void Controller::finishWindowDrag(const std::string& source,const QPoint &global,bool dragged){
    // Overlap on an ordinary mouse release must never merge. A preview
    // means the full deliberate hover interval has already elapsed.
    if(dragged&&source==dragging_&&!previewTarget_.empty()&&targetAt(global,source)==previewTarget_){
        // StickyWindow::rebuildTabs compensates for the rail width when
        // the target first becomes a multi-note stack; never restore the old
        // outer window width here (that would squeeze the paper by 70px).
        const std::string targetId=previewTarget_;
        if(auto*w=window(source))w->finishEdit();
        if(auto*w=window(targetId))w->finishEdit();
        workspace_.mergeStacks(source,targetId);
        hidePreview();syncWindows();
        scheduleSave();
    }
    if(temporaryDragTopmost_)
        platform::restoreAfterDrag(window(source),true);
    temporaryDragTopmost_=false;
    dragging_.clear();hoverTarget_.clear();enteredAt_=0;hidePreview();
}
QRect Controller::tabDragRect(const std::string& source,const QPoint& global) const {
    const auto &r=stack(source).rect;
    // Preserve the note's visible paper size when tearing it out of a stack.
    const int width=std::max(260,r.w-70);
    const int height=std::max(220,r.h);
    return {global.x()-std::min(125,width/2),global.y()-25,width,height};
}
void Controller::clearTabDragPreview(){
    delete tabDragPreview_;
    tabDragPreview_=nullptr;
    tabDragNoteId_.clear();
}
void Controller::tabDragged(const std::string &source,const std::string& noteId,const QPoint &global){
    hidePreview();
    auto *original=window(source);
    if(!original)return;
    if(original->tabBarGlobalRect().contains(global)){
        original->previewTabReorder(noteId,global);
        if(tabDragPreview_)tabDragPreview_->hide();
        return;
    }
    original->clearTabReorder();
    if(original->frameGeometry().contains(global)){
        if(tabDragPreview_)tabDragPreview_->hide();
        return;
    }
    const QRect targetRect=tabDragRect(source,global);
    if(!tabDragPreview_||tabDragNoteId_!=noteId){
        clearTabDragPreview();
        tabDragPreview_=new FloatingNoteDrag(note(noteId),targetRect.size());
        tabDragNoteId_=noteId;
    }
    tabDragPreview_->setGeometry(targetRect);
    if(!tabDragPreview_->isVisible()){
        tabDragPreview_->show();
        tabDragPreview_->raise();
    }
}
void Controller::tabReleased(const std::string &source,const std::string& noteId,const QPoint &global,bool dragged){
    qDebug("Tab released: moved=%d selected=%d",int(dragged),int(stack(source).activeId==noteId));
    clearTabDragPreview();
    hidePreview();
    if(!dragged){activate(source,noteId);return;}
    auto *original=window(source);
    if(!original)return;
    // Within the tab rail: commit the drop position shown by the live gap.
    if(original->tabBarGlobalRect().contains(global)){
        const int insertion=original->tabInsertionAt(global);
        original->clearTabReorder();
        const auto &ids=stack(source).noteIds;
        const auto it=std::find(ids.begin(),ids.end(),noteId);
        if(it!=ids.end()){
            const int from=static_cast<int>(it-ids.begin());
            const int to=std::clamp(insertion>from?insertion-1:insertion,0,static_cast<int>(ids.size())-1);
            reorderNotes(source,from,to);
        }
        return;
    }
    original->clearTabReorder();
    if(original->frameGeometry().contains(global))return;
    const auto target=targetAt(global,source);
    original->finishEdit();
    if(!target.empty()){
        if(auto*w=window(target))w->finishEdit();
        workspace_.moveNote(noteId,target,0);
    }else{
        // The real note appears exactly where the solid dragged copy was.
        const auto g=tabDragRect(source,global);
        qDebug("Tab detach: changing stack membership");
        workspace_.detach(noteId,{g.x(),g.y(),g.width(),g.height()});
        qDebug("Tab detach: model updated");
    }
    qDebug("Tab release: synchronizing windows");
    syncWindows();
    qDebug("Tab release: windows synchronized");
    scheduleSave();
}
void Controller::detachFromOverview(const std::string &source,const std::string &noteId,const QRect &rect){
    qDebug("Overview drop: selected=%d",int(stack(source).activeId==noteId));
    if(rect.width()<100||rect.height()<100)return;
    if(workspace_.owner(noteId).id!=source)return;
    if(auto *w=window(source))w->finishEdit();
    const bool single=workspace_.stack(source).noteIds.size()==1;
    workspace_.detach(noteId,{rect.x(),rect.y(),rect.width(),rect.height()});
    if(single)if(auto *w=window(source))w->setGeometry(rect);
    qDebug("Overview drop: model updated, synchronizing windows");
    syncWindows();
    qDebug("Overview drop: windows synchronized");
    scheduleSave();
}
void Controller::scheduleSave(){
    if(!storage_)return;
    fullSaveRequested_=true;
    saveTimer_.start();
}
void Controller::scheduleNoteSave(const std::string &id){
    if(!storage_)return;
    dirtyNoteIds_.insert(id);
    saveTimer_.start();
}
void Controller::scheduleStackSave(const std::string &id){
    if(!storage_)return;
    dirtyStackIds_.insert(id);
    saveTimer_.start();
}
void Controller::scheduleAllStacksSave(){
    if(!storage_)return;
    for(const auto &s:workspace_.stacks)dirtyStackIds_.insert(s.id);
    saveTimer_.start();
}

void Controller::checkForExternalUpdates(){
    if(quitting_ || !storage_ || syncing_ || applyingExternalUpdate_ ||
       fullSaveRequested_ || !dirtyNoteIds_.empty() || !dirtyStackIds_.empty() ||
       saveTimer_.isActive() || overview_ || !dragging_.empty() || !tabDragNoteId_.empty())
        return;
    for(const auto &[id,w]:windows_){
        Q_UNUSED(id);
        if(w && w->hasActiveEditor())return;
    }
    try{
        const std::string current=storage_->revision();
        if(current==databaseRevision_){
            storage_->release();
            return;
        }
        // A new SQLite connection observes an externally replaced file,
        // unlike a long-lived connection retaining the old file handle.
        Workspace incoming=storage_->load();
        if(storage_->revision()!=current){
            storage_->release();
            return; // Data changed again during read; retry next interval.
        }
        storage_->release();
        // The same note may have been edited AND successfully auto-saved while
        // an editor was open. Preserve that local content as a separate note
        // before accepting a later incoming version of the same note.
        const std::size_t copies=appendConflictCopies(
            incoming,lastSavedWorkspace_,workspace_,
            trUi(" (conflict copy)").toUtf8().toStdString(),
            &locallyEditedNotes_);
        if(copies)
            storage_->save(incoming,&current); // Atomic revision-checked merge.
        // Capture the revision belonging to the snapshot we actually loaded,
        // not a later external write that could arrive while repainting UI.
        const std::string reloadedRevision=copies?storage_->revision():current;
        storage_->release();

        // Only repaint windows whose stack metadata or visible note content
        // differs. Source/target stacks can be added or removed.
        externallyChangedStacks_.clear();
        for(const auto &s:incoming.stacks){
            auto old=std::find_if(workspace_.stacks.begin(),workspace_.stacks.end(),
                [&](const Stack &v){return v.id==s.id;});
            bool changed=old==workspace_.stacks.end();
            if(!changed){
                changed=old->noteIds!=s.noteIds || old->activeId!=s.activeId ||
                    old->visible!=s.visible || old->topmost!=s.topmost ||
                    old->opacity!=s.opacity || old->rect.x!=s.rect.x ||
                    old->rect.y!=s.rect.y || old->rect.w!=s.rect.w ||
                    old->rect.h!=s.rect.h;
            }
            if(!changed){
                for(const auto &noteId:s.noteIds){
                    const auto earlier=std::find_if(workspace_.notes.begin(),workspace_.notes.end(),
                        [&](const Note &n){return n.id==noteId;});
                    const auto latest=std::find_if(incoming.notes.begin(),incoming.notes.end(),
                        [&](const Note &n){return n.id==noteId;});
                    if(earlier==workspace_.notes.end()||latest==incoming.notes.end()||
                        earlier->title!=latest->title || earlier->tab!=latest->tab ||
                        earlier->color!=latest->color || earlier->html!=latest->html ||
                        earlier->followTitle!=latest->followTitle){
                        changed=true;
                        break;
                    }
                }
            }
            if(changed)externallyChangedStacks_.insert(s.id);
        }
        applyingExternalUpdate_=true;
        workspace_=std::move(incoming);
        syncWindows();
        applyingExternalUpdate_=false;
        externallyChangedStacks_.clear();
        databaseRevision_=reloadedRevision;
        lastSavedWorkspace_=workspace_;
        locallyEditedNotes_.clear();
        if(copies && tray_ && tray_->isVisible())
            tray_->showMessage(trUi("Stackies"),
                trUi("An external update was detected. Your local edits were kept as separate notes."),
                QSystemTrayIcon::Information,5000);
        qInfo("Reloaded external Stackies database revision; %llu local conflict notes preserved",
              static_cast<unsigned long long>(copies));
    }catch(const std::exception &e){
        applyingExternalUpdate_=false;
        externallyChangedStacks_.clear();
        qWarning("External database update check failed: %s",e.what());
        try{storage_->release();}catch(const std::exception &closeError){
            qWarning("Cannot release database after failed revision check: %s",closeError.what());
        }
    }
}
bool Controller::saveNow(){
    if(!storage_)return true;
    const bool pending=fullSaveRequested_ ||
        !dirtyNoteIds_.empty()||!dirtyStackIds_.empty();
    if(!pending){
        saveTimer_.stop(); // Manual flush may have left a queued timer.
        return true;
    }
    perf::Scope timing("Storage::save workspace");
    const bool partial=!fullSaveRequested_;
    try{
        if(partial)storage_->saveChanged(workspace_,dirtyNoteIds_,dirtyStackIds_,&databaseRevision_);
        else storage_->save(workspace_,&databaseRevision_);
        databaseRevision_=storage_->revision();
        storage_->release();
        lastSavedWorkspace_=workspace_;
        // saveNow() is also called directly (quit, migration, tests).
        // Stop the old debounce timer after a successful immediate flush:
        // an otherwise idle poll must not mistake it for pending edits.
        saveTimer_.stop();
        if(perf::enabled())
            qInfo().noquote()<<"PERF save mode"<<(partial?"partial":"full")
                             <<"note-rows"<<dirtyNoteIds_.size()
                             <<"stack-rows"<<dirtyStackIds_.size();
    }catch(const DatabaseRevisionConflict &){
        // External data wins. Preserve every locally modified/new note as an
        // independent note instead of asking users to recover a SQLite file.
        // Never copy the stale entire workspace over the external database.
        try{
            storage_->release();
            // Flush any last editor keystrokes before collecting local copies.
            for(auto &[id,w]:windows_){
                Q_UNUSED(id);
                if(w)w->finishEdit();
            }
            Workspace latest=storage_->load();
            const std::string expected=storage_->revision();
            // Include notes edited earlier in this session, even if the
            // debounce timer already saved them and advanced the baseline.
            // Also copy newly pending non-editor changes (title, color, etc.).
            const std::size_t copies=appendConflictCopies(
                latest,lastSavedWorkspace_,workspace_,
                trUi(" (conflict copy)").toUtf8().toStdString(),
                &locallyEditedNotes_);
            if(copies)storage_->save(latest,&expected);
            databaseRevision_=storage_->revision();
            storage_->release();

            workspace_=std::move(latest);
            lastSavedWorkspace_=workspace_;
            locallyEditedNotes_.clear();
            saveTimer_.stop();
            fullSaveRequested_=false;
            dirtyNoteIds_.clear();
            dirtyStackIds_.clear();
            if(!quitting_){
                syncWindows();
                if(copies){
                    bringVisibleWindowsForward();
                    if(tray_ && tray_->isVisible())
                        tray_->showMessage(trUi("Stackies"),
                            trUi("An external update was detected. Your local edits were kept as separate notes."),
                            QSystemTrayIcon::Information,5000);
                }
            }
            qInfo("External save conflict resolved with %llu separate notes",
                  static_cast<unsigned long long>(copies));
            return true;
        }catch(const std::exception &e){
            try{storage_->release();}catch(...){}
            qCritical("Conflict-copy save failed: %s",e.what());
            QMessageBox::warning(nullptr,trUi("Stackies - save failed"),
                trUi("Changes are still in memory, but could not be saved:\n%1").arg(e.what()));
            return false;
        }
    }catch(const std::exception &e){
        if(partial)fullSaveRequested_=true;
        try{storage_->release();}catch(const std::exception &closeError){
            qWarning("Cannot release SQLite after save failure: %s",closeError.what());
        }
        qCritical("Save failed: %s",e.what());
        QMessageBox::warning(nullptr,trUi("Stackies - save failed"),
                             trUi("Changes are still in memory, but could not be saved:\n%1").arg(e.what()));
        return false;
    }
    fullSaveRequested_=false;
    dirtyNoteIds_.clear();
    dirtyStackIds_.clear();
    return true;
}
void Controller::prepareForShutdown(){
    if(quitting_)return;
    quitting_=true;
    for(auto &[id,w]:windows_){Q_UNUSED(id);w->finishEdit();}
    saveTimer_.stop();
    revisionTimer_.stop();
    // Shutting down Windows must not be interpreted as hiding a Stack.
    // The last successfully saved visibility is authoritative even on forced exit.
    saveNow();
}
void Controller::quitSafely(){
    for(auto &[id,w]:windows_){Q_UNUSED(id);w->finishEdit();}
    saveTimer_.stop();
    if(saveNow()){
        revisionTimer_.stop();
        quitting_=true; // Qt sends closeEvent to visible windows on quit.
        qApp->quit();
    }
}
}
