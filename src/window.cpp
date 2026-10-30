#include "window.h"
#include "noteeditor.h"
#include "controller.h"
#include "colorpalette.h"
#include "i18n.h"
#include "checklistpaint.h"
#include "scrollbarstyle.h"
#include "perf.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QEnterEvent>
#include <QDebug>
#include <QEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QWidgetAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QTimer>
#include <QCursor>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QCache>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <cstdlib>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextList>
#include <QTextListFormat>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace sticky;
namespace {
QString qstr(const std::string &s){return QString::fromStdString(s);}
void drawTabler(QPainter &p,const QString &name,const QRectF &bounds,const QColor &ink){
    // Parsing SVG XML on every mouse-hover repaint is unnecessary.
    // Cache the renderer (rather than bitmaps) to retain vector sharpness
    // across different DPI scales and widget sizes.
    static QCache<QString,QSvgRenderer> renderers(48);
    const QString key=name+QLatin1Char('|')+ink.name();
    QSvgRenderer *svg=renderers.object(key);
    if(!svg){
        QFile file(QString(":/tabler/%1.svg").arg(name));
        if(!file.open(QIODevice::ReadOnly))return;
        QByteArray data=file.readAll();
        data.replace("currentColor",ink.name().toUtf8());
        auto *fresh=new QSvgRenderer(data);
        if(!fresh->isValid()){delete fresh;return;}
        renderers.insert(key,fresh);
        svg=fresh;
    }
    svg->render(&p,bounds);
}
QPushButton *smallButton(const QString &label,const QString &tip,QWidget *parent){
    auto *b=new QPushButton(label,parent);
    b->setToolTip(tip);b->setFixedSize(28,27);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet("QPushButton{background:transparent;border:none;border-radius:6px;color:#444;font-size:14px;}QPushButton:hover{background:#00000015;}");
    return b;
}
enum class FormatGlyph {Bold,Italic,Underline,Strike,Color,Bullets,Numbers,Checklist};
class FormatButton : public QPushButton {
    FormatGlyph glyph_;
    QColor ink_="#272625";
public:
    FormatButton(FormatGlyph glyph,const QString &hint,QWidget *parent)
        :QPushButton(parent),glyph_(glyph){
        setToolTip(hint);setFixedSize(24,27);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet("QPushButton{background:transparent;border:none;border-radius:5px;}"
                      "QPushButton:hover{background:rgba(45,65,75,25);}");
    }
    void setInk(QColor c){ink_=c;update();}
protected:
    void paintEvent(QPaintEvent *event) override {
        QPushButton::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QString name;
        switch(glyph_){
        case FormatGlyph::Bold:name="bold";break;
        case FormatGlyph::Italic:name="italic";break;
        case FormatGlyph::Underline:name="underline";break;
        case FormatGlyph::Strike:name="strikethrough";break;
        case FormatGlyph::Color:name="text-color";break;
        case FormatGlyph::Bullets:name="list";break;
        case FormatGlyph::Numbers:name="list-numbers";break;
        case FormatGlyph::Checklist:name="list-check";break;
        }
        drawTabler(p,name,QRectF(2,3,20,20),QColor("#41464B"));
        if(glyph_==FormatGlyph::Color)
            p.fillRect(QRectF(5,24,14,2.5),ink_);
    }
};
// Consistent vector-stroke toolbar glyphs: no font/Unicode fallback.
enum class ToolbarGlyph { Plus, Grid, Pin, Pencil, Minimize, Close, Check };
class IconButton : public QPushButton {
    ToolbarGlyph glyph_;
    bool pinned_=false;
public:
    IconButton(ToolbarGlyph glyph,const QString &tip,QWidget *parent)
        :QPushButton(parent),glyph_(glyph){
        setToolTip(tip);
        setFixedSize(29,29);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet(
            "QPushButton{background:transparent;border:none;border-radius:7px;}"
            "QPushButton:hover{background:rgba(35,48,62,19);}"
            "QPushButton:pressed{background:rgba(35,48,62,35);}");
    }
    void setGlyph(ToolbarGlyph glyph){glyph_=glyph;update();}
    void setPinned(bool pinned){pinned_=pinned;update();}
protected:
    void paintEvent(QPaintEvent *e) override {
        QPushButton::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QString name;
        switch(glyph_){
        case ToolbarGlyph::Plus:name="plus";break;
        case ToolbarGlyph::Grid:name="layout-grid";break;
        case ToolbarGlyph::Pin:name=pinned_?"pinned":"pin";break;
        case ToolbarGlyph::Pencil:name="pencil";break;
        case ToolbarGlyph::Minimize:name="minus";break;
        case ToolbarGlyph::Close:name="x";break;
        case ToolbarGlyph::Check:name="check";break;
        }
        drawTabler(p,name,QRectF(4.5,4.5,20,20),
                   isEnabled()?QColor("#44515B"):QColor("#9AA3A8"));
    }
};
class ScrollCue : public QPushButton {
    bool up_;
public:
    ScrollCue(bool up,QWidget *parent):QPushButton(parent),up_(up){
        setFixedSize(28,17);setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet("QPushButton{background:transparent;border:none;border-radius:6px;}"
                      "QPushButton:hover{background:rgba(60,75,89,21);}");
    }
protected:
    void paintEvent(QPaintEvent *e) override {
        QPushButton::paintEvent(e);
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(73,85,96,170),1.5);
        pen.setCapStyle(Qt::RoundCap);pen.setJoinStyle(Qt::RoundJoin);p.setPen(pen);
        const qreal apex=up_?5:12,base=up_?11:6;
        QPainterPath chevron;
        chevron.moveTo(8,base);chevron.lineTo(14,apex);chevron.lineTo(20,base);
        p.drawPath(chevron);
    }
};
// This narrow overlay only hides the paper/tab seam. The tab button itself
// owns the one and only outer contour (including its right-hand edge).
class TabSeamCover : public QWidget {
    QColor fill_="#F6E6B7";
public:
    explicit TabSeamCover(QWidget *parent=nullptr):QWidget(parent){
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    void setFill(const QColor &color){fill_=color;update();}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        // Replace the underlying translucent paper border rather than
        // blending a second semi-transparent patch over it.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(0,1,width(),height()-2),fill_);
    }
};
}
DragHeader::DragHeader(QWidget *parent):QWidget(parent){setCursor(Qt::SizeAllCursor);setFixedHeight(36);}
void DragHeader::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton){startGlobal_=e->globalPosition().toPoint();startTopLeft_=window()->pos();dragging_=false;e->accept();return;}
    QWidget::mousePressEvent(e);
}
void DragHeader::mouseMoveEvent(QMouseEvent* e){
    if(!(e->buttons()&Qt::LeftButton))return;
    auto g=e->globalPosition().toPoint();
    if(!dragging_&&(g-startGlobal_).manhattanLength()>=7)dragging_=true;
    if(dragging_){window()->move(startTopLeft_+(g-startGlobal_));emit moving(g);}
    e->accept();
}
void DragHeader::mouseReleaseEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton){emit finished(e->globalPosition().toPoint(),dragging_);dragging_=false;e->accept();return;}
    QWidget::mouseReleaseEvent(e);
}
void DragHeader::mouseDoubleClickEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton&&titleTarget_&&titleTarget_->geometry().contains(e->position().toPoint())){
        dragging_=false;
        emit titleDoubleClicked();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}
TabButton::TabButton(const std::string &id,QWidget *parent):QPushButton(parent),noteId_(id){
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(38);
    setFixedWidth(54);
    reachAnimation_.setDuration(140);
    reachAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&reachAnimation_,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
        reach_=value.toInt();
        // The fixed-size parent slot prevents the other tabs from shifting.
        setFixedWidth(54+reach_);
    });
}
void TabButton::setIndexText(const QString &name){
    const QFontMetrics metrics(font());
    const QString label=metrics.elidedText(name,Qt::ElideRight,46);
    if(text()!=label)setText(label);
    if(!toolTip().isEmpty())setToolTip(QString());
}
void TabButton::setActive(bool active){
    if(active_==active)return; // Don't cancel an unrelated hover animation.
    active_=active;
    reachAnimation_.stop();
    reach_=active_?6:0;
    setFixedWidth(54+reach_);
}
void TabButton::animateReach(int target){
    if(reach_==target)return;
    reachAnimation_.stop();
    reachAnimation_.setStartValue(reach_);
    reachAnimation_.setEndValue(target);
    reachAnimation_.start();
}
void TabButton::enterEvent(QEnterEvent *e){
    if(!moved_)animateReach(active_?6:3);
    QPushButton::enterEvent(e);
}
void TabButton::leaveEvent(QEvent *e){
    if(!moved_)animateReach(active_?6:0);
    QPushButton::leaveEvent(e);
}
void TabButton::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton){pressGlobal_=e->globalPosition().toPoint();moved_=false;}
    QPushButton::mousePressEvent(e);
}
void TabButton::mouseMoveEvent(QMouseEvent *e){
    if(e->buttons()&Qt::LeftButton){
        auto p=e->globalPosition().toPoint();
        if((p-pressGlobal_).manhattanLength()>8){
            if(!moved_)animateReach(active_?6:0);
            moved_=true;
        }
        if(moved_)emit moving(qstr(noteId_),p);
    }
    QPushButton::mouseMoveEvent(e);
}
void TabButton::mouseReleaseEvent(QMouseEvent *e){
    const auto p=e->globalPosition().toPoint();
    QPushButton::mouseReleaseEvent(e);
    if(e->button()==Qt::LeftButton)emit releasedAt(qstr(noteId_),p,moved_);
}

StickyWindow::StickyWindow(Controller *c,const std::string &stackId):controller_(c),stackId_(stackId){
    setWindowFlags(Qt::Window|Qt::FramelessWindowHint);
    // Ensure each standalone Stack advertises the application icon, even
    // when the Linux window manager treats frameless windows independently.
    setWindowIcon(qApp->windowIcon());
    setAttribute(Qt::WA_TranslucentBackground);
    // Coalesce rich-text edits. Final HTML is still flushed synchronously
    // when editing ends, including tab changes and application exit.
    editorVisualTimer_.setSingleShot(true);
    editorVisualTimer_.setInterval(0);
    connect(&editorVisualTimer_,&QTimer::timeout,this,[this]{
        if(editor_)static_cast<NoteEditor*>(editor_)->refreshChecklistVisuals();
    });
    editorSyncTimer_.setSingleShot(true);
    editorSyncTimer_.setInterval(180);
    connect(&editorSyncTimer_,&QTimer::timeout,this,[this]{
        if(editor_)controller_->updateHtml(showingId_.toStdString(),editor_->toHtml());
    });
    if(controller_->stack(stackId_).topmost)setWindowFlag(Qt::WindowStaysOnTopHint,true);
    setWindowTitle("Stackies");
    setMinimumSize(220,160);
    auto *root=new QHBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
    paper_=new QFrame(this);paper_->setObjectName("paper");
    auto *paperLayout=new QVBoxLayout(paper_);paperLayout->setContentsMargins(1,1,1,1);paperLayout->setSpacing(0);
    header_=new DragHeader(paper_);
    auto *h=new QHBoxLayout(header_);h->setContentsMargins(11,3,6,3);h->setSpacing(2);
    title_=new QLabel(header_);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont=sticky::noteContentFont();
    titleFont.setPixelSize(12);
    titleFont.setWeight(QFont::DemiBold);
    title_->setFont(titleFont);
    title_->setStyleSheet("color:#38312A;background:transparent;");
    h->addWidget(title_,1);
    titleEdit_=new QLineEdit(header_);
    titleEdit_->setStyleSheet("QLineEdit{background:rgba(255,255,255,180);color:#28333A;border:1px solid #9F9889;border-radius:5px;padding:2px 5px;font-size:12px;}");
    h->addWidget(titleEdit_,1);
    titleEdit_->hide();
    header_->setTitleTarget(title_);
    connect(header_,&DragHeader::titleDoubleClicked,this,&StickyWindow::beginTitleEdit);
    connect(titleEdit_,&QLineEdit::editingFinished,this,[this]{finishTitleEdit();});
    titleEdit_->installEventFilter(this);
    auto *add=new IconButton(ToolbarGlyph::Plus,trUi("Add note to this stack"),header_);
    auto *overview=new IconButton(ToolbarGlyph::Grid,trUi("Overview"),header_);
    pinButton_=new IconButton(ToolbarGlyph::Pin,trUi("Toggle always on top"),header_);
    editButton_=new IconButton(ToolbarGlyph::Pencil,trUi("Edit / finish editing"),header_);
    auto *minimizeBtn=new IconButton(ToolbarGlyph::Minimize,trUi("Minimize this note / stack"),header_);
    auto *closeBtn=new IconButton(ToolbarGlyph::Close,trUi("Hide this note / stack"),header_);
    h->addWidget(add);h->addWidget(overview);h->addWidget(pinButton_);
    h->addWidget(editButton_);h->addWidget(minimizeBtn);h->addWidget(closeBtn);
    paperLayout->addWidget(header_);
    pages_=new QStackedWidget(paper_);reader_=new ReadPane(pages_);
    connect(reader_,&ReadPane::editRequested,this,&StickyWindow::beginEdit);
    connect(reader_,&ReadPane::checklistToggled,this,[this](int position){
        const auto &s=controller_->stack(stackId_);
        QTextDocument doc;doc.setHtml(qstr(controller_->note(s.activeId).html));
        const QTextBlock block=doc.findBlock(position);
        if(!block.isValid()||!NoteEditor::isChecklist(block))return;
        QTextCursor cursor(&doc);
        cursor.setPosition(block.position());
        sticky::checklist::flipCheckbox(cursor);
        const QString html=doc.toHtml();
        controller_->updateHtml(s.activeId,html);
        reader_->setHtml(html);
    });
    pages_->addWidget(reader_);
    paperLayout->addWidget(pages_,1);
    toolBar_=new QWidget(paper_);auto *tools=new QHBoxLayout(toolBar_);
    tools->setContentsMargins(4,2,4,4);tools->setSpacing(2);
    auto *bold=new FormatButton(FormatGlyph::Bold,trUi("Bold (Ctrl+B)"),toolBar_);
    auto *italic=new FormatButton(FormatGlyph::Italic,trUi("Italic (Ctrl+I)"),toolBar_);
    auto *underline=new FormatButton(FormatGlyph::Underline,trUi("Underline (Ctrl+U)"),toolBar_);
    auto *strike=new FormatButton(FormatGlyph::Strike,trUi("Strikethrough (Ctrl+Shift+X)"),toolBar_);
    auto *color=new FormatButton(FormatGlyph::Color,trUi("Text color"),toolBar_);
    auto *bullet=new FormatButton(FormatGlyph::Bullets,trUi("Bullet list"),toolBar_);
    auto *numbered=new FormatButton(FormatGlyph::Numbers,trUi("Numbered list"),toolBar_);
    auto *checklist=new FormatButton(FormatGlyph::Checklist,trUi("Checklist"),toolBar_);
    for(auto *button:{bold,italic,underline,strike,color,bullet,numbered,checklist})
        tools->addWidget(button);
    tools->addStretch();
    toolBar_->hide();paperLayout->addWidget(toolBar_);
    auto *bottom=new QWidget(paper_);bottom->setFixedHeight(14);
    auto *bottomLayout=new QHBoxLayout(bottom);bottomLayout->setContentsMargins(0,0,2,2);
    bottomLayout->addStretch();bottomLayout->addWidget(new QSizeGrip(bottom));
    paperLayout->addWidget(bottom);
    root->addWidget(paper_,1);
    tabScroll_=new QScrollArea(this);
    tabScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll_->setWidgetResizable(true);
    tabScroll_->setFixedWidth(70);
    tabScroll_->setFrameShape(QFrame::NoFrame);
    tabScroll_->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    tabRoot_=new QWidget();
    tabRoot_->setStyleSheet("background:transparent;");
    tabScroll_->setWidget(tabRoot_);
    tabScroll_->viewport()->installEventFilter(this);
    tabRoot_->installEventFilter(this);
    root->addWidget(tabScroll_);
    seam_=new TabSeamCover(this);
    seam_->hide();
    scrollUp_=new ScrollCue(true,this);
    scrollDown_=new ScrollCue(false,this);
    scrollUp_->hide();scrollDown_->hide();
    connect(scrollUp_,&QPushButton::clicked,this,[this]{scrollTabs(-1);});
    connect(scrollDown_,&QPushButton::clicked,this,[this]{scrollTabs(+1);});
    header_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header_,&QWidget::customContextMenuRequested,this,[this](const QPoint&p){
        showQuickColorMenu(header_->mapToGlobal(p));
    });
    connect(add,&QPushButton::clicked,this,[this]{controller_->createNote(stackId_);});
    connect(overview,&QPushButton::clicked,this,[this]{controller_->showOverview(stackId_);});
    connect(pinButton_,&QPushButton::clicked,this,[this]{controller_->toggleTopmost(stackId_);});
    connect(editButton_,&QPushButton::clicked,this,[this]{editing_?finishEdit():beginEdit();});
    connect(minimizeBtn,&QPushButton::clicked,this,[this]{controller_->minimizeStack(stackId_);});
    connect(closeBtn,&QPushButton::clicked,this,[this]{this->close();});
    connect(header_,&DragHeader::moving,this,[this](const QPoint&p){controller_->windowDragged(stackId_,p);});
    connect(header_,&DragHeader::finished,this,[this](const QPoint&p,bool dragged){controller_->finishWindowDrag(stackId_,p,dragged);});
    connect(bold,&QPushButton::clicked,this,[this]{if(editor_)editor_->setFontWeight(editor_->fontWeight()==QFont::Bold?QFont::Normal:QFont::Bold);});
    connect(italic,&QPushButton::clicked,this,[this]{if(editor_)editor_->setFontItalic(!editor_->fontItalic());});
    connect(underline,&QPushButton::clicked,this,[this]{if(editor_)editor_->setFontUnderline(!editor_->fontUnderline());});
    connect(strike,&QPushButton::clicked,this,[this]{
        if(editor_){static_cast<NoteEditor*>(editor_)->toggleStrike();editor_->setFocus();}
    });
    connect(bullet,&QPushButton::clicked,this,[this]{
        if(editor_){static_cast<NoteEditor*>(editor_)->toggleList(NoteEditor::ListKind::Bullet);editor_->setFocus();}
    });
    connect(numbered,&QPushButton::clicked,this,[this]{
        if(editor_){static_cast<NoteEditor*>(editor_)->toggleList(NoteEditor::ListKind::Numbered);editor_->setFocus();}
    });
    connect(checklist,&QPushButton::clicked,this,[this]{
        if(editor_){static_cast<NoteEditor*>(editor_)->toggleList(NoteEditor::ListKind::Checklist);editor_->setFocus();}
    });
    connect(color,&QPushButton::clicked,this,[this,color]{
        if(!editor_)return;
        showColorPalette(this,true,editor_->textColor(),
            [this,color](const QColor &c){
                if(!editor_)return;
                editor_->setTextColor(c);
                color->setInk(c);
                editor_->setFocus();
            },
            [this,color]{
                if(!editor_)return;
                // Unset the foreground only. Preserve mixed bold, italic and
                // strikethrough formatting within the selection.
                QTextCursor selection=editor_->textCursor();
                if(selection.hasSelection()){
                    QTextCursor edit=selection;
                    edit.beginEditBlock();
                    for(int pos=selection.selectionStart();pos<selection.selectionEnd();++pos){
                        edit.setPosition(pos);
                        edit.setPosition(pos+1,QTextCursor::KeepAnchor);
                        auto fmt=edit.charFormat();
                        fmt.clearProperty(QTextFormat::ForegroundBrush);
                        edit.setCharFormat(fmt);
                    }
                    edit.endEditBlock();
                    editor_->setTextCursor(selection);
                }else{
                    auto fmt=selection.charFormat();
                    fmt.clearProperty(QTextFormat::ForegroundBrush);
                    editor_->setCurrentCharFormat(fmt);
                }
                color->setInk(QColor("#272625"));
                editor_->setFocus();
            });
    });
    setGeometry(controller_->stack(stackId_).rect.x,controller_->stack(stackId_).rect.y,controller_->stack(stackId_).rect.w,controller_->stack(stackId_).rect.h);
    settingUp_=false;
    refresh();
}
void StickyWindow::setPaletteColor(const QColor &c){
    QColor next=c.isValid()?c:QColor("#F6E6B7");
    const int alpha=controller_->stack(stackId_).opacity*255/100;
    next.setAlpha(alpha);
    if(next!=pageColor_){
        pageColor_=next;
        const QString fill=QString("rgba(%1,%2,%3,%4)")
            .arg(pageColor_.red()).arg(pageColor_.green()).arg(pageColor_.blue()).arg(alpha);
        paper_->setStyleSheet(QString("QFrame#paper{background:%1;border:1px solid #83765E;border-radius:9px;}").arg(fill));
    }
    reader_->setPaperColor(pageColor_);
    // A second translucent fill over the same area would multiply opacity.
    if(editor_){
        static_cast<NoteEditor*>(editor_)->setPaperColor(pageColor_);
        editor_->setStyleSheet("QTextEdit{border:none;background:transparent;color:#272625;padding:8px;font-size:12pt;}");
    }
}
void StickyWindow::refresh(){
    perf::Scope timing("StickyWindow::refresh");
    const auto &s=controller_->stack(stackId_);
    const auto &n=controller_->note(s.activeId);
    if(!titleEditing_)title_->setText(qstr(n.title));
    setWindowTitle(qstr(n.title)+" — Stackies");
    static_cast<IconButton*>(pinButton_)->setPinned(s.topmost);
    if(showingId_!=qstr(n.id)){
        finishEdit();
        showingId_=qstr(n.id);
        reader_->setHtml(qstr(n.html),true); // New note resets text selection.
    }else if(!editing_)reader_->setHtml(qstr(n.html));
    setPaletteColor(QColor(qstr(n.color)));
    rebuildTabs();
}
namespace {
constexpr int kTabHeight=38;
constexpr int kTabStep=44;
constexpr int kTabTop=48;      // Paper header and up-arrow safety zone.
constexpr int kTabBottom=29;   // Down-arrow / lower paper edge safety zone.
}
int StickyWindow::visibleTabCapacity() const {
    const int area=tabScroll_?tabScroll_->viewport()->height():0;
    return std::max(1,1+(area-kTabTop-kTabBottom-kTabHeight)/kTabStep);
}
void StickyWindow::scrollTabs(int amount){
    if(movingTabId_.size()||tabOrder_.size()<=visibleTabCount_)return;
    const int last=std::max(0,static_cast<int>(tabOrder_.size())-visibleTabCount_);
    const int next=std::clamp(firstVisibleTab_+amount,0,last);
    if(next==firstVisibleTab_)return;
    firstVisibleTab_=next;
    positionTabs();
}
void StickyWindow::positionTabs(bool ensureActive){
    if(forcedClose_||adjustingTabs_||!tabRoot_)return;
    adjustingTabs_=true;
    const int count=tabSlots_.size();
    visibleTabCount_=visibleTabCapacity();
    const int maxFirst=std::max(0,count-visibleTabCount_);
    firstVisibleTab_=std::clamp(firstVisibleTab_,0,maxFirst);
    if(ensureActive&&count){
        const auto &s=controller_->stack(stackId_);
        const int active=tabOrder_.indexOf(qstr(s.activeId));
        if(active>=0){
            if(active<firstVisibleTab_)firstVisibleTab_=active;
            else if(active>=firstVisibleTab_+visibleTabCount_)
                firstVisibleTab_=active-visibleTabCount_+1;
            firstVisibleTab_=std::clamp(firstVisibleTab_,0,maxFirst);
        }
    }
    for(int i=0;i<count;++i){
        QWidget *slot=tabSlots_[i];
        const bool visible=i>=firstVisibleTab_&&i<firstVisibleTab_+visibleTabCount_;
        if(visible){
            slot->move(0,kTabTop+(i-firstVisibleTab_)*kTabStep);
            slot->show();
        }else slot->hide();
    }
    if(scrollUp_){
        const int x=tabScroll_->geometry().x()+(tabScroll_->width()-28)/2;
        scrollUp_->move(x,tabScroll_->geometry().y()+4);
        scrollDown_->move(x,tabScroll_->geometry().bottom()-20);
        scrollUp_->setVisible(tabScroll_->isVisible()&&firstVisibleTab_>0);
        scrollDown_->setVisible(tabScroll_->isVisible()&&
            firstVisibleTab_+visibleTabCount_<count);
        if(scrollUp_->isVisible())scrollUp_->raise();
        if(scrollDown_->isVisible())scrollDown_->raise();
    }
    adjustingTabs_=false;
    updateSeam();
}
void StickyWindow::rebuildTabs(){
    const auto &s=controller_->stack(stackId_);
    QStringList ids;
    for(const auto &id:s.noteIds)ids.append(qstr(id));
    const bool structureChanged=ids!=tabOrder_;
    const bool wantRail=ids.size()>1;
    if(tabRailInitialized_ && wantRail!=tabRailShown_){
        // Preserve the visible paper, not the whole outer window, as the
        // 70px rail first appears or disappears.
        QRect next=geometry();
        next.setWidth(std::max(minimumWidth(),next.width()+(wantRail?70:-70)));
        if(QScreen *display=QGuiApplication::screenAt(geometry().center())){
            const QRect bounds=display->availableGeometry();
            if(next.width()<=bounds.width()){
                next.moveLeft(std::clamp(next.left(),bounds.left(),
                                         bounds.right()-next.width()+1));
            }
        }
        setGeometry(next); // move/resize events also update persisted geometry
    }
    tabRailShown_=wantRail;
    tabRailInitialized_=true;
    tabScroll_->setVisible(wantRail);
    if(structureChanged){
        qDebug("Rebuilding tabs: %d -> %d",int(tabSlots_.size()),int(ids.size()));
        clearTabReorder();
        // A tab can emit releasedAt from inside its own mouse release event.
        // Retire old widgets after event delivery rather than deleting the
        // event sender (or its parent slot) on the same call stack.
        for(QWidget *slot:tabSlots_){
            slot->hide();
            slot->deleteLater();
        }
        tabSlots_.clear();
        tabOrder_=ids;
        for(const auto &id:s.noteIds){
            const auto &n=controller_->note(id);
            auto *slot=new QWidget(tabRoot_);
            slot->setFixedSize(62,kTabHeight);
            slot->installEventFilter(this);
            auto *button=new TabButton(n.id,slot);
            button->move(0,0);
            button->installEventFilter(this);
            connect(button,&TabButton::releasedAt,this,[this](QString name,QPoint pos,bool dragged){
                onTabReleased(name,pos,dragged);
            });
            connect(button,&TabButton::moving,this,[this](QString name,QPoint pos){
                controller_->tabDragged(stackId_,name.toStdString(),pos);
            });
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button,&QWidget::customContextMenuRequested,this,[this,id]{
                QMenu menu;
                auto *rename=menu.addAction(trUi("Rename tab…"));
                auto *follow=menu.addAction(trUi("Follow note title"));
                follow->setCheckable(true);
                follow->setChecked(controller_->note(id).followTitle);
                menu.addSeparator();
                controller_->addColorActions(&menu,id);
                menu.addSeparator();
                auto *deleteAction=menu.addAction(trUi("Delete note…"));
                const auto *act=menu.exec(QCursor::pos());
                if(act==rename)controller_->renameTab(id);
                else if(act==follow)controller_->setFollowTitle(id,follow->isChecked());
                else if(act==deleteAction)controller_->deleteNote(id);
            });
            tabSlots_.append(slot);
        }
    }
    for(int i=0;i<tabSlots_.size();++i){
        auto *button=static_cast<TabButton*>(tabSlots_[i]->findChild<TabButton*>());
        const auto &n=controller_->note(s.noteIds[i]);
        const bool active=s.activeId==n.id;
        button->setActive(active);
        QFont font=button->font();
        font.setPixelSize(11);font.setBold(active);
        if(button->font()!=font)button->setFont(font);
        button->setIndexText(qstr(n.tab));
        QColor color(qstr(n.color));
        if(!color.isValid())color=QColor("#F6E6B7");
        const int alpha=s.opacity*255/100;
        auto asCss=[alpha](const QColor &c){
            return QString("rgba(%1,%2,%3,%4)")
                .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
        };
        // Selected tabs stay visually unchanged on hover: no background,
        // border-color or outline shift. Inactive tabs retain their cue.
        const QString base=QString(
            "QPushButton{background:%1;border:%2;"
            "border-top-left-radius:0px;border-bottom-left-radius:0px;"
            "border-top-right-radius:10px;border-bottom-right-radius:10px;"
            "color:#293038;padding:0px 2px;text-align:center;}")
            .arg(asCss(color),active?"1px solid #83765E":"1px solid #9A9D98");
        const QString hover=active?QString():QString(
            "QPushButton:hover{background:%1;border-color:#697C88;}")
            .arg(asCss(color.lighter(106)));
        const QString style=base+hover;
        if(button->styleSheet()!=style)button->setStyleSheet(style);
    }
    if(structureChanged||!tabLayoutReady_){
        tabLayoutReady_=true;
        // Creation can activate a new tab outside the visible range.
        // Otherwise preserve the reader's current tab window.
        const int active=tabOrder_.indexOf(qstr(s.activeId));
        const bool outside=active<firstVisibleTab_||
                            active>=firstVisibleTab_+visibleTabCount_;
        positionTabs(!isVisible()||(structureChanged&&outside));
    }else{
        // Content selection never rebuilds or scrolls the index rail.
        updateSeam();
    }
}
void StickyWindow::onTabReleased(QString id,QPoint global,bool moved){
    // Do not rebuild/destroy the tab hierarchy from within TabButton's
    // mouseReleaseEvent. The context object also cancels the queued action
    // safely if this stack window has closed in the meantime.
    QTimer::singleShot(0,this,[this,id=std::move(id),global,moved]{
        // The source stack may have been removed before this queued release runs.
        if(forcedClose_)return;
        controller_->tabReleased(stackId_,id.toStdString(),global,moved);
    });
}
int StickyWindow::tabInsertionAt(const QPoint &global) const {
    const int count=tabSlots_.size();
    if(count==0)return 0;
    const int first=firstVisibleTab_;
    const int end=std::min(count,first+visibleTabCount_);
    for(int i=first;i<end;++i){
        const int mid=kTabTop+(i-firstVisibleTab_)*kTabStep+kTabHeight/2;
        if(global.y()<tabRoot_->mapToGlobal(QPoint(0,mid)).y())return i;
    }
    return end;
}
void StickyWindow::previewTabReorder(const std::string& noteId,const QPoint &global){
    const auto &ids=controller_->stack(stackId_).noteIds;
    auto it=std::find(ids.begin(),ids.end(),noteId);
    if(it==ids.end())return;
    const int from=static_cast<int>(it-ids.begin());
    if(from>=tabSlots_.size()||!tabSlots_[from]->isVisible())return;
    if(movingTabId_!=noteId){
        clearTabReorder();
        movingTabId_=noteId;
        for(auto *slot:tabSlots_)tabBasePositions_.append(slot->pos());
        // Move the captured physical button itself. No translucent clone,
        // duplicate caption, or insertion stripe.
        auto *button=tabSlots_[from]->findChild<TabButton*>();
        draggedTabStyle_=button->styleSheet();
        QColor paper(qstr(controller_->note(noteId).color));
        if(!paper.isValid())paper=QColor("#F6E6B7");
        button->setStyleSheet(QString(
            "QPushButton{background:%1;border:1px solid #738693;"
            "border-top-left-radius:3px;border-bottom-left-radius:3px;"
            "border-top-right-radius:10px;border-bottom-right-radius:10px;"
            "color:#293038;padding:0px 2px;text-align:center;}").arg(paper.name()));
        tabSlots_[from]->raise();
        seam_->hide();
    }
    const QPoint pos=tabRoot_->mapFromGlobal(global);
    tabSlots_[from]->move(0,pos.y()-kTabHeight/2);
    tabSlots_[from]->raise();
    const int insert=tabInsertionAt(global);
    if(insert==insertionIndex_)return;
    insertionIndex_=insert;
    const int dest=std::clamp(insert>from?insert-1:insert,0,static_cast<int>(tabSlots_.size())-1);
    for(int i=0;i<tabSlots_.size();++i){
        if(i==from||!tabSlots_[i]->isVisible())continue;
        int to=i;
        if(from<dest&&i>from&&i<=dest)to=i-1;
        else if(from>dest&&i>=dest&&i<from)to=i+1;
        const int targetY=kTabTop+(to-firstVisibleTab_)*kTabStep;
        QWidget *slot=tabSlots_[i];
        for(auto *animation:slot->findChildren<QPropertyAnimation*>(QString(),Qt::FindDirectChildrenOnly)){
            animation->stop();animation->deleteLater();
        }
        if(slot->pos().y()==targetY)continue;
        auto *animation=new QPropertyAnimation(slot,"pos",slot);
        animation->setDuration(125);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(slot->pos());
        animation->setEndValue(QPoint(0,targetY));
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
void StickyWindow::clearTabReorder(){
    if(movingTabId_.empty())return;
    const auto &ids=controller_->stack(stackId_).noteIds;
    const auto it=std::find(ids.begin(),ids.end(),movingTabId_);
    if(it!=ids.end()){
        const int idx=static_cast<int>(it-ids.begin());
        if(idx<tabSlots_.size()){
            auto *button=tabSlots_[idx]->findChild<TabButton*>();
            button->setStyleSheet(draggedTabStyle_);
        }
    }
    for(auto *slot:tabSlots_){
        for(auto *animation:slot->findChildren<QPropertyAnimation*>(QString(),Qt::FindDirectChildrenOnly)){
            animation->stop();animation->deleteLater();
        }
    }
    movingTabId_.clear();insertionIndex_=-1;tabBasePositions_.clear();
    draggedTabStyle_.clear();
    positionTabs();
}
QRect StickyWindow::tabBarGlobalRect() const {
    return {tabScroll_->mapToGlobal(QPoint(0,0)),tabScroll_->size()};
}
void StickyWindow::updateTabOverflow(){
    positionTabs();
}
void StickyWindow::updateSeam(){
    if(!seam_||!tabScroll_->isVisible()||!movingTabId_.empty()||tabSlots_.empty()){
        if(seam_)seam_->hide();return;
    }
    const auto &s=controller_->stack(stackId_);
    const int index=tabOrder_.indexOf(qstr(s.activeId));
    if(index<firstVisibleTab_||index>=firstVisibleTab_+visibleTabCount_){
        seam_->hide();return;
    }
    const QPoint top=tabSlots_[index]->mapTo(this,QPoint(0,0));
    auto *outline=static_cast<TabSeamCover*>(seam_);
    outline->setFill(pageColor_);
    outline->setGeometry(paper_->geometry().right()-2,top.y(),7,kTabHeight);
    outline->show();outline->raise();
}
void StickyWindow::beginTitleEdit(){
    if(titleEditing_)return;
    const auto &n=controller_->note(controller_->stack(stackId_).activeId);
    titleEditing_=true;
    titleEdit_->setText(qstr(n.title));
    title_->hide();titleEdit_->show();
    titleEdit_->selectAll();titleEdit_->setFocus();
}
void StickyWindow::finishTitleEdit(bool cancel){
    if(!titleEditing_)return;
    titleEditing_=false;
    const QString text=titleEdit_->text();
    titleEdit_->hide();title_->show();
    if(!cancel)controller_->setNoteTitle(controller_->stack(stackId_).activeId,text);
    else title_->setText(qstr(controller_->note(controller_->stack(stackId_).activeId).title));
}
void StickyWindow::showQuickColorMenu(const QPoint &global){
    QMenu menu(this);
    const auto id=controller_->stack(stackId_).activeId;
    controller_->addColorActions(&menu,id);
    menu.addSeparator();
    auto *remove=menu.addAction(trUi("Delete note…"));
    if(menu.exec(global)==remove)controller_->deleteNote(id);
}
void StickyWindow::beginEdit(){
    if(editing_)return;
    const auto &s=controller_->stack(stackId_);
    const auto &n=controller_->note(s.activeId);
    showingId_=qstr(n.id);
    editor_=new NoteEditor(pages_);editor_->setAcceptRichText(true);editor_->setFrameShape(QFrame::NoFrame);
    editor_->viewport()->setAutoFillBackground(false);
    editor_->setFont(sticky::noteContentFont());editor_->setHtml(qstr(n.html));
    static_cast<NoteEditor*>(editor_)->refreshChecklistVisuals();
    editor_->installEventFilter(this);
    pages_->addWidget(editor_);pages_->setCurrentWidget(editor_);
    setPaletteColor(pageColor_);
    toolBar_->show();editing_=true;static_cast<IconButton*>(editButton_)->setGlyph(ToolbarGlyph::Check);editButton_->setToolTip(trUi("Finish editing (Ctrl+Enter)"));
    connect(editor_,&QTextEdit::textChanged,this,[this]{
        if(!editor_)return;
        controller_->markInteractiveEdit(showingId_.toStdString());
        editorVisualTimer_.start();
        editorSyncTimer_.start();
    });
    editor_->setFocus();
}
void StickyWindow::finishEdit(){
    if(!editing_)return;
    if(editor_){
        editorVisualTimer_.stop();
        editorSyncTimer_.stop();
        const QString finalHtml=editor_->toHtml();
        controller_->updateHtml(showingId_.toStdString(),finalHtml);
        reader_->setHtml(finalHtml,true); // Exiting edit resets reader selection.
        pages_->setCurrentWidget(reader_);
        pages_->removeWidget(editor_);
        editor_->deleteLater();editor_=nullptr;
    }
    editing_=false;toolBar_->hide();static_cast<IconButton*>(editButton_)->setGlyph(ToolbarGlyph::Pencil);editButton_->setToolTip(trUi("Edit this note"));
}
bool StickyWindow::eventFilter(QObject *o,QEvent *e){
    if(tabScroll_&&e->type()==QEvent::Wheel&&
        (o==tabScroll_->viewport()||o==tabRoot_||
         std::any_of(tabSlots_.cbegin(),tabSlots_.cend(),[o](QWidget *slot){
             return o==slot||o==slot->findChild<TabButton*>();
         }))){
        auto *wheel=static_cast<QWheelEvent*>(e);
        const int dy=wheel->angleDelta().y();
        const int py=wheel->pixelDelta().y();
        if(dy||py){
            scrollTabs(dy? (dy>0?-1:1)*std::max(1,std::abs(dy)/120)
                        : (py>0?-1:1));
            return true;
        }
    }
    if(o==titleEdit_&&e->type()==QEvent::KeyPress){
        auto *key=static_cast<QKeyEvent*>(e);
        if(key->key()==Qt::Key_Escape){finishTitleEdit(true);return true;}
    }
    if(o==editor_&&e->type()==QEvent::KeyPress){
        auto *key=static_cast<QKeyEvent*>(e);
        if(key->key()==Qt::Key_X &&
           (key->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier))==
           (Qt::ControlModifier|Qt::ShiftModifier)){
            static_cast<NoteEditor*>(editor_)->toggleStrike();return true;
        }
        if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&(key->modifiers()&Qt::ControlModifier)){finishEdit();return true;}
    }
    return QWidget::eventFilter(o,e);
}
void StickyWindow::changeEvent(QEvent *e){
    QWidget::changeEvent(e);
#ifdef Q_OS_WIN
    // Fallback for taskbar restore paths that bypass WM_SYSCOMMAND.
    // Keep the source minimized until its Overview closing animation ends.
    if(e->type()==QEvent::WindowStateChange && !isMinimized() &&
       controller_->requestOverviewTaskbarRestore(stackId_))
        showMinimized();
#endif
}
#ifdef Q_OS_WIN
bool StickyWindow::nativeEvent(const QByteArray &eventType,void *message,qintptr *result){
    const auto *msg=static_cast<const MSG*>(message);
    if(msg && msg->message==WM_SYSCOMMAND &&
       ((msg->wParam & 0xFFF0)==SC_RESTORE ||
        (msg->wParam & 0xFFF0)==SC_MAXIMIZE) &&
       controller_->requestOverviewTaskbarRestore(stackId_)){
        // Suppress restoring the original HWND while Overview is open.
        if(result)*result=0;
        return true;
    }
    return QWidget::nativeEvent(eventType,message,result);
}
#endif
void StickyWindow::closeEvent(QCloseEvent *e){
    if(forcedClose_||controller_->isQuitting()){e->accept();return;}
    e->ignore();controller_->hideStack(stackId_);
}
void StickyWindow::forceClose(){forcedClose_=true;close();}
void StickyWindow::moveEvent(QMoveEvent *e){QWidget::moveEvent(e);if(!settingUp_&&!forcedClose_)controller_->saveGeometry(stackId_,geometry());}
void StickyWindow::resizeEvent(QResizeEvent *e){
    QWidget::resizeEvent(e);
    if(!settingUp_&&!forcedClose_){
        updateTabOverflow();
        controller_->saveGeometry(stackId_,geometry());
    }
}

void StickyWindow::showEvent(QShowEvent *e){
    QWidget::showEvent(e);
    QTimer::singleShot(0,this,[this]{
        // deleteLater() is deferred; queued show callbacks can outlive their stack.
        if(!forcedClose_)positionTabs(true);
    });
}
