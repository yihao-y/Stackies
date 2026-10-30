#include "controller.h"
#include "datapath.h"
#include "diagnostics.h"
#include "window.h"
#include "overview.h"
#include "storage.h"
#include "i18n.h"
#include "checklistpaint.h"
#include "glassreflection.h"
#include <QApplication>
#include <QFont>
#include <QFontInfo>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QToolTip>
#include <QCoreApplication>
#include <QSettings>
#include <QEvent>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QDebug>
#include <iostream>
#include <stdexcept>

#define VERIFY(condition) do { if(!(condition)) throw std::runtime_error("UI test failed at line " + std::to_string(__LINE__) + ": " #condition); } while(0)

int main(int argc,char **argv){
    // Keep the smoke test headless, non-interactive and isolated from real notes.
    qputenv("QT_QPA_PLATFORM","offscreen");
    QTemporaryDir data;
    if(!data.isValid())return 2;
    // Never let a user's GUI preference override the test's temp database.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
    qputenv("STACKIES_DATA_DIR",data.path().toUtf8());
    QApplication app(argc,argv);
    app.setOrganizationName("Stackies");
    app.setApplicationName("Stackies");
    QApplication::setQuitOnLastWindowClosed(false);
    try{
        // Notes follow their data setting; diagnostics remain machine-local.
        // Only a duplicated trailing organization/application component
        // is collapsed; the Roaming/Local OS base is never changed.
        VERIFY(sticky::stackiesDataDirectory()==data.path());
        const QString localDiagnostics=sticky::diagnosticsDirectory();
        VERIFY(!localDiagnostics.isEmpty());
        VERIFY(localDiagnostics!=data.path());
        // GUI selection, including an explicit default, outranks the environment.
        {
            QSettings preferences;
            VERIFY(!preferences.contains("storage/dataDirectory"));
            const QString selected=QDir(data.path()).filePath("selected-directory");
            preferences.setValue("storage/dataDirectory",selected);
            VERIFY(sticky::stackiesDataDirectory()==selected);
            VERIFY(sticky::diagnosticsDirectory()==localDiagnostics);
            preferences.setValue("storage/dataDirectory",QString());
            VERIFY(preferences.contains("storage/dataDirectory"));
            VERIFY(sticky::stackiesDataDirectory()==sticky::stackiesDefaultDataDirectory());
            VERIFY(sticky::diagnosticsDirectory()==localDiagnostics);
            preferences.remove("storage/dataDirectory");
            VERIFY(sticky::stackiesDataDirectory()==data.path());
        }
        VERIFY(sticky::collapseDuplicateStackiesDirectory(
            QStringLiteral("/home/example/.local/share/Stackies/Stackies"))==
            QStringLiteral("/home/example/.local/share/Stackies"));
        VERIFY(sticky::collapseDuplicateStackiesDirectory(
            QStringLiteral("/home/example/.local/share/Stackies"))==
            QStringLiteral("/home/example/.local/share/Stackies"));
        VERIFY(sticky::collapseDuplicateStackiesDirectory(
            QStringLiteral("/home/example/.local/share/OtherApp/Stackies"))==
            QStringLiteral("/home/example/.local/share/OtherApp/Stackies"));
        // A manually selected non-Latin locale must use one common font
        // across the application, native menus, tooltips and note renderers.
        sticky::applyUiFont(app,"zh_CN");
        const QFont ui=app.font();
        VERIFY(!ui.family().isEmpty());
        // QFontInfo may not resolve a native typeface with QT_QPA_PLATFORM=offscreen.
        // Keep that diagnostic for an actual desktop run, not a headless CI gate.
        if(QGuiApplication::platformName()!=QStringLiteral("offscreen"))
            VERIFY(!QFontInfo(ui).family().isEmpty());
        QMenu contextMenu;
        VERIFY(contextMenu.font().family()==ui.family());
        VERIFY(QToolTip::font().family()==ui.family());
        const QFont note=sticky::noteContentFont();
        VERIFY(note.family()==ui.family());
        VERIFY(note.pointSize()==11);
        // Reading mode must scroll at a normal QTextEdit-like rate.
        // A default QAbstractScrollArea step of 1 makes wheel scrolling
        // appear almost stationary, especially on Linux.
        ReadPane reading;
        VERIFY(reading.verticalScrollBar()->singleStep()==20);
        // Offscreen Qt has no access to installed fonts on some Windows
        // setups (Qt6 reports an empty resolved family). That is not a UI
        // defect. Exercise label fallback even without a font engine, and
        // verify actual glyph coverage only when Qt can resolve Latin text.
        const bool fontEngineReady=sticky::canDisplayUiText(
            ui,QStringLiteral("English"));
        if(fontEngineReady){
            VERIFY(sticky::readableLanguageLabel(
                QStringLiteral("English"),QStringLiteral("Unused"),ui)==
                QStringLiteral("English"));
        }else{
            qInfo("Offscreen test font unavailable; verifying fallback labels only");
            VERIFY(sticky::readableLanguageLabel(
                QStringLiteral("日本語"),QStringLiteral("Japanese"),ui)==
                QStringLiteral("Japanese"));
        }
        // U+FFFF is not a Unicode character and cannot be rendered even
        // if a valid desktop font is installed.
        VERIFY(sticky::readableLanguageLabel(
            QString(QChar(0xFFFF)),QStringLiteral("Fallback"),ui)==
            QStringLiteral("Fallback"));
        VERIFY(sticky::hasUiLanguageFont(QStringLiteral("en")));
        VERIFY(!sticky::hasUiLanguageFont(QStringLiteral("not-a-locale")));
        // Native QTextList markers contain no checkbox Unicode text. Check
        // HTML persistence, checked state and a second toggle after reload.
        QTextDocument checklistDoc;
        checklistDoc.setPlainText(QStringLiteral("Todo\nDone"));
        QTextCursor all(&checklistDoc);
        all.select(QTextCursor::Document);
        QTextListFormat listFmt;
        listFmt.setIndent(1);
        listFmt.setStyle(QTextListFormat::ListDisc);
        all.createList(listFmt);
        QTextBlock unchecked=checklistDoc.begin();
        QTextBlock checked=unchecked.next();
        VERIFY(unchecked.textList()!=nullptr);
        VERIFY(checked.textList()!=nullptr);
        QTextCursor first(unchecked),second(checked);
        auto firstFmt=first.blockFormat();
        firstFmt.setMarker(QTextBlockFormat::MarkerType::Unchecked);
        first.setBlockFormat(firstFmt);
        auto secondFmt=second.blockFormat();
        secondFmt.setMarker(QTextBlockFormat::MarkerType::Checked);
        second.setBlockFormat(secondFmt);
        VERIFY(sticky::checklist::isMarker(unchecked));
        VERIFY(!sticky::checklist::isChecked(unchecked));
        VERIFY(sticky::checklist::isChecked(checked));
        VERIFY(unchecked.text()==QStringLiteral("Todo"));
        const QString checklistHtml=checklistDoc.toHtml();
        QTextDocument reloaded;
        reloaded.setHtml(checklistHtml);
        VERIFY(sticky::checklist::isMarker(reloaded.begin()));
        VERIFY(sticky::checklist::isChecked(reloaded.begin().next()));
        QTextCursor toggle(reloaded.begin());
        sticky::checklist::flipCheckbox(toggle);
        VERIFY(sticky::checklist::isChecked(reloaded.begin()));
        sticky::checklist::flipCheckbox(toggle);
        VERIFY(!sticky::checklist::isChecked(reloaded.begin()));
        VERIFY(!checklistHtml.contains(QStringLiteral("☐")));
        VERIFY(!checklistHtml.contains(QStringLiteral("☑")));

        // A marker is painted using its own ink (not selected text ink),
        // including the case where the selected paragraph is entirely red.
        reloaded.setTextWidth(260);
        QImage markerSample(80,38,QImage::Format_ARGB32_Premultiplied);
        markerSample.fill(Qt::transparent);
        QPainter markerPainter(&markerSample);
        sticky::checklist::paintMarker(
            markerPainter,reloaded.begin(),QRectF(43,5,1,18),
            QColor("#F6E6B7"),sticky::noteContentFont());
        markerPainter.end();
        bool painted=false;
        for(int y=0;y<markerSample.height();++y)
            for(int x=0;x<markerSample.width();++x)
                painted|=qAlpha(markerSample.pixel(x,y))>0;
        VERIFY(painted);

        // A list marker uses Source compositing, but the Overview note's
        // selected blue outline must stay uninterrupted alongside its gutter.
        OverviewCanvas::Card outlineCard{
            QStringLiteral("outline-regression"),QStringLiteral("Checklist"),
            checklistHtml,QStringLiteral("#F6E6B7"),330,270
        };
        OverviewCanvas borderCanvas({outlineCard},outlineCard.id);
        const QImage borderImage=borderCanvas.fullPixmapFor(outlineCard.id).toImage();
        VERIFY(borderImage.width()>=330 && borderImage.height()>=270);
        for(int y=46;y<=110;++y){
            const QColor edge=borderImage.pixelColor(1,y);
            VERIFY(edge.blue()>edge.red()+30);
        }

        // Transition paint order: active third page must cover the other
        // cards. Of the remaining pages, first is above second and fourth.
        QVector<OverviewCanvas::Card> layeredCards{
            {QStringLiteral("first"),QStringLiteral("First"),QStringLiteral("<p></p>"),
             QStringLiteral("#E5C6A6"),220,160},
            {QStringLiteral("second"),QStringLiteral("Second"),QStringLiteral("<p></p>"),
             QStringLiteral("#C5DBF0"),220,160},
            {QStringLiteral("third"),QStringLiteral("Third"),QStringLiteral("<p></p>"),
             QStringLiteral("#D5E5C1"),220,160},
            {QStringLiteral("fourth"),QStringLiteral("Fourth"),QStringLiteral("<p></p>"),
             QStringLiteral("#E4CCE7"),220,160}
        };
        OverviewCanvas stackCanvas(layeredCards,QStringLiteral("third"));
        stackCanvas.reflow(1200);
        const auto pixmapColor=[](const QPixmap &pix){
            const QImage image=pix.toImage();
            VERIFY(!image.isNull());
            return image.pixelColor(image.width()/2,image.height()*4/5);
        };
        const auto initialLayers=stackCanvas.snapshots();
        VERIFY(initialLayers.size()==4);
        VERIFY(pixmapColor(initialLayers[0].second)==QColor("#E4CCE7"));
        VERIFY(pixmapColor(initialLayers[1].second)==QColor("#C5DBF0"));
        VERIFY(pixmapColor(initialLayers[2].second)==QColor("#E5C6A6"));
        VERIFY(pixmapColor(initialLayers[3].second)==QColor("#D5E5C1"));
        // Switching to another card must refresh its highlighted preview
        // and put that card last in the outgoing/returning animation.
        stackCanvas.setActiveCard(QStringLiteral("second"));
        stackCanvas.reflow(1200);
        const auto changedLayers=stackCanvas.snapshots();
        VERIFY(changedLayers.size()==4);
        VERIFY(pixmapColor(changedLayers[0].second)==QColor("#E4CCE7"));
        VERIFY(pixmapColor(changedLayers[1].second)==QColor("#D5E5C1"));
        VERIFY(pixmapColor(changedLayers[2].second)==QColor("#E5C6A6"));
        VERIFY(pixmapColor(changedLayers[3].second)==QColor("#C5DBF0"));
        // The same paper should gain or lose its strong blue border when
        // selection changes. Compare each card to its own previous preview
        // to avoid conflating outline colors with different paper colors.
        const auto edgeRed=[](const QPixmap &pix){
            const QImage img=pix.toImage();
            return img.pixelColor(1,img.height()/2).red();
        };
        VERIFY(edgeRed(changedLayers[3].second)+30<
               edgeRed(initialLayers[1].second)); // second becomes selected
        VERIFY(edgeRed(initialLayers[3].second)+30<
               edgeRed(changedLayers[1].second)); // third becomes inactive
        // Broad diagonal light band from the Qt glass lab. The
        // brightest point follows the sloped center, not the perimeter.
        for(const QSize panel:{QSize(320,260),QSize(640,480),
                               QSize(1440,810),QSize(2000,700),QSize(650,1100)}){
            const auto layout=sticky::glass::reflectionLayout(
                panel.width(),panel.height());
            VERIFY(layout.centerTop==0.40);
            VERIFY(layout.centerDrift==0.34);
            VERIFY(layout.sigma==0.13);
            for(double y:{0.0,0.25,0.5,0.75,1.0}){
                const double center=layout.center(y);
                VERIFY(center>0.0&&center<1.0);
                const auto opacity=[&](double x){
                    return sticky::glass::reflectionOpacity(x,y,layout);
                };
                VERIFY(opacity(center)>0.16);
                VERIFY(opacity(center)>opacity(center-0.13));
                VERIFY(opacity(center)>opacity(center+0.13));
                VERIFY(opacity(center-0.13)>opacity(center-0.26));
                VERIFY(opacity(center+0.13)>opacity(center+0.26));
                VERIFY(opacity(0.0)<opacity(center));
                VERIFY(opacity(1.0)<opacity(center));
            }
            VERIFY(sticky::glass::reflectionOpacity(-0.1,0.5,layout)==0.0);
            VERIFY(sticky::glass::reflectionOpacity(1.1,0.5,layout)==0.0);
        }

        sticky::Controller controller;
        controller.initialize();
        // The UI path must actually persist a Note + Stack edit together
        // through the partial save mode, not only exercise raw Storage SQL.
        const auto firstStackId=controller.workspace().stacks.front().id;
        const auto firstNoteId=controller.stack(firstStackId).activeId;
        const auto originalWelcome=controller.note(firstNoteId).html;
        const auto editedWelcome=QString::fromStdString(originalWelcome)+
            QStringLiteral("<p>Incremental save round-trip</p>");
        controller.updateHtml(firstNoteId,editedWelcome);
        controller.setStackOpacity(firstStackId,95);
        VERIFY(controller.saveNow());
        {
            sticky::Storage reader((data.path()+"/notes.sqlite").toStdString());
            const auto saved=reader.load();
            VERIFY(saved.note(firstNoteId).html==editedWelcome.toStdString());
            VERIFY(saved.stack(firstStackId).opacity==95);
            VERIFY(saved.notes.size()==1);
        }
        // A second SQLite writer simulates a synchronized database update.
        // saveNow() must cancel the old debounce timer, otherwise the
        // immediate revision check will incorrectly consider the UI busy.
        // The controller must see its new revision without rebuilding the app.
        {
            sticky::Storage another((data.path()+"/notes.sqlite").toStdString());
            auto external=another.load();
            external.note(firstNoteId).html="<p>External computer update</p>";
            another.save(external);
            another.release();
        }
        controller.checkForExternalUpdates();
        VERIFY(controller.note(firstNoteId).html=="<p>External computer update</p>");
        // Real editor regression: the user's text has ALREADY been auto-saved
        // when an external write replaces that same note's contents.
        // Finishing the editor is a no-op save; the revision reload must
        // still preserve the local edit as a new independent note.
        {
            auto *localWindow=controller.window(firstStackId);
            VERIFY(localWindow!=nullptr);
            localWindow->beginEdit();
            auto *editor=localWindow->findChild<QTextEdit*>();
            VERIFY(editor!=nullptr);
            editor->setPlainText(QStringLiteral("Local edit saved during editing"));
            const std::string localVersion=editor->toHtml().toStdString();
            controller.updateHtml(firstNoteId,QString::fromStdString(localVersion));
            VERIFY(controller.saveNow());
            {
                sticky::Storage externalWriter((data.path()+"/notes.sqlite").toStdString());
                auto external=externalWriter.load();
                external.note(firstNoteId).html="<p>Remote edit after autosave</p>";
                externalWriter.save(external);
                externalWriter.release();
            }
            localWindow->finishEdit();
            controller.checkForExternalUpdates();
            VERIFY(controller.note(firstNoteId).html=="<p>Remote edit after autosave</p>");
            std::string copiedNoteId;
            for(const auto &n:controller.workspace().notes){
                if(n.id!=firstNoteId && n.html==localVersion)copiedNoteId=n.id;
            }
            VERIFY(!copiedNoteId.empty());
            VERIFY(controller.workspace().owner(copiedNoteId).noteIds.size()==1);
            {
                sticky::Storage persisted((data.path()+"/notes.sqlite").toStdString());
                const auto current=persisted.load();
                VERIFY(current.note(copiedNoteId).html==localVersion);
                VERIFY(current.note(firstNoteId).html=="<p>Remote edit after autosave</p>");
            }
            // Remove the regression's independent note before the existing
            // overview/layout assertions, which expect the original note count.
            controller.workspace().deleteNote(copiedNoteId,0);
            controller.workspace().permanentlyDelete(copiedNoteId);
            controller.syncWindows();
            controller.scheduleSave();
            VERIFY(controller.saveNow());
        }
        // Regression for the other conflict entrypoint: the note was
        // auto-saved, then changed externally, but a later local stack save
        // discovers the stale revision *before* the 30-second reload.
        {
            // finishEdit() defers the prior editor's deletion. Drain it so
            // findChild<QTextEdit*>() refers to the newly active editor.
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            auto *localWindow=controller.window(firstStackId);
            VERIFY(localWindow!=nullptr);
            localWindow->beginEdit();
            auto *editor=localWindow->findChild<QTextEdit*>();
            VERIFY(editor!=nullptr);
            editor->setPlainText(QStringLiteral("Autosaved before stale saveNow"));
            const std::string localVersion=editor->toHtml().toStdString();
            controller.updateHtml(firstNoteId,QString::fromStdString(localVersion));
            VERIFY(controller.saveNow());
            {
                sticky::Storage externalWriter((data.path()+"/notes.sqlite").toStdString());
                auto external=externalWriter.load();
                external.note(firstNoteId).html="<p>External version before stale save</p>";
                externalWriter.save(external);
                externalWriter.release();
            }
            localWindow->finishEdit();
            controller.setStackOpacity(firstStackId,90);
            VERIFY(controller.saveNow());  // Forces DatabaseRevisionConflict path.
            VERIFY(controller.note(firstNoteId).html==
                   "<p>External version before stale save</p>");
            std::string copiedNoteId;
            for(const auto &n:controller.workspace().notes){
                if(n.id!=firstNoteId && n.html==localVersion)copiedNoteId=n.id;
            }
            VERIFY(!copiedNoteId.empty());
            VERIFY(controller.workspace().owner(copiedNoteId).noteIds.size()==1);
            {
                sticky::Storage persisted((data.path()+"/notes.sqlite").toStdString());
                const auto current=persisted.load();
                VERIFY(current.note(firstNoteId).html==
                       "<p>External version before stale save</p>");
                VERIFY(current.note(copiedNoteId).html==localVersion);
            }
            // Keep the remaining overview tests' original note count.
            controller.workspace().deleteNote(copiedNoteId,0);
            controller.workspace().permanentlyDelete(copiedNoteId);
            controller.syncWindows();
            controller.scheduleSave();
            VERIFY(controller.saveNow());
            controller.setStackOpacity(firstStackId,95);
            VERIFY(controller.saveNow());
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        controller.updateHtml(firstNoteId,QString::fromStdString(originalWelcome));
        VERIFY(controller.saveNow());
        // Leave the rest of the visual checks using their original content.
        controller.updateHtml(firstNoteId,QString::fromStdString(originalWelcome));
        VERIFY(controller.saveNow());
        const auto *tray=controller.findChild<QSystemTrayIcon*>();
        VERIFY(tray && tray->contextMenu());
        VERIFY(tray->contextMenu()->styleSheet().contains(QStringLiteral("QMenu::item:selected")));
        QMenu *languageMenu=nullptr;
        for(QAction *action:tray->contextMenu()->actions())
            if(action->menu())languageMenu=action->menu();
        VERIFY(languageMenu && languageMenu->actions().size()==7);
        const QStringList safeNames={
            QStringLiteral("System default"),QStringLiteral("English"),
            QStringLiteral("Chinese (Simplified)"),
            QStringLiteral("Chinese (Traditional)"),
            QStringLiteral("Japanese"),QStringLiteral("Korean"),
            QStringLiteral("Spanish")
        };
        for(int i=0;i<languageMenu->actions().size();++i){
            QAction *action=languageMenu->actions().at(i);
            if(fontEngineReady){
                // Normal desktops must never show tofu in language labels.
                VERIFY(sticky::canDisplayUiText(languageMenu->font(),action->text()));
            }else{
                // With no font backend, Qt cannot render *any* glyph.
                // Verify the application selected the ASCII fallback.
                VERIFY(action->text()==safeNames[i]);
            }
        }
        // Only one localized starter note on a clean first launch.
        VERIFY(controller.workspace().notes.size()==1);
        VERIFY(controller.workspace().stacks.size()==1);
        VERIFY(controller.workspace().notes[0].title=="Welcome");
        VERIFY(controller.workspace().notes[0].html.find("Stackies")!=std::string::npos);
        controller.workspace().addNote("Second",{560,155,350,300});
        controller.scheduleSave(); // Direct model mutation: full save required.
        controller.syncWindows();
        const auto ids=controller.workspace().stacks;
        VERIFY(ids.size()==2);
        // Lower edge and clamping are important at the new 50–100% range.
        controller.setStackOpacity(ids[0].id,50);
        VERIFY(controller.stack(ids[0].id).opacity==50);
        controller.setStackOpacity(ids[0].id,47);
        VERIFY(controller.stack(ids[0].id).opacity==50);
        const auto source=ids[0].id;
        const auto other=ids[1].id;
        // Single->multi adds only the rail width; multi->single removes it
        // without changing the width or height of the yellow paper.
        const QRect singleGeometry=controller.window(other)->geometry();
        const auto extra=controller.workspace().addNote("Rail check",{80,80,330,250});
        controller.scheduleSave();
        controller.syncWindows();
        const auto retiredStack=controller.workspace().owner(extra).id;
        controller.workspace().moveNote(extra,other,0);
        controller.scheduleSave();
        controller.syncWindows();
        // Qt can deliver queued show/tab callbacks before deleteLater destroys
        // the removed stack window. They must never read the retired stack ID.
        VERIFY(controller.window(retiredStack)==nullptr);
        QCoreApplication::processEvents();
        // processEvents() alone does not necessarily drain DeferredDelete.
        // A tab-rail rebuild hides obsolete slots and deletes them later;
        // findChildren() would otherwise include those old TabButtons,
        // whose stylesheet still reflects their former selected status.
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        VERIFY(controller.workspace().owner(extra).id==other);
        bool foundActiveTab=false,foundInactiveTab=false;
        int currentTabCount=0;
        for(TabButton *tab:controller.window(other)->findChildren<TabButton*>()){
            ++currentTabCount;
            if(tab->noteId()==controller.stack(other).activeId){
                foundActiveTab=true;
                VERIFY(!tab->styleSheet().contains(QStringLiteral("QPushButton:hover")));
            }else{
                foundInactiveTab=true;
                VERIFY(tab->styleSheet().contains(QStringLiteral("QPushButton:hover")));
            }
        }
        VERIFY(currentTabCount==2);
        VERIFY(foundActiveTab && foundInactiveTab);
        VERIFY(controller.window(other)->geometry().width()==singleGeometry.width()+70);
        VERIFY(controller.window(other)->geometry().height()==singleGeometry.height());
        controller.workspace().detach(extra,{80,80,330,250});
        // This test mutates Workspace directly, bypassing Controller's
        // change tracking; explicitly promote to an authoritative snapshot.
        controller.scheduleSave();
        controller.syncWindows();
        VERIFY(controller.window(other)->geometry().width()==singleGeometry.width());
        VERIFY(controller.window(other)->geometry().height()==singleGeometry.height());
        bool succeeded=false;
#ifdef Q_OS_LINUX
        bool suppressedFinalPaint=false;
#endif
        QTimer::singleShot(0,&app,[&]{
            try{
                // Closing a single stack really DOES persist a hidden flag.
                controller.workspace().stack(source).visible=true;
                controller.workspace().stack(other).visible=true;
                controller.syncWindows();
                VERIFY(controller.window(source)->isVisible());
                // Ordinary minimization keeps the Stack in the model and
                // survives a routine sync rather than reopening automatically.
                controller.minimizeStack(source);
                VERIFY(controller.window(source)->isMinimized());
                VERIFY(controller.stack(source).visible);
                controller.syncWindows();
                VERIFY(controller.window(source)->isMinimized());
                controller.window(source)->showNormal();
                VERIFY(!controller.window(source)->isMinimized());
                controller.window(source)->close();
                VERIFY(!controller.stack(source).visible);
                VERIFY(controller.stack(other).visible);
                // A temporary Overview presentation must not touch the model.
                controller.workspace().stack(source).visible=true;
                controller.workspace().stack(other).visible=false;
                controller.scheduleSave();
                controller.setStackOpacity(source,50);
                controller.syncWindows();
                controller.showOverview(source);
                VERIFY(controller.stack(source).visible);
                // Before the first animated frame, keep the real source
                // visible; the old immediate minimize produced a blank gap.
                VERIFY(!controller.window(source)->isMinimized());
                VERIFY(!controller.window(source)->isHidden());
                QTimer::singleShot(110,&app,[&]{
                    try{
                        // After the first transition frame, source presentation
                        // changes. Linux must avoid iconifying it: an X11
                        // minimize/restore race can strand detached stacks.
#ifdef Q_OS_LINUX
                        VERIFY(controller.window(source)->isHidden());
                        VERIFY(!controller.window(source)->isMinimized());
#else
                        VERIFY(controller.window(source)->isMinimized());
#endif
                        controller.syncWindows();
#ifdef Q_OS_LINUX
                        VERIFY(controller.window(source)->isHidden());
                        VERIFY(!controller.window(source)->isMinimized());
#else
                        VERIFY(controller.window(source)->isMinimized());
#endif
                        controller.setOverviewStackPresentation(source,false);
                        VERIFY(controller.window(source)->isVisible());
                        VERIFY(!controller.window(source)->isMinimized());
                        controller.setOverviewStackPresentation(source,true);
#ifdef Q_OS_LINUX
                        VERIFY(controller.window(source)->isHidden());
                        VERIFY(!controller.window(source)->isMinimized());
#else
                        VERIFY(controller.window(source)->isMinimized());
                        VERIFY(!controller.window(source)->isHidden());
#endif
                        VERIFY(controller.stack(source).visible);
                        VERIFY(!controller.stack(other).visible);
                        // A taskbar restore request for the source ends Overview;
                        // other stacks must not affect it.
                        VERIFY(!controller.requestOverviewTaskbarRestore(other));
                        VERIFY(controller.requestOverviewTaskbarRestore(source));
#ifdef Q_OS_LINUX
                        VERIFY(controller.window(source)->isHidden());
#else
                        VERIFY(controller.window(source)->isMinimized());
#endif
#ifdef Q_OS_LINUX
                        // Force the Linux fallback path: a compositor may
                        // suppress the last frame of the closing animation.
                        // A hidden transition cannot deliver afterNextPaint().
                        QTimer::singleShot(1,&app,[&]{
                            for(QWidget *top:QApplication::topLevelWidgets()){
                                if(auto *overlay=qobject_cast<OverviewOverlay*>(top)){
                                    if(auto *transition=overlay->findChild<QWidget*>(
                                           QStringLiteral("overviewTransitionLayer"))){
                                        transition->hide();
                                        suppressedFinalPaint=true;
                                    }
                                }
                            }
                        });
#endif
                    }catch(const std::exception &e){
                        qCritical("UI overview handoff failure: %s",e.what());
                        app.exit(3);
                    }
                });
                QTimer::singleShot(560,&app,[&]{
                    try{
                        VERIFY(controller.window(source)->isVisible());
                        VERIFY(!controller.window(source)->isMinimized());
                        VERIFY(controller.stack(source).visible);
                        VERIFY(!controller.stack(other).visible);
#ifdef Q_OS_LINUX
                        VERIFY(suppressedFinalPaint);
                        // Exercise a structural tear-off from a MULTI-note
                        // source, rather than merely restoring a single note.
                        // The source stack must remain at its old location.
                        const auto retainedNote=controller.stack(source).activeId;
                        const auto detachedNote=controller.workspace().addNote(
                            "Overview detach regression",{660,160,330,260});
                        const auto temporaryStack=controller.workspace().owner(detachedNote).id;
                        controller.workspace().moveNote(detachedNote,source,0);
                        controller.scheduleSave();
                        controller.syncWindows();
                        VERIFY(controller.window(temporaryStack)==nullptr);
                        VERIFY(controller.stack(source).noteIds.size()==2);
                        const QRect before=controller.window(source)->geometry();
                        controller.showOverview(source);
                        QTimer::singleShot(110,&app,[&,retainedNote,detachedNote,before]{
                            try{
                                VERIFY(controller.window(source)->isHidden());
                                OverviewOverlay *overlay=nullptr;
                                for(QWidget *top:QApplication::topLevelWidgets())
                                    if(auto *candidate=qobject_cast<OverviewOverlay*>(top))
                                        overlay=candidate;
                                VERIFY(overlay!=nullptr);
                                controller.setOverviewStackPresentation(source,false);
                                overlay->hide();
                                controller.detachFromOverview(source,detachedNote,
                                                              {720,240,330,260});
                                overlay->deleteLater();
                                QTimer::singleShot(80,&app,[&,retainedNote,detachedNote,before]{
                                    try{
                                        VERIFY(controller.stack(source).noteIds.size()==1);
                                        VERIFY(controller.stack(source).activeId==retainedNote);
                                        VERIFY(controller.window(source)->isVisible());
                                        VERIFY(!controller.window(source)->isMinimized());
                                        // The original paper stays in place;
                                        // removing its tab rail may shrink the
                                        // outer window by 70 px, as intended.
                                        VERIFY(controller.window(source)->geometry().topLeft()==before.topLeft());
                                        VERIFY(controller.window(source)->geometry().height()==before.height());
                                        VERIFY(controller.window(source)->geometry().width()<=before.width());
                                        const auto detachedStack=
                                            controller.workspace().owner(detachedNote).id;
                                        VERIFY(detachedStack!=source);
                                        VERIFY(controller.window(detachedStack)!=nullptr);
                                        VERIFY(controller.window(detachedStack)->isVisible());
                                        succeeded=true;
                                        controller.quitSafely();
                                    }catch(const std::exception &e){
                                        qCritical("UI Linux overview detach failure: %s",e.what());
                                        app.exit(3);
                                    }
                                });
                            }catch(const std::exception &e){
                                qCritical("UI Linux overview detach setup failure: %s",e.what());
                                app.exit(3);
                            }
                        });
#else
                        succeeded=true;
                        controller.quitSafely();
#endif
                    }catch(const std::exception &e){
                        qCritical("UI smoke completion failure: %s",e.what());
                        app.exit(3);
                    }
                });
            }catch(const std::exception &e){
                qCritical("UI smoke failure: %s",e.what());
                app.exit(3);
            }
        });
        const int result=app.exec();
        VERIFY(result==0 && succeeded);
        // Quitting must not save temporary hidden presentation or hide another Stack.
        sticky::Storage stored((data.path()+"/notes.sqlite").toStdString());
        auto restored=stored.load();
        VERIFY(restored.stack(source).visible);
        VERIFY(!restored.stack(other).visible);
        VERIFY(restored.stack(source).opacity==50);
        // Saving a stale local edit should produce a separate visible note
        // beside the externally changed original, and must allow clean exit.
        const std::string remoteText="<p>Changed by another device</p>";
        const std::string localText="<p>Edited on this device</p>";
        restored.note(firstNoteId).html=remoteText;
        stored.save(restored);
        stored.release();
        controller.updateHtml(firstNoteId,QString::fromStdString(localText));
        VERIFY(controller.saveNow());
        auto conflictResult=stored.load();
        VERIFY(conflictResult.note(firstNoteId).html==remoteText);
        bool hasLocalCopy=false;
        for(const auto &n:conflictResult.notes){
            if(n.id==firstNoteId || n.html!=localText)continue;
            VERIFY(conflictResult.owner(n.id).noteIds.size()==1);
            VERIFY(conflictResult.owner(n.id).visible);
            hasLocalCopy=true;
        }
        VERIFY(hasLocalCopy);
        std::cout<<"PASS: selected Overview card contour, minimize persists across sync, taskbar restore closes Overview, quit preserves visibility\n";
        return 0;
    }catch(const std::exception &e){
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
