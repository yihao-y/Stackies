#include "overview.h"
#include "controller.h"
#include "window.h"
#include "i18n.h"
#include "checklistpaint.h"
#include "glassreflection.h"
#include "scrollbarstyle.h"
#include "perf.h"
#include <QApplication>
#include <QCursor>
#include <QColor>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPixmap>
#include <QImage>
#include <QCache>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDebug>
#include <utility>
#include <functional>
#include <QTextDocument>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QLineF>
#include <limits>
#include <algorithm>
#include <cmath>
#include <vector>


namespace {
// The desktop remains unchanged; only this panel is translucent.
// Soft edge lighting and a drop shadow convey a raised pane of clear glass.
class GlassPanel : public QFrame {
    QPixmap cache_;
public:
    explicit GlassPanel(QWidget *parent=nullptr):QFrame(parent){
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        cache_=QPixmap();
        QFrame::resizeEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        const qreal dpr=devicePixelRatioF();
        const QSize physical(qMax(1,qCeil(width()*dpr)),qMax(1,qCeil(height()*dpr)));
        if(cache_.isNull()||cache_.size()!=physical||cache_.devicePixelRatio()!=dpr)
            rebuild(physical,dpr);
        QPainter painter(this);
        painter.drawPixmap(0,0,cache_);
    }
private:
    void rebuild(const QSize &physical,qreal dpr){
        // The material is independent of the note contents: reuse even when
        // Overview was closed and reconstructed.
        static QCache<QString,QPixmap> sharedGlass(64*1024);
        const QString key=QString("glass-gradient-edgefast-v1-%1x%2@%3")
            .arg(physical.width()).arg(physical.height()).arg(dpr,0,'f',3);
        if(auto *cached=sharedGlass.object(key)){cache_=*cached;return;}
        QElapsedTimer glassTime;
        glassTime.start();
        QPixmap buffer(physical);
        buffer.setDevicePixelRatio(dpr);
        buffer.fill(Qt::transparent);
        QPainter p(&buffer);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r=QRectF(rect()).adjusted(2.5,2.5,-2.5,-2.5);
        const qreal w=r.width(),h=r.height();
        // Clear-glass material: no solid tint and no desktop snapshot.
        // The desktop stays visible through the exact center of the pane.
        // A broad diagonal surface reflection, bevel, inner shade and outline
        // are cached separately from the outside projected shadow.
        constexpr qreal kBevelDepth=1.00;
        constexpr qreal kBevelWidth=11.0;
        const auto alpha=[](qreal fraction){
            return qBound(0,qRound(fraction*255.0),255);
        };

        p.save();
        QPainterPath inside;
        inside.addRoundedRect(r.adjusted(1.4,1.4,-1.4,-1.4),17.6,17.6);
        p.setClipPath(inside);

        // A 1D Qt gradient reproduces the broad Gaussian diagonal band
        // without evaluating exp() independently at every panel pixel.
        // Iso-brightness lines obey u - centerDrift*v = constant.
        const auto field=sticky::glass::reflectionLayout(w,h);
        const qreal tilt=field.centerDrift*w/h;
        const QPointF axis(w/(1.0+tilt*tilt),
                           -tilt*w/(1.0+tilt*tilt));
        const QPointF origin=r.topLeft();
        constexpr qreal low=-0.40,high=1.10;
        QLinearGradient reflectionBand(origin+axis*low,origin+axis*high);
        constexpr int kBandStops=80;
        for(int i=0;i<=kBandStops;++i){
            const qreal normalized=qreal(i)/kBandStops;
            const qreal across=low+(high-low)*normalized;
            const qreal z=(across-field.centerTop)/field.sigma;
            // The original vertical fade ranges from 0.98 to 0.90.
            // Its midpoint (0.94) avoids an extra full-panel alpha pass.
            const qreal opacity=field.strength*std::exp(-0.5*z*z)*0.94;
            reflectionBand.setColorAt(normalized,QColor(248,252,255,alpha(opacity)));
        }
        p.fillRect(r,reflectionBand);
        p.restore(); // Reflections stay inside the rounded glass surface.

        // Continuous signed-distance bevel; right/bottom inner shade uses
        // the same contour to avoid a separate hard inner border.
        QImage bevel(physical,QImage::Format_ARGB32_Premultiplied);
        bevel.setDevicePixelRatio(dpr);
        bevel.fill(Qt::transparent);
        QImage innerShade(physical,QImage::Format_ARGB32_Premultiplied);
        innerShade.setDevicePixelRatio(dpr);
        innerShade.fill(Qt::transparent);
        const QRectF contour=r.adjusted(0.8,0.8,-0.8,-0.8);
        const qreal radius=18.0;
        const qreal cx=contour.center().x(),cy=contour.center().y();
        const qreal halfW=contour.width()/2,halfH=contour.height()/2;
        // Only the ~11 px bevel and 10 px inner shade need SDF work.
        // The large clear center never contributes to either image.
        const int edgePixels=qCeil((radius+kBevelWidth+5.0)*dpr);
        for(int y=0;y<physical.height();++y){
            auto *scan=reinterpret_cast<QRgb*>(bevel.scanLine(y));
            auto *shadeScan=reinterpret_cast<QRgb*>(innerShade.scanLine(y));
            const qreal py=(y+0.5)/dpr;
            const auto paintEdgePixel=[&](int x){
                const qreal px=(x+0.5)/dpr;
                const qreal qx=std::abs(px-cx)-(halfW-radius);
                const qreal qy=std::abs(py-cy)-(halfH-radius);
                // sqrt is only necessary at the four actual rounded corners.
                const qreal distance=(qx>0.0&&qy>0.0
                    ?std::hypot(qx,qy):std::max(qx,qy))-radius;
                if(distance < -kBevelWidth || distance > 1.8)return;
                const qreal inner=std::pow(std::clamp(
                    (distance+kBevelWidth)/kBevelWidth,0.0,1.0),1.45);
                const qreal t=std::clamp(distance/1.8,0.0,1.0);
                const qreal fade=inner*(1.0-t*t*(3.0-2.0*t));
                const qreal direction=std::clamp(
                    0.5+0.6*((px-cx)/std::max<qreal>(1,halfW)+
                             (py-cy)/std::max<qreal>(1,halfH)),0.0,1.0);
                const qreal brightness=(1-direction)*0.45+direction*0.38;
                const int a=alpha(kBevelDepth*brightness*fade);

                if(distance<0.0){
                    const qreal factor=std::clamp((distance+10.0)/10.0,0.0,1.0);
                    const qreal soft=factor*factor*(3.0-2.0*factor);
                    const qreal right=std::clamp(
                        (px-physical.width()/dpr*0.5)/
                        (physical.width()/dpr*0.5),0.0,1.0);
                    const qreal bottom=std::clamp(
                        (py-physical.height()/dpr*0.5)/
                        (physical.height()/dpr*0.5),0.0,1.0);
                    const int shadeAlpha=qBound(0,qRound(
                        30.0*soft*(0.05+0.95*std::max(right,bottom))),255);
                    if(shadeAlpha)shadeScan[x]=qRgba(0,0,0,shadeAlpha);
                }
                if(!a)return;
                const int red=qRound(248*(1-direction)+112*direction);
                const int green=qRound(252*(1-direction)+145*direction);
                const int blue=qRound(255*(1-direction)+162*direction);
                scan[x]=qRgba(red*a/255,green*a/255,blue*a/255,a);
            };
            const bool middleRow=
                py>contour.top()+radius+kBevelWidth+2.0 &&
                py<contour.bottom()-radius-kBevelWidth-2.0 &&
                edgePixels*2<physical.width();
            if(middleRow){
                for(int x=0;x<edgePixels;++x)paintEdgePixel(x);
                for(int x=physical.width()-edgePixels;x<physical.width();++x)
                    paintEdgePixel(x);
            }else{
                for(int x=0;x<physical.width();++x)paintEdgePixel(x);
            }
        }
        p.drawImage(QPointF(0,0),bevel);

        // Thin, continuous cool-grey outline visible on light desktops.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(76,96,113,100),0.95));
        p.drawRoundedRect(r.adjusted(1,1,-1,-1),18.6,18.6);

        // Apply the inner shadow after the rim, as in the A/B Qt lab.
        p.save();
        QPainterPath innerClip;
        innerClip.addRoundedRect(
            r.adjusted(1.4,1.4,-1.4,-1.4),17.6,17.6);
        p.setClipPath(innerClip);
        p.drawImage(QPointF(0,0),innerShade);
        p.restore();
        p.end();
        cache_=std::move(buffer);
        const qint64 bytes=qint64(physical.width())*physical.height()*4;
        sharedGlass.insert(key,new QPixmap(cache_),
                           qMax(1,int((bytes+1023)/1024)));
        qInfo().noquote()<<"Overview glass generated in"<<glassTime.elapsed()
                         <<"ms at"<<physical<<"DPR"<<dpr;
    }
};
}
// Temporarily paints moving notes above the glass in screen coordinates.
// This lets the whole stack open/close at its real desktop location,
// even when that location lies outside the glass panel.
// Screen-wide mouse-transparent drag layer. Unlike the scroll viewport,
// it does not clip a card that is partly outside the glass panel.
class OverviewDragLayer : public QWidget {
    QPixmap image_;
    QRectF target_;
public:
    explicit OverviewDragLayer(QWidget *parent):QWidget(parent){
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }
    void showCard(const QPixmap &image,const QRectF &target){
        image_=image;target_=target;show();raise();update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if(image_.isNull())return;
        QPainter p(this);p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(target_,image_,QRectF(image_.rect()));
        p.setPen(QPen(QColor("#3D7EAA"),2.0));p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(target_.adjusted(1,1,-1,-1),10,10);
    }
};
class OverviewTransitionLayer : public QWidget {
    struct Item { QPixmap pixmap; QRectF source,target; };
    QVector<Item> items_;
    QVariantAnimation motion_;
    qreal progress_=0.0;
    bool closing_=false;
    std::function<void(bool)> finish_;
    std::function<void(qreal)> progressCallback_;
    std::function<void()> afterNextPaint_;
public:
    explicit OverviewTransitionLayer(QWidget *parent):QWidget(parent){
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        motion_.setDuration(245);
        motion_.setEasingCurve(QEasingCurve::InOutCubic);
        connect(&motion_,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
            progress_=v.toReal();update();
            if(progressCallback_)progressCallback_(progress_);
        });
        connect(&motion_,&QVariantAnimation::finished,this,[this]{
            if(finish_)finish_(closing_);
        });
        hide();
    }
    qreal progress() const {return progress_;}
    void setFinished(std::function<void(bool)> callback){finish_=std::move(callback);}
    void setProgressChanged(std::function<void(qreal)> callback){progressCallback_=std::move(callback);}
    // Hand off a real desktop window only after the replacement frame has
    // passed through Qt's paint cycle, not merely after animate() starts.
    void afterNextPaint(std::function<void()> callback){afterNextPaint_=std::move(callback);}
    void animate(bool closing,const QRectF &source,
                 const QVector<QPair<QRectF,QPixmap>> &snapshots,
                 const QPoint &canvasOrigin,bool reverseCurrent=false){
        const qreal start=reverseCurrent?progress_:(closing?1.0:0.0);
        motion_.stop();
        items_.clear();
        for(const auto &s:snapshots){
            if(s.second.isNull())continue;
            items_.append({s.second,source,
                s.first.translated(canvasOrigin.x(),canvasOrigin.y())});
        }
        closing_=closing;
        progress_=start;
        show();raise();
        if(progressCallback_)progressCallback_(progress_);
        motion_.setDuration(std::max(1,qRound(245*std::abs(start-(closing?0.0:1.0)))));
        motion_.setStartValue(start);
        motion_.setEndValue(closing?0.0:1.0);
        motion_.start();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::SmoothPixmapTransform);
        for(const auto &item:items_){
            const QRectF rect(item.source.topLeft()*(1-progress_)+
                              item.target.topLeft()*progress_,
                              item.source.size()*(1-progress_)+
                              item.target.size()*progress_);
            p.drawPixmap(rect,item.pixmap,QRectF(item.pixmap.rect()));
        }
        p.end();
        if(afterNextPaint_){
            auto callback=std::move(afterNextPaint_);
            // PaintEvent runs before the backing-store flush; execute on
            // the next event-loop turn rather than inside the painter.
            QTimer::singleShot(0,this,[callback=std::move(callback)]{callback();});
        }
    }
};
OverviewCanvas::OverviewCanvas(const QVector<Card> &cards,const QString &active,QWidget *parent)
    :QWidget(parent),cards_(cards),active_(active){
    setAttribute(Qt::WA_OpaquePaintEvent,false);
    setMinimumHeight(180);
    sortAnimation_.setDuration(150);
    sortAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&sortAnimation_,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
        sortProgress_=v.toReal();update();
    });
}
void OverviewCanvas::reflow(int w){
    sticky::perf::Scope timing("OverviewCanvas::reflow");
    const qreal currentDpr=devicePixelRatioF();
    if(lastReflowWidth_==w && lastReflowDpr_==currentDpr && !cardCache_.isEmpty())
        return;
    lastReflowWidth_=w;
    lastReflowDpr_=currentDpr;
    const qreal gap=18.0, pad=24.0;
    // Original paper size is the default; never upscale a note for overview.
    // Fit at least two side-by-side when two or more notes exist.
    const qreal paperW=cards_.isEmpty()?330.0:std::max<qreal>(140,cards_.first().paperWidth);
    const qreal paperH=cards_.isEmpty()?270.0:std::max<qreal>(130,cards_.first().paperHeight);
    const qreal available=std::max<qreal>(160,w-2*pad);
    const int minColumns=cards_.size()<2?1:2;
    const qreal fitTwo=std::max<qreal>(0.2,(available-gap)/(2.0*paperW));
    const qreal scale=minColumns==1?std::min<qreal>(1.0,available/paperW):
                                            std::min<qreal>(1.0,fitTwo);
    const qreal cardW=paperW*scale;
    const qreal cardH=paperH*scale;
    columns_=std::min(std::max(minColumns,
        static_cast<int>(std::floor((available+gap)/(cardW+gap)))),5);
    if(!cards_.isEmpty())columns_=std::min(columns_,static_cast<int>(cards_.size()));
    targets_.clear();
    const int rows=(cards_.size()+columns_-1)/columns_;
    for(int row=0;row<rows;++row){
        const int count=std::min(columns_,static_cast<int>(cards_.size())-row*columns_);
        const qreal left=pad; // Stable, left-aligned grid; last row is not centered.
        for(int col=0;col<count;++col){
            targets_.append({left+col*(cardW+gap),pad+row*(cardH+gap),cardW,cardH});
        }
    }
    setMinimumHeight(static_cast<int>(std::ceil(2*pad+rows*(cardH+gap))));
    QElapsedTimer previewTime;
    previewTime.start();
    cardCache_.clear();
    for(int i=0;i<cards_.size()&&i<targets_.size();++i){
        cardCache_.insert(cards_[i].id,
            makeCardPixmap(cards_[i],targets_[i].size().toSize()));
    }
    qInfo().noquote()<<"Overview thumbnails generated in"<<previewTime.elapsed()
                     <<"ms for"<<cards_.size()<<"notes";
    update();
}
QPixmap OverviewCanvas::makeCardPixmap(const Card &c,const QSize &size) const {
    const qreal dpr=devicePixelRatioF();
    // Bounded cache survives closing/reopening Overview. Content, geometry,
    // active status, DPI and chosen typeface all participate in its key.
    static QCache<QString,QPixmap> thumbnails(32*1024); // max cost in KiB
    const QString input=c.id+QChar(u'\0')+c.title+QChar(u'\0')+c.html+QChar(u'\0')+
        c.color+QString("|%1|%2|%3|%4|%5|%6")
            .arg(c.paperWidth).arg(c.paperHeight).arg(size.width())
            .arg(size.height()).arg(dpr).arg(c.id==active_)+
        QApplication::font().toString();
    const QString key=QString::fromLatin1(
        QCryptographicHash::hash(input.toUtf8(),QCryptographicHash::Sha256).toHex());
    if(auto *hit=thumbnails.object(key))return *hit;
    const QSize pixels(std::max(1,static_cast<int>(std::ceil(size.width()*dpr))),
                       std::max(1,static_cast<int>(std::ceil(size.height()*dpr))));
    QPixmap pixmap(pixels);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Layout in the original paper coordinates first, then uniformly scale
    // the *result*. Formatting, line wrapping and list indentation must not
    // change when the same note appears in Overview.
    const qreal width=std::max<qreal>(1,c.paperWidth);
    const qreal height=std::max<qreal>(1,c.paperHeight);
    p.scale(qreal(size.width())/width,qreal(size.height())/height);
    const QRectF paper(1,1,std::max<qreal>(1,width-2),std::max<qreal>(1,height-2));
    const QColor background(c.color);
    const QPen cardOutline(c.id==active_?QColor("#3570A8"):QColor(90,99,113,130),
                           c.id==active_?2:1);
    p.setPen(Qt::NoPen);
    p.setBrush(background.isValid()?background:QColor("#FFF0C8"));
    p.drawRoundedRect(paper,9,9);

    // Match the single-note window's 36-pixel title bar.
    p.save();
    p.setClipRect(QRectF(11,3,std::max<qreal>(1,width-24),30));
    p.setPen(QColor("#38312A"));
    QFont heading=sticky::noteContentFont();
    heading.setPixelSize(12);
    heading.setWeight(QFont::DemiBold);
    p.setFont(heading);
    p.drawText(QRectF(11,3,std::max<qreal>(1,width-24),30),
               Qt::AlignLeft|Qt::AlignVCenter,c.title);
    p.restore();

    // Mirror ReadPane: the paper has a 1px interior layout margin,
    // followed by a 12px left/11px top inset and 24px text-width reserve.
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setIndentWidth(24);
    doc.setDefaultFont(sticky::noteContentFont());
    doc.setHtml(c.html);
    doc.setTextWidth(std::max<qreal>(70,width-2-24));
    p.save();
    p.setClipRect(QRectF(1,37,std::max<qreal>(1,width-2),
                         std::max<qreal>(1,height-36-14-1)));
    p.translate(1+12,1+36+11);
    QAbstractTextDocumentLayout::PaintContext context;
    // The same fixed-color, unselected markers and completion overlay as
    // the editor/reader. Qt keeps the semantic list data for HTML storage.
    for(QTextBlock block=doc.begin();block.isValid();block=block.next())
        sticky::checklist::addCompletionSelection(context,doc,block);
    doc.documentLayout()->draw(&p,context);
    for(QTextBlock block=doc.begin();block.isValid();block=block.next()){
        if(block.textList())
            sticky::checklist::paintMarker(
                p,block,sticky::checklist::documentTextStart(doc,block),
                background.isValid()?background:QColor("#FFF0C8"),
                doc.defaultFont());
    }
    p.restore();
    // The checklist marker gutter uses Source compositing, so draw the
    // card outline last to keep the selected blue edge continuous.
    p.setPen(cardOutline);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(paper,9,9);
    p.end();
    const qint64 bytes=qint64(pixmap.width())*pixmap.height()*4;
    thumbnails.insert(key,new QPixmap(pixmap),qMax(1,int((bytes+1023)/1024)));
    return pixmap;
}
QPixmap OverviewCanvas::draggedPixmap() const {
    return cardCache_.value(draggedId_);
}
QSize OverviewCanvas::fullSizeFor(const QString &id) const {
    for(const Card &c:cards_)if(c.id==id)
        return QSize(qRound(c.paperWidth),qRound(c.paperHeight));
    return {};
}
QPixmap OverviewCanvas::fullPixmapFor(const QString &id) const {
    for(const Card &c:cards_)if(c.id==id)
        return makeCardPixmap(c,fullSizeFor(id));
    return {};
}
QRectF OverviewCanvas::draggedRectGlobal() const {
    const auto it=std::find_if(cards_.cbegin(),cards_.cend(),
        [this](const Card &c){return c.id==draggedId_;});
    if(it==cards_.cend())return {};
    const int i=static_cast<int>(it-cards_.cbegin());
    if(i>=targets_.size())return {};
    QRectF r=targets_[i];r.moveTopLeft(dragAt_-grabOffset_);
    return r.translated(QPointF(mapToGlobal(QPoint(0,0))));
}
void OverviewCanvas::resumeDrag(const QPoint &global){
    if(!draggingCard_)return;
    dragAt_=mapFromGlobal(global);
    emit dragVisualChanged(draggedId_,draggedRectGlobal());
}
void OverviewCanvas::cancelDrag(){
    pressIndex_=-1;draggingCard_=false;draggedId_.clear();
    sortAnimation_.stop();sortProgress_=1;priorRects_.clear();
    if(!originalDragOrder_.isEmpty())cards_=originalDragOrder_;
    originalDragOrder_.clear();suspendReorder_=false;cardsVisible_=true;update();
    emit dragVisualChanged(QString(),{});
}
void OverviewCanvas::setActiveCard(const QString &id){
    if(id==active_)return;
    active_=id;
    // The blue outline is baked into each pixmap. Rebuild those previews
    // before the next closing animation uses them.
    lastReflowWidth_=-1;
    update();
}
QVector<QPair<QRectF,QPixmap>> OverviewCanvas::snapshots(const QString &exclude) const {
    QVector<QPair<QRectF,QPixmap>> shots;
    const int count=std::min(cards_.size(),targets_.size());
    // Qt paints later pixmaps on top. Paint the remaining pages in reverse
    // logical order, so page 1 covers page 2, etc., then paint the active
    // page last so it visually matches the real StickyWindow.
    int activeIndex=-1;
    for(int i=count-1;i>=0;--i){
        if(cards_[i].id==exclude)continue;
        if(cards_[i].id==active_){activeIndex=i;continue;}
        shots.append(qMakePair(targets_[i],cardCache_.value(cards_[i].id)));
    }
    if(activeIndex>=0)
        shots.append(qMakePair(targets_[activeIndex],
                               cardCache_.value(cards_[activeIndex].id)));
    return shots;
}
void OverviewCanvas::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::SmoothPixmapTransform);
    if(!cardsVisible_)return;
    for(int i=0;i<cards_.size()&&i<targets_.size();++i){
        const auto &c=cards_[i];
        if(draggingCard_&&c.id==draggedId_)continue;
        QRectF r=targets_[i];
        if(sortProgress_<1&&priorRects_.contains(c.id)){
            const QRectF old=priorRects_.value(c.id);
            r.moveTopLeft(old.topLeft()*(1-sortProgress_)+r.topLeft()*sortProgress_);
        }
        if(r.bottom()<0||r.top()>height())continue;
        const QPixmap pix=cardCache_.value(c.id);
        if(!pix.isNull())p.drawPixmap(r,pix,QRectF(pix.rect()));
    }
    // The overlay drag layer paints the dragged card above the glass.
}
void OverviewCanvas::mousePressEvent(QMouseEvent *e){
    if(e->button()!=Qt::LeftButton){e->ignore();return;}
    pressIndex_=-1;
    for(int i=0;i<targets_.size();++i){
        if(targets_[i].contains(e->position())){
            pressIndex_=i;
            originalDragOrder_=cards_;
            pressedAt_=dragAt_=e->position();
            draggedId_=cards_[i].id;
            grabOffset_=e->position()-targets_[i].topLeft();
            draggingCard_=false;
            e->accept();return;
        }
    }
    e->ignore();
}
void OverviewCanvas::mouseMoveEvent(QMouseEvent *e){
    if(!(e->buttons()&Qt::LeftButton)||pressIndex_<0){e->ignore();return;}
    const QPointF before=dragAt_;
    dragAt_=e->position();
    if(!draggingCard_&&QLineF(pressedAt_,dragAt_).length()<7){e->accept();return;}
    draggingCard_=true;
    if(suspendReorder_){
        emit dragVisualChanged(draggedId_,draggedRectGlobal());
        e->accept();return;
    }
    // Swap only when the pointer enters another card, not merely because
    // its center is closer. This prevents oscillation near gaps.
    int target=-1;
    for(int i=0;i<targets_.size();++i){
        if(targets_[i].adjusted(5,5,-5,-5).contains(dragAt_)){
            target=i;break;
        }
    }
    const auto it=std::find_if(cards_.begin(),cards_.end(),
        [this](const Card& c){return c.id==draggedId_;});
    if(it!=cards_.end()&&target>=0){
        const int current=static_cast<int>(it-cards_.begin());
        if(target!=current){
            QHash<QString,QRectF> currentRects;
            for(int i=0;i<cards_.size();++i){
                QRectF r=targets_[i];
                if(sortProgress_<1&&priorRects_.contains(cards_[i].id)){
                    const auto old=priorRects_.value(cards_[i].id);
                    r.moveTopLeft(old.topLeft()*(1-sortProgress_)+r.topLeft()*sortProgress_);
                }
                currentRects.insert(cards_[i].id,r);
            }
            auto card=cards_.takeAt(current);
            cards_.insert(target,card);
            sortAnimation_.stop();
            priorRects_=currentRects;
            sortProgress_=0;
            sortAnimation_.setStartValue(0.0);
            sortAnimation_.setEndValue(1.0);
            sortAnimation_.start();
        }
    }
    if(target>=0&&sortProgress_<1.0){
        update();
    }else if(draggingCard_){
        const auto found=std::find_if(cards_.cbegin(),cards_.cend(),
            [this](const Card& card){return card.id==draggedId_;});
        const int index=found==cards_.cend()?-1:
            static_cast<int>(found-cards_.cbegin());
        if(index>=0&&index<targets_.size()){
            const QSizeF size=targets_[index].size();
            const QRectF previous(before-grabOffset_,size);
            const QRectF current(dragAt_-grabOffset_,size);
            update(previous.united(current).adjusted(-4,-4,4,4).toAlignedRect());
        }else update();
    }
    emit dragVisualChanged(draggedId_,draggedRectGlobal());
    e->accept();
}
void OverviewCanvas::mouseReleaseEvent(QMouseEvent *e){
    if(e->button()!=Qt::LeftButton||pressIndex_<0){e->ignore();return;}
    const int original=pressIndex_;
    pressIndex_=-1;
    if(suspendReorder_){
        suspendReorder_=false;
        draggingCard_=false;
        if(!originalDragOrder_.isEmpty())cards_=originalDragOrder_;
        sortAnimation_.stop();sortProgress_=1;priorRects_.clear();
        update();
    }else if(draggingCard_){
        const auto it=std::find_if(cards_.cbegin(),cards_.cend(),
            [this](const Card& c){return c.id==draggedId_;});
        const int finalIndex=static_cast<int>(it-cards_.cbegin());
        draggingCard_=false;
        sortAnimation_.stop();sortProgress_=1;priorRects_.clear();
        update();
        if(finalIndex!=original)emit reordered(original,finalIndex);
    }else if(original<cards_.size()){
        emit selected(cards_[original].id);
    }
    draggedId_.clear();originalDragOrder_.clear();
    emit dragVisualChanged(QString(),{});
    e->accept();
}

OverviewOverlay::OverviewOverlay(sticky::Controller *controller,
                                 const std::string &stackId,QWidget *parent):QWidget(parent),controller_(controller),stackId_(stackId){
    setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *win=controller->window(stackId);
    auto *screen=(win?QGuiApplication::screenAt(win->frameGeometry().center()):nullptr);
    if(!screen)screen=QGuiApplication::primaryScreen();
    const QRect screenRect=screen?screen->availableGeometry():QRect(0,0,1280,720);
    // The outer window is fully transparent. It only gives animated notes
    // enough room to fly from the stack's real desktop coordinates.
    setGeometry(screenRect);
    overviewScreenRect_=screenRect;
    const auto &s=controller->stack(stackId);
    const int paperWidth=std::max(150,s.rect.w-(s.noteIds.size()>1?70:0));
    const int paperHeight=std::max(130,s.rect.h);
    const QPoint paperPos=win?win->mapToGlobal(QPoint(0,0)):QPoint(s.rect.x,s.rect.y);
    sourcePaper_={paperPos,QSize(paperWidth,paperHeight)};
    QVector<OverviewCanvas::Card> cards;
    for(const auto &id:s.noteIds){
        const auto &n=controller->note(id);
        cards.append({QString::fromStdString(id),QString::fromStdString(n.title),
                      QString::fromStdString(n.html),QString::fromStdString(n.color),
                      qreal(paperWidth),qreal(paperHeight)});
    }
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(24,24,24,28);
    layout->addStretch(1);
    auto *centerRow=new QHBoxLayout;
    centerRow->addStretch(1);
    glass_=new GlassPanel(this);
    glass_->setObjectName("glass");
    glass_->setStyleSheet("QLabel{background:transparent;color:#263342;}");
    glass_->setFixedSize(std::max(320,std::min(screenRect.width()-48,
                          static_cast<int>(screenRect.width()*0.75))),
                        std::max(260,std::min(screenRect.height()-52,
                          static_cast<int>(screenRect.height()*0.75))));
    auto *inner=new QVBoxLayout(glass_);
    inner->setContentsMargins(25,19,25,22);
    inner->setSpacing(10);
    auto *top=new QHBoxLayout;
    auto *title=new QLabel(sticky::trUi("OVERVIEW"),glass_);
    // Let the platform + chosen UI language select the entire font family.
    // Do not synthesize bold through a stylesheet: mixed CJK fallback
    // previously made individual glyphs appear in different weights.
    QFont heading=title->font();
    heading.setPixelSize(16);
    heading.setWeight(QFont::DemiBold);
    title->setFont(heading);
    title->setStyleSheet("color:#263342;background:transparent;");
    top->addWidget(title);top->addStretch();
    auto *hint=new QLabel(sticky::trUi("%1 notes · Drag to reorder · Click to open").arg(cards.size()),glass_);
    hint->setStyleSheet("font-size:11px;color:#6C7680;");
    top->addWidget(hint);
    auto *close=new QPushButton("×",glass_);
    close->setFixedSize(32,30);
    close->setStyleSheet("QPushButton{border:0;border-radius:7px;"
        "background:rgba(20,40,60,22);font-size:21px;color:#425164;}"
        "QPushButton:hover{background:rgba(20,40,60,45);}");
    top->addWidget(close);inner->addLayout(top);
    scroller_=new QScrollArea(glass_);
    scroller_->setFrameShape(QFrame::NoFrame);
    scroller_->setWidgetResizable(true);
    scroller_->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    scroller_->verticalScrollBar()->setStyleSheet(sticky::verticalScrollBarStyle());
    canvas_=new OverviewCanvas(cards,QString::fromStdString(s.activeId));
    canvas_->setStyleSheet("background:transparent;");
    scroller_->setWidget(canvas_);
    inner->addWidget(scroller_,1);
    centerRow->addWidget(glass_);
    centerRow->addStretch(1);
    layout->addLayout(centerRow);
    layout->addStretch(1);
    transition_=new OverviewTransitionLayer(this);
    transition_->setObjectName(QStringLiteral("overviewTransitionLayer"));
    dragLayer_=new OverviewDragLayer(this);
    dragLayer_->setGeometry(rect());
    glassOpacity_=new QGraphicsOpacityEffect(glass_);
    glassOpacity_->setOpacity(1);
    glass_->setGraphicsEffect(glassOpacity_);
    glassOpacity_->setEnabled(false); // No offscreen compositing in idle Overview.
    transition_->setProgressChanged([this](qreal progress){
        if(!tearMorphing_ || !glassOpacity_)return;
        glassOpacity_->setOpacity(progress);
        if((tearing_||tearReturning_) && !tearPixmap_.isNull())
            followTearoff(QCursor::pos());
        update();
    });
    reentryTimer_.setSingleShot(true);
    connect(&reentryTimer_,&QTimer::timeout,this,[this]{
        const QPoint cursor=QCursor::pos();
        if(tearing_&&sourcePaper_.contains(cursor)&&
           (QGuiApplication::mouseButtons()&Qt::LeftButton))restoreFromTearoff(cursor);
    });
    transition_->setFinished([this](bool closed){
        if(tearMorphing_){
            transition_->hide();
            glassOpacity_->setOpacity(closed?0.0:1.0);
            if(closed){
                if(tearing_)controller_->setOverviewStackPresentation(stackId_,false);
            }else{
                tearMorphing_=false;tearReturning_=false;
                glassOpacity_->setEnabled(false);
                canvas_->setCardsVisible(true);
                if(!canvas_->isDraggingCard())dragLayer_->hide();
            }
            dragLayer_->raise();
            return;
        }
        if(closed){
            // The final paint normally hands the desktop window back.
            // Windows uses this ordering to avoid a one-frame flash.
            transition_->afterNextPaint([this]{completeAnimatedClose();});
            transition_->update();
#ifdef Q_OS_LINUX
            // Some Linux compositors do not deliver another child paint at
            // animation completion (e.g. an occluded translucent overlay).
            // A missing paint must never leave the real Stack minimized.
            // Prefer the painted handoff; fall back after roughly two frames.
            QTimer::singleShot(40,this,[this]{completeAnimatedClose();});
#endif
        }else{
            transition_->hide();
            canvas_->show();
            canvas_->setFocus();
            sticky::perf::memory("overview-open");
        }
    });
    connect(close,&QPushButton::clicked,this,&OverviewOverlay::closeAnimated);
    connect(canvas_,&OverviewCanvas::selected,this,[this,controller,stackId](QString id){
        controller->activate(stackId,id.toStdString());
        // A newly chosen page is now the topmost page of the returning
        // stack. Refresh its highlighted preview before shrinking the cards.
        canvas_->setActiveCard(id);
        closeAnimated();
    });
    connect(canvas_,&OverviewCanvas::reordered,this,[controller,stackId](int from,int to){
        controller->reorderNotes(stackId,from,to);
    });
    connect(canvas_,&OverviewCanvas::dragVisualChanged,this,
        [this](const QString &id,const QRectF &globalRect){
            if(id.isEmpty()){
                if(tearing_){finishTearoff();return;}
                // An early mouse release during reopen must not make the
                // held paper vanish while the other pages are still flying.
                if(tearReturning_)return;
                dragLayer_->hide();return;
            }
            if(tearing_){followTearoff(QCursor::pos());return;}
            if(tearReturning_){
                // The full-sized paper stays attached to the pointer while
                // remaining cards reopen; change back after the animation.
                followTearoff(QCursor::pos());return;
            }
            dragLayer_->setGeometry(rect());
            dragLayer_->showCard(canvas_->draggedPixmap(),
                globalRect.translated(-QPointF(mapToGlobal(QPoint(0,0)))));
            const QRectF glassGlobal(glass_->mapToGlobal(QPoint(0,0)),
                                     QSizeF(glass_->size()));
            // Re-entry starts outside the glass; do not immediately hide it.
            if(returningToGlass_){
                if(glassGlobal.intersects(globalRect))returningToGlass_=false;
                else return;
            }
            // Full-paper geometry, not mouse location, controls tear-off.
            if(!glassGlobal.intersects(globalRect))beginTearoff(id,globalRect);
        });
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    connect(escape,&QShortcut::activated,this,[this]{
        if(tearing_)cancelTearoff();else closeAnimated();
    });
}
void OverviewOverlay::beginTearoff(const QString &id,const QRectF &globalRect){
    tearSize_=canvas_->fullSizeFor(id);
    tearPixmap_=canvas_->fullPixmapFor(id);
    if(tearSize_.isEmpty()||tearPixmap_.isNull())return;
    tearing_=true;returningToGlass_=false;tearingId_=id;
    const QPoint pointer=QCursor::pos();
    tearGrabFraction_={
        qBound(0.0,(pointer.x()-globalRect.left())/qMax(1.0,globalRect.width()),1.0),
        qBound(0.0,(pointer.y()-globalRect.top())/qMax(1.0,globalRect.height()),1.0)
    };
    // The pressed canvas keeps Qt's implicit mouse grab throughout the drag.
    // No native window resize, reparenting, or grab transfer: those caused
    // the abrupt flashes in the previous implementation.
    canvas_->setTearoffActive(true);
    canvas_->setCardsVisible(false);
    glassOpacity_->setEnabled(true);
    tearMorphing_=true;
    beginTransition(true,id);
    followTearoff(pointer);
    dragLayer_->raise();
}
void OverviewOverlay::followTearoff(const QPoint &global){
    const QPoint top(global.x()-qRound(tearGrabFraction_.x()*tearSize_.width()),
                     global.y()-qRound(tearGrabFraction_.y()*tearSize_.height()));
    tearRect_=QRect(top,tearSize_);
    // The paper stays pinned to the same cursor grab point while its size
    // interpolates between full Stack paper and the Overview card. It must
    // never jump from one discrete pixmap size to the other.
    const QRectF large=QRectF(tearRect_.translated(-mapToGlobal(QPoint(0,0))));
    const QRectF currentSmall=canvas_->draggedRectGlobal();
    if(currentSmall.isValid())lastSmallGlobal_=currentSmall;
    const QRectF small=lastSmallGlobal_.translated(
        -QPointF(mapToGlobal(QPoint(0,0))));
    const qreal blend=tearMorphing_?transition_->progress():1.0;
    const QRectF visual(large.topLeft()*(1-blend)+small.topLeft()*blend,
                        large.size()*(1-blend)+small.size()*blend);
    dragLayer_->showCard(tearPixmap_,visual);
    if(tearing_ && sourcePaper_.contains(global)){
        if(!reentryTimer_.isActive())reentryTimer_.start(180);
    }else reentryTimer_.stop();
}
void OverviewOverlay::restoreFromTearoff(const QPoint &global){
    if(!tearing_)return;
    reentryTimer_.stop();tearing_=false;returningToGlass_=true;
    tearReturning_=true;
    controller_->setOverviewStackPresentation(stackId_,true);
    // Reverse the closing animation from its current position, rather than
    // reopening from a fresh frame when the pointer crosses the boundary.
    beginTransition(false,tearingId_,true);
    canvas_->setTearoffActive(false);
    canvas_->resumeDrag(global);
    dragLayer_->raise();
}
void OverviewOverlay::cancelTearoff(){
    reentryTimer_.stop();tearing_=false;returningToGlass_=false;
    canvas_->setTearoffActive(false);
    controller_->setOverviewStackPresentation(stackId_,true);
    tearReturning_=true;
    beginTransition(false,tearingId_,true);
    canvas_->cancelDrag();canvas_->setFocus();
    dragLayer_->hide();
}
void OverviewOverlay::finishTearoff(){
    if(!tearing_)return;
    reentryTimer_.stop();
    const QString id=tearingId_;
    const QRect finalRect=tearRect_;
    // Qt must finish dispatching mouseReleaseEvent before this window's
    // note structure changes or the overlay is destroyed.
    QTimer::singleShot(0,this,[this,id,finalRect]{
        tearing_=false;returningToGlass_=false;
        canvas_->cancelDrag();
        controller_->setOverviewStackPresentation(stackId_,false);
        hide();
        controller_->detachFromOverview(stackId_,id.toStdString(),finalRect);
        deleteLater();
    });
}
void OverviewOverlay::completeAnimatedClose(){
    if(!closing_||!isVisible()||closeHandoffStarted_)return;
    closeHandoffStarted_=true;
    // The source returns behind the visible overlay to prevent a blank gap.
    controller_->setOverviewStackPresentation(stackId_,false);
    raise();
    QTimer::singleShot(0,this,[this]{
        transition_->hide();
        hide();
        deleteLater();
    });
}
void OverviewOverlay::beginTransition(bool closing,const QString &exclude,bool reverseCurrent){
    if(!transition_||!canvas_||!isVisible())return;
    canvas_->reflow(scroller_->viewport()->width());
    const auto items=canvas_->snapshots(exclude);
    const QRectF start(QPointF(mapFromGlobal(sourcePaper_.topLeft())),
                       QSizeF(sourcePaper_.size()));
    const QPoint destination=canvas_->mapTo(this,QPoint(0,0));
    if(!tearMorphing_)canvas_->hide();
    transition_->setGeometry(rect());
    transition_->animate(closing,start,items,destination,reverseCurrent);
}
void OverviewOverlay::open(){
    // Avoid briefly showing the fully expanded cards before the first
    // transition frame. The source StickyWindow remains visible meanwhile.
    canvas_->hide();
    show();raise();activateWindow();setFocus();
    QTimer::singleShot(0,this,[this]{
        if(closing_)return;
        transition_->afterNextPaint([this]{
            if(!closing_&&isVisible())
                controller_->setOverviewStackPresentation(stackId_,true);
        });
        beginTransition(false);
    });
}
void OverviewOverlay::closeAnimated(){
    if(closing_||!isVisible())return;
    closing_=true;
    // Reverse from the current interpolation position if the user closes
    // before the opening animation finishes; do not jump to its endpoint.
    beginTransition(true,{},true);
}
void OverviewOverlay::paintEvent(QPaintEvent *){
    if(!glass_)return;
    // Layered shadow matching the Colab test:
    // draw a shifted pane silhouette, blur the alpha mask, cut out the
    // transparent glass interior. No SDF contact gap or directional masks.
    const QRect glassBounds=glass_->geometry();
    const QRect bounds=glassBounds.adjusted(-24,-24,56,62);
    const qreal dpr=devicePixelRatioF();
    if(shadowCache_.isNull() || shadowCacheRect_!=bounds ||
       shadowCacheDpr_!=dpr){
        shadowCacheRect_=bounds;shadowCacheDpr_=dpr;
        // Cast shadows are low-frequency; half-resolution alpha is sufficient.
        // Render at 1/2 the final device pixel density, then upscale smoothly.
        const qreal sampleScale=0.5*dpr;
        const QSize physical(qMax(1,qCeil(bounds.width()*sampleScale)),
                             qMax(1,qCeil(bounds.height()*sampleScale)));
        static QCache<QString,QPixmap> sharedShadows(64*1024);
        const QString key=QString("layered-halfres-a2-20-v1-%1x%2@%3")
            .arg(physical.width()).arg(physical.height()).arg(dpr,0,'f',3);
        if(auto *cached=sharedShadows.object(key)){
            shadowCache_=*cached;
        }else{
            QElapsedTimer shadowTime;
            shadowTime.start();
            constexpr qreal kShadowX=23.0;
            constexpr qreal kShadowY=32.0;
            constexpr qreal kShadowBlur=18.0;
            constexpr qreal kShadowAlpha=80.0;

            // The pane matches the bevel contour (3.3 px inset
            // relative to the glass widget) and its 18 px corner.
            const QRectF pane=QRectF(glassBounds)
                .translated(-bounds.topLeft())
                .adjusted(3.3,3.3,-3.3,-3.3);
            QImage mask(physical,QImage::Format_ARGB32_Premultiplied);
            mask.fill(Qt::transparent);
            {
                QPainter p(&mask);
                p.scale(sampleScale,sampleScale);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                p.drawRoundedRect(
                    pane.translated(kShadowX,kShadowY),18.0,18.0);
            }

            // Three separable box passes approximate a Gaussian blur
            // (variance approximately sigma^2), linearly per pixel.
            // The effect is cached and never recalculated during animation.
            const int W=physical.width(),H=physical.height();
            const int radius=qMax(1,qRound(kShadowBlur*sampleScale));
            const float divisor=float(radius*2+1);
            std::vector<float> alpha(size_t(W)*H),scratch(alpha.size());
            for(int y=0;y<H;++y){
                const auto *scan=reinterpret_cast<const QRgb*>(mask.constScanLine(y));
                for(int x=0;x<W;++x)
                    alpha[size_t(y)*W+x]=float(qAlpha(scan[x]));
            }
            for(int pass=0;pass<3;++pass){
                // Horizontal box filter; outside the image is transparent.
                for(int y=0;y<H;++y){
                    const size_t row=size_t(y)*W;
                    float sum=0.0f;
                    for(int k=0;k<=radius && k<W;++k)
                        sum+=alpha[row+k];
                    for(int x=0;x<W;++x){
                        scratch[row+x]=sum/divisor;
                        if(x+radius+1<W)sum+=alpha[row+x+radius+1];
                        if(x-radius>=0)sum-=alpha[row+x-radius];
                    }
                }
                // Vertical box filter.
                for(int x=0;x<W;++x){
                    float sum=0.0f;
                    for(int k=0;k<=radius && k<H;++k)
                        sum+=scratch[size_t(k)*W+x];
                    for(int y=0;y<H;++y){
                        alpha[size_t(y)*W+x]=sum/divisor;
                        if(y+radius+1<H)
                            sum+=scratch[size_t(y+radius+1)*W+x];
                        if(y-radius>=0)
                            sum-=scratch[size_t(y-radius)*W+x];
                    }
                }
            }

            QImage image(physical,QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            for(int y=0;y<H;++y){
                auto *scan=reinterpret_cast<QRgb*>(image.scanLine(y));
                for(int x=0;x<W;++x){
                    const int a=qBound(0,qRound(
                        alpha[size_t(y)*W+x]*kShadowAlpha/255.0),255);
                    if(a)scan[x]=qRgba(39*a/255,61*a/255,77*a/255,a);
                }
            }

            // Transparent glass must not show its own cast shadow.
            {
                QPainter p(&image);
                p.scale(sampleScale,sampleScale);
                p.setRenderHint(QPainter::Antialiasing);
                p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                p.setPen(Qt::NoPen);
                // A2: retain 20% of the shifted shadow beneath clear glass.
                p.setBrush(QColor(0,0,0,204));
                p.drawRoundedRect(pane,18.0,18.0);
            }

            shadowCache_=QPixmap::fromImage(std::move(image));
            const qint64 bytes=qint64(physical.width())*physical.height()*4;
            sharedShadows.insert(key,new QPixmap(shadowCache_),
                                 qMax(1,int((bytes+1023)/1024)));
            qInfo().noquote()<<"Overview shadow A2 generated in"
                             <<shadowTime.elapsed()<<"ms at"<<physical
                             <<"DPR"<<dpr;
        }
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(tearMorphing_?glassOpacity_->opacity():1.0);
    p.drawPixmap(QRectF(bounds),shadowCache_,QRectF(shadowCache_.rect()));
}
void OverviewOverlay::resizeEvent(QResizeEvent *e){
    QWidget::resizeEvent(e);
    if(dragLayer_)dragLayer_->setGeometry(rect());
    QTimer::singleShot(0,this,[this]{
        if(!tearing_&&canvas_&&scroller_)canvas_->reflow(scroller_->viewport()->width());
    });
}
void OverviewOverlay::keyPressEvent(QKeyEvent *e){
    if(e->key()==Qt::Key_Escape){
        if(tearing_)cancelTearoff();else closeAnimated();
        e->accept();return;
    }
    QWidget::keyPressEvent(e);
}
void OverviewOverlay::mousePressEvent(QMouseEvent *e){
    if(!tearing_&&glass_&&!glass_->geometry().contains(e->position().toPoint()))closeAnimated();
    QWidget::mousePressEvent(e);
}

void OverviewOverlay::mouseMoveEvent(QMouseEvent *e){
    if(tearing_&&(e->buttons()&Qt::LeftButton)){
        followTearoff(e->globalPosition().toPoint());
        e->accept();return;
    }
    QWidget::mouseMoveEvent(e);
}
void OverviewOverlay::mouseReleaseEvent(QMouseEvent *e){
    if(tearing_&&e->button()==Qt::LeftButton){
        followTearoff(e->globalPosition().toPoint());
        finishTearoff();
        e->accept();return;
    }
    QWidget::mouseReleaseEvent(e);
}
