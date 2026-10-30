#pragma once
#include <QWidget>
#include <QVariantAnimation>
#include <QString>
#include <QVector>
#include <QRectF>
#include <QHash>
#include <QPointF>
#include <QPixmap>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <string>
class QScrollArea;
namespace sticky { class Controller; }
class OverviewCanvas : public QWidget {
    Q_OBJECT
public:
    struct Card { QString id,title,html,color; qreal paperWidth=330, paperHeight=270; };
private:
    QVector<Card> cards_;
    QVector<Card> originalDragOrder_;
    QVector<QRectF> targets_;
    QString active_;
    int columns_=3;
    QVariantAnimation sortAnimation_;
    QHash<QString,QPixmap> cardCache_;
    int lastReflowWidth_=-1;
    qreal lastReflowDpr_=-1;
    QHash<QString,QRectF> priorRects_;
    qreal sortProgress_=1;
    int pressIndex_=-1;
    bool draggingCard_=false;
    bool cardsVisible_=true;
    bool suspendReorder_=false;
    QPointF pressedAt_;
    QPointF dragAt_;
    QPointF grabOffset_;
    QString draggedId_;
public:
    OverviewCanvas(const QVector<Card>&cards,const QString &active,QWidget *parent=nullptr);
    void reflow(int viewportWidth);
    void setActiveCard(const QString &id);
    // Back-to-front transition layers: lower-priority pages first, active
    // page last. The Overview grid itself remains in logical note order.
    QVector<QPair<QRectF,QPixmap>> snapshots(const QString &exclude={}) const;
    void setCardsVisible(bool visible){ cardsVisible_=visible;update(); }
    bool isDraggingCard() const {return draggingCard_;}
    void setTearoffActive(bool active){ suspendReorder_=active; }
    QPixmap draggedPixmap() const;
    QPixmap fullPixmapFor(const QString &id) const;
    QSize fullSizeFor(const QString &id) const;
    QRectF draggedRectGlobal() const;
    void resumeDrag(const QPoint &global);
    void cancelDrag();
signals:
    void selected(QString noteId);
    void reordered(int from,int to);
    void dragVisualChanged(QString id,QRectF globalRect);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QPixmap makeCardPixmap(const Card &card,const QSize &size) const;
};
class OverviewTransitionLayer;
class OverviewDragLayer;
class OverviewOverlay : public QWidget {
    Q_OBJECT
    OverviewCanvas *canvas_=nullptr;
    QScrollArea *scroller_=nullptr;
    QWidget *glass_=nullptr;
    QPixmap shadowCache_;
    QRect shadowCacheRect_;
    qreal shadowCacheDpr_=-1;
    OverviewTransitionLayer *transition_=nullptr;
    OverviewDragLayer *dragLayer_=nullptr;
    sticky::Controller *controller_=nullptr;
    std::string stackId_;
    bool tearing_=false;
    bool returningToGlass_=false;
    QString tearingId_;
    QSize tearSize_;
    QPixmap tearPixmap_;
    QPointF tearGrabFraction_;
    QRect tearRect_;
    QRectF lastSmallGlobal_;
    QTimer reentryTimer_;
    QRect sourcePaper_;
    QRect overviewScreenRect_;
    bool closing_=false;
    bool closeHandoffStarted_=false;
    bool tearMorphing_=false;
    bool tearReturning_=false;
    class QGraphicsOpacityEffect *glassOpacity_=nullptr;
    void completeAnimatedClose();
    void beginTransition(bool closing,const QString &exclude={},bool reverseCurrent=false);
    void beginTearoff(const QString &id,const QRectF &globalRect);
    void followTearoff(const QPoint &global);
    void restoreFromTearoff(const QPoint &global);
    void cancelTearoff();
    void finishTearoff();
public:
    OverviewOverlay(sticky::Controller *controller,const std::string &stackId,QWidget *parent=nullptr);
    void open();
    void closeAnimated();
protected:
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
};
