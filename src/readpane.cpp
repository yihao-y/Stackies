#include "window.h"
#include "checklistpaint.h"
#include "i18n.h"
#include "scrollbarstyle.h"
#include "perf.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <algorithm>

ReadPane::ReadPane(QWidget *parent):QAbstractScrollArea(parent),paper_("#F6E6B7"){
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("QAbstractScrollArea{background:transparent;border:none;}");
    verticalScrollBar()->setStyleSheet(sticky::verticalScrollBarStyle());
    // QAbstractScrollArea defaults to 1 pixel/step; QTextEdit uses 20.
    // Match editing mode so mouse-wheel scrolling is not painfully slow.
    verticalScrollBar()->setSingleStep(20);
    doc_.setDocumentMargin(0);
    doc_.setIndentWidth(24);
    doc_.setDefaultFont(sticky::noteContentFont());
    reflow();
}
void ReadPane::setHtml(const QString &html,bool forceReset){
    if(!forceReset && lastHtml_==html)return; // Qt reparses and relayouts on setHtml().
    lastHtml_=html;
    sticky::perf::Scope timing("ReadPane::setHtml");
    doc_.setHtml(html);anchor_=cursor_=-1;reflow();viewport()->update();
}
void ReadPane::setPaperColor(const QColor &c){
    if(paper_==c)return;
    paper_=c;viewport()->update();
}
void ReadPane::reflow(){
    if(!viewport())return;
    doc_.setTextWidth(std::max(70,viewport()->width()-24));
    const int h=static_cast<int>(doc_.size().height())+20;
    verticalScrollBar()->setRange(0,std::max(0,h-viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}
void ReadPane::resizeEvent(QResizeEvent *e){QAbstractScrollArea::resizeEvent(e);reflow();}
int ReadPane::hitAt(const QPoint &p) const {
    const QPointF d(p.x()-12,p.y()-11+verticalScrollBar()->value());
    return doc_.documentLayout()->hitTest(d,Qt::FuzzyHit);
}
void ReadPane::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton){setFocus();anchor_=cursor_=hitAt(e->position().toPoint());viewport()->update();}
    QAbstractScrollArea::mousePressEvent(e);
}
void ReadPane::mouseMoveEvent(QMouseEvent *e){
    if((e->buttons()&Qt::LeftButton)&&anchor_>=0){cursor_=hitAt(e->position().toPoint());viewport()->update();}
    QAbstractScrollArea::mouseMoveEvent(e);
}
void ReadPane::mouseReleaseEvent(QMouseEvent* e){
    if(e->button()==Qt::LeftButton&&anchor_==cursor_){
        const int pos=hitAt(e->position().toPoint());
        const QPointF point(e->position().x()-12,
                            e->position().y()-11+verticalScrollBar()->value());
        const QTextBlock block=doc_.findBlock(pos);
        if(sticky::checklist::isMarker(block)){
            const QRectF first=sticky::checklist::documentTextStart(doc_,block);
            const QRectF hit(first.left()-32,first.top()-3,
                             31,first.height()+6);
            if(first.isValid()&&hit.contains(point)){
                emit checklistToggled(block.position());
                e->accept();return;
            }
        }
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}
void ReadPane::mouseDoubleClickEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton){emit editRequested();e->accept();return;}
    QAbstractScrollArea::mouseDoubleClickEvent(e);
}
void ReadPane::copySelection(){
    if(anchor_<0||cursor_<=-1||anchor_==cursor_)return;
    QTextCursor c(&doc_);c.setPosition(anchor_);c.setPosition(cursor_,QTextCursor::KeepAnchor);
    QString text=c.selectedText();text.replace(QChar::ParagraphSeparator,'\n');
    QApplication::clipboard()->setText(text);
}
void ReadPane::keyPressEvent(QKeyEvent *e){
    if(e->matches(QKeySequence::Copy)){copySelection();e->accept();return;}
    QAbstractScrollArea::keyPressEvent(e);
}
void ReadPane::paintEvent(QPaintEvent *){
    QPainter painter(viewport());
    // Paper is painted once by the containing Stack. Keep text/selection opaque
    // instead of compositing the translucent paper a second time.
    painter.save();
    painter.translate(12,11-verticalScrollBar()->value());
    QAbstractTextDocumentLayout::PaintContext context;
    // Completion styling is display-only. Manual strikethrough remains
    // untouched in the stored HTML even after checking/unchecking a task.
    for(QTextBlock b=doc_.begin();b.isValid();b=b.next())
        sticky::checklist::addCompletionSelection(context,doc_,b);
    if(anchor_>=0&&cursor_>=0&&anchor_!=cursor_){
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor=QTextCursor(&doc_);
        selection.cursor.setPosition(anchor_);selection.cursor.setPosition(cursor_,QTextCursor::KeepAnchor);
        selection.format.setBackground(QColor("#7EB4E8"));
        context.selections.append(selection);
    }
    doc_.documentLayout()->draw(&painter,context);
    for(QTextBlock b=doc_.begin();b.isValid();b=b.next()){
        if(b.textList())
            sticky::checklist::paintMarker(
                painter,b,sticky::checklist::documentTextStart(doc_,b),
                paper_,doc_.defaultFont());
    }
    painter.restore();
}

