#pragma once
#include <algorithm>
#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextList>
#include <QTextListFormat>

// Checklist markers are native Qt list-item metadata, NOT editable Unicode
// characters. All three list markers are visually separate from text selection.
namespace sticky::checklist {
using Marker=QTextBlockFormat::MarkerType;
inline bool isMarker(const QTextBlock &block) {
    return block.textList() && block.blockFormat().marker()!=Marker::NoMarker;
}
inline bool isChecked(const QTextBlock &block) {
    return isMarker(block) && block.blockFormat().marker()==Marker::Checked;
}
inline void flipCheckbox(QTextCursor cursor) {
    if(!isMarker(cursor.block()))return;
    QTextBlockFormat format=cursor.blockFormat();
    format.setMarker(format.marker()==Marker::Checked?
                     Marker::Unchecked:Marker::Checked);
    cursor.setBlockFormat(format);
}
inline void addCompletionSelection(QAbstractTextDocumentLayout::PaintContext &context,
                                   QTextDocument &doc,const QTextBlock &block) {
    if(!isChecked(block)||block.text().isEmpty())return;
    QAbstractTextDocumentLayout::Selection selection;
    selection.cursor=QTextCursor(&doc);
    selection.cursor.setPosition(block.position());
    selection.cursor.setPosition(block.position()+block.text().size(),
                                 QTextCursor::KeepAnchor);
    selection.format.setFontStrikeOut(true);
    context.selections.append(selection);
}

// First-character position in DOCUMENT space; callers using QTextEdit may
// instead pass QTextEdit::cursorRect(QTextCursor(block)) in viewport space.
inline QRectF documentTextStart(const QTextDocument &doc,const QTextBlock &block) {
    const QRectF bounds=doc.documentLayout()->blockBoundingRect(block);
    const QTextLayout *layout=block.layout();
    if(!layout||layout->lineCount()==0)return {};
    const QTextLine line=layout->lineAt(0);
    if(!line.isValid())return {};
    return QRectF(bounds.left()+line.cursorToX(0),bounds.top()+line.y(),
                  1.0,line.height());
}

// Qt draws a list marker using the text's foreground and may include the
// marker in a selected block's blue highlight. Repaint ONLY its gutter on
// top of that native rendering. The result is a fixed-colour, unselected
// marker for bullets, numbers AND checkboxes, without replacing QTextList.
inline void paintMarker(QPainter &p,const QTextBlock &block,
                        const QRectF &textStart,const QColor &paper,
                        const QFont &contentFont) {
    if(!block.textList()||!textStart.isValid())return;
    const auto marker=block.blockFormat().marker();
    const auto style=block.textList()->format().style();
    QFont font=contentFont;
    const QFontMetricsF fm(font);
    const qreal areaWidth=std::max<qreal>(36.0,fm.horizontalAdvance(
        block.textList()->itemText(block))+13.0);
    const QRectF gutter(textStart.left()-areaWidth,textStart.top()-1.0,
                        areaWidth-1.0,textStart.height()+2.0);
    p.save();
    p.setClipRect(gutter);
    // Replace any selection fill or native marker with the single paper
    // layer underneath. Source avoids doubling alpha on translucent notes.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(gutter,paper);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QColor ink(67,84,98);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(ink);
    p.setBrush(Qt::NoBrush);
    const qreal cy=textStart.top()+textStart.height()/2;
    const qreal anchor=textStart.left()-fm.horizontalAdvance(QStringLiteral(" "))-4.0;
    if(marker!=Marker::NoMarker) {
        const qreal size=12.0;
        const QRectF box(anchor-size,cy-size/2.0,size,size);
        p.setPen(QPen(ink,1.5));
        p.setBrush(marker==Marker::Checked?QBrush(ink):QBrush(Qt::NoBrush));
        p.drawRoundedRect(box,2.0,2.0);
        if(marker==Marker::Checked) {
            p.setPen(QPen(Qt::white,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            QPainterPath tick;
            tick.moveTo(box.left()+2.5,box.center().y());
            tick.lineTo(box.left()+5.0,box.bottom()-3.0);
            tick.lineTo(box.right()-2.2,box.top()+2.8);
            p.drawPath(tick);
        }
    }else if(style==QTextListFormat::ListDisc||
             style==QTextListFormat::ListCircle||
             style==QTextListFormat::ListSquare) {
        const qreal size=std::max<qreal>(4.0,fm.lineSpacing()/3.0);
        const QRectF dot(anchor-size,cy-size/2.0,size,size);
        if(style==QTextListFormat::ListDisc) {
            p.setPen(Qt::NoPen);p.setBrush(ink);p.drawEllipse(dot);
        }else if(style==QTextListFormat::ListCircle) {
            p.setPen(QPen(ink,1.2));p.drawEllipse(dot);
        }else{
            p.setPen(Qt::NoPen);p.setBrush(ink);p.drawRect(dot);
        }
    }else {
        const QString label=block.textList()->itemText(block);
        const qreal labelWidth=fm.horizontalAdvance(label);
        p.setFont(font);
        p.setPen(ink);
        p.drawText(QPointF(anchor-labelWidth,
                           cy+(fm.ascent()-fm.descent())/2.0),label);
    }
    p.restore();
}
} // namespace sticky::checklist
