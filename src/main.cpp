#include "controller.h"
#include "datapath.h"
#include "singleinstance.h"
#include "diagnostics.h"
#include "i18n.h"
#include <QDebug>
#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFile>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QColor>
#include <QEvent>
#include <QPainterPath>
#include <QRegion>
#include <QWidget>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QSessionManager>
#include <QHelpEvent>
#include <QScreen>
#include <QToolTip>
#include <QFontMetrics>
#include <QTimer>
#include <algorithm>
#include <exception>
#include <stdexcept>

// Own the complete shape of in-app tooltips. Mixing the Qt stylesheet's
// rounded content with an OS-managed rectangular popup shadow creates the
// square second layer seen on Windows. No native tooltip shadow is requested.
class RoundedTooltipWindow : public QWidget {
    QString caption_;
public:
    RoundedTooltipWindow()
        : QWidget(nullptr,Qt::ToolTip|Qt::FramelessWindowHint|
                         Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void present(const QString &caption,const QPoint &anchor){
        caption_=caption;
        setFont(QToolTip::font());
        const QRect measured=fontMetrics().boundingRect(
            QRect(0,0,320,900),Qt::TextWordWrap,caption_);
        resize(std::max(30,measured.width()+18),
               std::max(fontMetrics().height()+12,measured.height()+12));
        const QScreen *screen=QGuiApplication::screenAt(anchor)
            ?QGuiApplication::screenAt(anchor):QGuiApplication::primaryScreen();
        QPoint pos=anchor+QPoint(12,18);
        if(screen){
            const QRect area=screen->availableGeometry();
            if(pos.x()+width()>area.right()+1)pos.setX(area.right()-width()+1);
            if(pos.y()+height()>area.bottom()+1)pos.setY(anchor.y()-height()-10);
            pos.setX(std::max(area.left(),pos.x()));
            pos.setY(std::max(area.top(),pos.y()));
        }
        move(pos);
        update();
        show();
        raise();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#A6A7A5"),1));
        p.setBrush(QColor("#FFF9EB"));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5,0.5,-0.5,-0.5),5,5);
        p.setPen(QColor("#26313B"));
        p.drawText(rect().adjusted(9,6,-9,-6),
                   Qt::TextWordWrap|Qt::AlignVCenter,caption_);
    }
};
class TooltipPresenter : public QObject {
    RoundedTooltipWindow tip_;
    QWidget *owner_=nullptr;
    QTimer timeout_;
public:
    TooltipPresenter(){
        timeout_.setInterval(8000);
        timeout_.setSingleShot(true);
        connect(&timeout_,&QTimer::timeout,this,[this]{dismiss();});
    }
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()==QEvent::ToolTip){
            auto *widget=qobject_cast<QWidget*>(object);
            if(!widget||widget==&tip_||widget->toolTip().isEmpty())
                return false;
            auto *help=static_cast<QHelpEvent*>(event);
            owner_=widget;
            tip_.present(widget->toolTip(),help->globalPos());
            timeout_.start();
            event->accept();
            return true; // Do not create Qt's separate native tooltip window.
        }
        if(object==owner_ &&
           (event->type()==QEvent::Leave||event->type()==QEvent::Hide||
            event->type()==QEvent::MouseButtonPress||
            event->type()==QEvent::WindowDeactivate)){
            dismiss();
        }
        return QObject::eventFilter(object,event);
    }
private:
    void dismiss(){timeout_.stop();tip_.hide();owner_=nullptr;}
};

int main(int argc,char **argv){
    QApplication app(argc,argv);
    QApplication::setOrganizationName("Stackies");
    QApplication::setApplicationName("Stackies");
#ifdef Q_OS_LINUX
    // GNOME/KDE associate the running window with stackies.desktop, not
    // solely with the icon embedded in the Qt process.
    QGuiApplication::setDesktopFileName(QStringLiteral("stackies"));
#endif
    QApplication::setApplicationVersion(STACKIES_APP_VERSION);
    const QString language=sticky::effectiveUiLocale();
    QTranslator translator;
    if(!sticky::loadUiTranslator(translator,language))
        qWarning("Stackies: translation missing for %s",qPrintable(language));
    else if(language!="en")app.installTranslator(&translator);
    sticky::applyUiFont(app,language);
    // Verify every bundled locale, not only the .qm build output. Also
    // ensure its translated message is actually visible via the context.
    if(app.arguments().contains("--verify-translations")){
        for(const QString &code:sticky::supportedUiLocales()){
            if(code=="en")continue;
            QTranslator check;
            if(!sticky::loadUiTranslator(check,code))return 2;
            app.installTranslator(&check);
            const bool translated=sticky::trUi("Quit")!=QStringLiteral("Quit");
            app.removeTranslator(&check);
            if(!translated)return 3;
        }
        return 0;
    }
    QIcon appIcon;
    // Render the original SVG for larger icons, and the simplified variant
    // for 16/20/24px taskbar and tray surfaces.
    for(int size:{16,20,24,32,48,64,128,256}){
        const QString asset=size<32?":/app/small.svg":":/app/icon.svg";
        QFile file(asset);
        if(!file.open(QIODevice::ReadOnly))continue;
        QSvgRenderer renderer(file.readAll());
        if(!renderer.isValid())continue;
        QPixmap image(size,size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();
        appIcon.addPixmap(image);
    }
    app.setWindowIcon(appIcon);
    sticky::installDiagnostics();
    qInfo("Application started v%s", qPrintable(QApplication::applicationVersion()));
    QApplication::setQuitOnLastWindowClosed(false);
    // Intercept internal QWidget tooltip requests before Qt paints its own
    // separately styled top-level popup. The app retains native tooltips
    // outside QWidget (for example the system tray).
    TooltipPresenter tooltipPresenter;
    app.installEventFilter(&tooltipPresenter);
    const QString uiFamily=app.font().family();
    app.setStyleSheet(QString("QMenu { font-family: \"%1\"; }").arg(uiFamily));
    try{
        sticky::Controller controller;
        sticky::SingleInstance instance(sticky::stackiesDataDirectory());
        const auto started=instance.start([&controller]{
            controller.activateFromSecondInstance();
        });
        if(started==sticky::SingleInstance::Result::Forwarded)return 0;
        if(started!=sticky::SingleInstance::Result::Primary)
            throw std::runtime_error("Cannot establish the Stackies single-instance lock or activation channel");
        controller.initialize();
        QObject::connect(&app,&QGuiApplication::commitDataRequest,&controller,
            [&controller](QSessionManager &){controller.prepareForShutdown();});
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&controller,
            [&controller]{controller.prepareForShutdown();});
        const int result=app.exec();
        qInfo("Application exited (code %d)",result);
        return result;
    }catch(const std::exception &e){
        qCritical("Startup error: %s",e.what());
        QMessageBox::critical(nullptr,"Stackies",sticky::trUi("Unable to start:\n%1").arg(e.what()));
        return 1;
    }
}
