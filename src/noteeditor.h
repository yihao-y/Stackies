#pragma once
// Dedicated rich-text editing widget. Kept behavior-identical to the
// historical implementation during the window module split.
#include "checklistpaint.h"
#include "scrollbarstyle.h"
#include <QColor>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextEdit>
#include <QTextList>
#include <QTextListFormat>
#include <QVector>
#include <algorithm>

class NoteEditor : public QTextEdit {
public:
    enum class ListKind {Bullet,Numbered,Checklist};
    explicit NoteEditor(QWidget *parent):QTextEdit(parent){
        document()->setIndentWidth(24);
        verticalScrollBar()->setStyleSheet(sticky::verticalScrollBarStyle());
    }
    static bool isChecklist(const QTextBlock &block){
        return sticky::checklist::isMarker(block);
    }
    static bool checked(const QTextBlock &block){
        return sticky::checklist::isChecked(block);
    }
    static bool isBulletStyle(QTextListFormat::Style s){
        return s==QTextListFormat::ListDisc||
               s==QTextListFormat::ListCircle||
               s==QTextListFormat::ListSquare;
    }
    static QTextListFormat::Style bulletStyle(int indent){
        switch((qMax(1,indent)-1)%3){
        case 1:return QTextListFormat::ListCircle;
        case 2:return QTextListFormat::ListSquare;
        default:return QTextListFormat::ListDisc;
        }
    }
    void setPaperColor(const QColor &paper) {paper_=paper;}
    void refreshChecklistVisuals(){
        // Only completed items need overlays. Unchecked markers are native
        // block metadata and never require transparent Unicode selections.
        QList<QTextEdit::ExtraSelection> selections;
        for(QTextBlock b=document()->begin();b.isValid();b=b.next()){
            if(!checked(b)||b.text().isEmpty())continue;
            QTextEdit::ExtraSelection item;
            item.cursor=QTextCursor(b);
            item.cursor.setPosition(b.position());
            item.cursor.setPosition(b.position()+b.text().size(),
                                    QTextCursor::KeepAnchor);
            item.format.setFontStrikeOut(true);
            selections.append(item);
        }
        setExtraSelections(selections);
    }
    QVector<QTextBlock> selectedBlocks() const {
        const QTextCursor c=textCursor();
        QTextBlock first=document()->findBlock(c.selectionStart());
        QTextBlock last=document()->findBlock(c.selectionEnd());
        if(c.hasSelection()&&last.isValid()&&last!=first&&
           c.selectionEnd()==last.position())last=last.previous();
        QVector<QTextBlock> result;
        for(QTextBlock b=first;b.isValid();b=b.next()){
            result.append(b);
            if(b==last)break;
        }
        return result;
    }
    void indentBlocks(int delta){
        const auto blocks=selectedBlocks();
        if(blocks.isEmpty())return;
        QTextCursor group=textCursor();group.beginEditBlock();
        // Group consecutive same-level list items into ONE nested list, so
        // numbered selections keep continuous numbering after Tab.
        if(blocks.size()>1 && blocks.first().textList()){
            const auto original=blocks.first().textList()->format();
            const bool uniform=std::all_of(blocks.begin(),blocks.end(),
                [&original](const QTextBlock &b){
                    return b.textList()&&b.textList()->format().style()==original.style()&&
                        b.textList()->format().indent()==original.indent();
                });
            if(uniform && !(delta<0 && original.indent()<=1)){
                QTextCursor whole(document());
                whole.setPosition(blocks.first().position());
                whole.setPosition(blocks.last().position()+blocks.last().length()-1,
                                  QTextCursor::KeepAnchor);
                auto updated=original;
                updated.setIndent(qBound(1,original.indent()+delta,8));
                if(isBulletStyle(original.style()))
                    updated.setStyle(bulletStyle(updated.indent()));
                whole.createList(updated);
                group.endEditBlock();
                return;
            }
        }
        for(const auto &b:blocks){
            if(QTextList *list=b.textList()){
                const QTextListFormat fmtOld=list->format();
                QTextCursor cur(b);
                if(delta<0&&fmtOld.indent()<=1){
                    list->remove(b);
                }else{
                    QTextListFormat fmt=fmtOld;
                    fmt.setIndent(qBound(1,fmt.indent()+delta,8));
                    if(isBulletStyle(fmtOld.style()))
                        fmt.setStyle(bulletStyle(fmt.indent()));
                    cur.createList(fmt);
                }
            }else{
                QTextCursor cur(b);auto fmt=cur.blockFormat();
                fmt.setIndent(qBound(0,fmt.indent()+delta,8));
                cur.setBlockFormat(fmt);
            }
        }
        group.endEditBlock();
    }
    void toggleList(ListKind kind){
        QTextCursor cursor=textCursor();
        const auto blocks=selectedBlocks();
        if(blocks.isEmpty())return;
        const auto isCheck=[](const QTextBlock &b){
            return sticky::checklist::isMarker(b);
        };
        const bool removeCheck=kind==ListKind::Checklist&&
            std::all_of(blocks.cbegin(),blocks.cend(),isCheck);
        const QTextListFormat::Style style=kind==ListKind::Numbered?
            QTextListFormat::ListDecimal:QTextListFormat::ListDisc;
        QVector<QTextBlockFormat::MarkerType> priorMarkers;
        priorMarkers.reserve(blocks.size());
        for(const QTextBlock &b:blocks)
            priorMarkers.append(b.blockFormat().marker());
        cursor.beginEditBlock();
        if(removeCheck){
            // Clearing the list metadata also clears the marker, without
            // removing actual note text or activating a Unicode fallback.
            for(const QTextBlock &b:blocks){
                if(QTextList *list=b.textList())list->remove(b);
                QTextCursor c(b);
                QTextBlockFormat format=c.blockFormat();
                format.setMarker(QTextBlockFormat::MarkerType::NoMarker);
                format.setIndent(0);
                c.setBlockFormat(format);
            }
        }else if(kind!=ListKind::Checklist && !cursor.hasSelection() &&
                 cursor.block().textList() &&
                 cursor.block().blockFormat().marker()==
                     QTextBlockFormat::MarkerType::NoMarker &&
                 (kind==ListKind::Bullet?
                     isBulletStyle(cursor.block().textList()->format().style()):
                     cursor.block().textList()->format().style()==style)){
            cursor.block().textList()->remove(cursor.block());
        }else{
            // All list types remain REAL QTextLists. Qt natively renders
            // per-item checkboxes and preserves them in rich-text HTML.
            QTextListFormat listFmt;
            listFmt.setStyle(style);
            listFmt.setIndent(blocks.first().textList()?
                blocks.first().textList()->format().indent():1);
            if(kind==ListKind::Bullet)
                listFmt.setStyle(bulletStyle(listFmt.indent()));
            cursor.createList(listFmt);
            for(int i=0;i<blocks.size();++i){
                QTextCursor c(blocks[i]);
                QTextBlockFormat format=c.blockFormat();
                format.setMarker(kind==ListKind::Checklist?
                    (priorMarkers[i]==QTextBlockFormat::MarkerType::NoMarker?
                       QTextBlockFormat::MarkerType::Unchecked:priorMarkers[i]):
                    QTextBlockFormat::MarkerType::NoMarker);
                c.setBlockFormat(format);
            }
        }
        cursor.endEditBlock();
    }
    void toggleStrike(){
        QTextCharFormat fmt;fmt.setFontStrikeOut(!textCursor().charFormat().fontStrikeOut());
        mergeCurrentCharFormat(fmt);
    }
protected:
    void paintEvent(QPaintEvent *event) override {
        QTextEdit::paintEvent(event);
        QPainter painter(viewport());
        // Text selection never colors or highlights the list gutter.
        for(QTextBlock b=document()->begin();b.isValid();b=b.next()){
            if(!b.textList())continue;
            const QRect caret=cursorRect(QTextCursor(b));
            if(!viewport()->rect().intersects(
                 caret.adjusted(-48,-2,0,2)))continue;
            sticky::checklist::paintMarker(painter,b,QRectF(caret),
                                           paper_,document()->defaultFont());
        }
    }
    void keyPressEvent(QKeyEvent *e) override{
        if(e->key()==Qt::Key_Tab||e->key()==Qt::Key_Backtab){
            const int delta=e->key()==Qt::Key_Backtab||
                (e->modifiers()&Qt::ShiftModifier)?-1:1;
            indentBlocks(delta);e->accept();return;
        }
        const QTextCursor cursor=textCursor();
        if(e->text()==" " && !(e->modifiers()&
           (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){
            const QString prefix=cursor.block().text().left(cursor.positionInBlock());
            if(prefix=="-"||prefix=="*"||prefix=="1."){
                QTextCursor c=cursor;c.beginEditBlock();
                for(int i=0;i<prefix.size();++i)c.deletePreviousChar();
                QTextListFormat format;
                format.setStyle(prefix=="1."?QTextListFormat::ListDecimal:
                                              QTextListFormat::ListDisc);
                format.setIndent(1);c.createList(format);
                c.endEditBlock();e->accept();return;
            }
        }
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter){
            const QTextBlock b=cursor.block();
            if(b.text().trimmed().isEmpty()&&b.textList()){
                b.textList()->remove(b);e->accept();return;
            }
            // QTextEdit propagates native checklist metadata on Enter,
            // resetting checked items to unchecked for the new row.
        }
        QTextEdit::keyPressEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override {
        pressedMarkerPosition_=-1;
        if(e->button()==Qt::LeftButton){
            const QTextBlock b=markerAt(e->position().toPoint());
            if(b.isValid()){
                pressedMarkerPosition_=b.position();
                setFocus();
                e->accept();return;
            }
        }
        QTextEdit::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(e->button()==Qt::LeftButton && pressedMarkerPosition_>=0){
            const int initial=pressedMarkerPosition_;
            pressedMarkerPosition_=-1;
            const QTextBlock b=markerAt(e->position().toPoint());
            if(b.isValid()&&b.position()==initial){
                sticky::checklist::flipCheckbox(QTextCursor(b));
                refreshChecklistVisuals();
            }
            e->accept();return;
        }
        QTextEdit::mouseReleaseEvent(e);
    }
private:
    QTextBlock markerAt(const QPoint &point) const {
        const QTextBlock b=cursorForPosition(point).block();
        if(!isChecklist(b))return {};
        const QRect first=cursorRect(QTextCursor(b));
        const QRectF hit(first.x()-32,first.y()-3,31,first.height()+6);
        return hit.contains(point)?b:QTextBlock();
    }
    int pressedMarkerPosition_=-1;
    QColor paper_="#F6E6B7";

};
