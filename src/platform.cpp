#include "platform.h"
#include <QGuiApplication>
#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(Q_OS_LINUX) && defined(STACKIES_HAVE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif
namespace sticky::platform {
#ifdef Q_OS_WIN
namespace {
struct Hit {
    POINT point{};
    HWND dragged=nullptr,preview=nullptr,tabPreview=nullptr,front=nullptr;
};
HWND topWindowExcluding(QWidget *dragged,QWidget *preview,QWidget *tabPreview){
    Hit hit;
    hit.dragged=dragged?reinterpret_cast<HWND>(dragged->winId()):nullptr;
    hit.preview=preview?reinterpret_cast<HWND>(preview->winId()):nullptr;
    hit.tabPreview=tabPreview?reinterpret_cast<HWND>(tabPreview->winId()):nullptr;
    if(!GetCursorPos(&hit.point))return nullptr;
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
        auto &state=*reinterpret_cast<Hit*>(data);
        if(hwnd==state.dragged||hwnd==state.preview||hwnd==state.tabPreview)return TRUE;
        if(!IsWindowVisible(hwnd)||IsIconic(hwnd))return TRUE;
        if((GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0)return TRUE;
        RECT rect{};
        if(!GetWindowRect(hwnd,&rect)||!PtInRect(&rect,state.point))return TRUE;
        state.front=hwnd;return FALSE;
    },reinterpret_cast<LPARAM>(&hit));
    return hit.front;
}
}
#endif
#if defined(Q_OS_LINUX) && defined(STACKIES_HAVE_X11)
namespace {
// EWMH changes the stacking state of a mapped X11 window without unmapping
// it. QWidget::setWindowFlag() hides/recreates the window and flickers its
// taskbar entry. This request also works through XWayland when Qt uses xcb.
bool requestX11Topmost(QWidget *window,bool topmost){
    if(QGuiApplication::platformName()!=QStringLiteral("xcb")||!window->isVisible())
        return false;
    Display *display=XOpenDisplay(nullptr);
    if(!display)return false;
    const auto xid=static_cast<::Window>(window->winId());
    XWindowAttributes attributes{};
    const bool mapped=XGetWindowAttributes(display,xid,&attributes) &&
                      attributes.map_state==IsViewable;
    const Atom state=XInternAtom(display,"_NET_WM_STATE",True);
    const Atom above=XInternAtom(display,"_NET_WM_STATE_ABOVE",True);
    if(!mapped||state==None||above==None){
        XCloseDisplay(display);
        return false;
    }
    const ::Window root=RootWindowOfScreen(attributes.screen);
    XEvent event{};
    event.xclient.type=ClientMessage;
    event.xclient.window=xid;
    event.xclient.message_type=state;
    event.xclient.format=32;
    event.xclient.data.l[0]=topmost?1:0; // EWMH ADD / REMOVE
    event.xclient.data.l[1]=static_cast<long>(above);
    event.xclient.data.l[3]=1; // Normal application source
    const Status queued=XSendEvent(display,root,False,
        SubstructureRedirectMask|SubstructureNotifyMask,&event);
    if(queued)XFlush(display);
    XCloseDisplay(display);
    // Successful delivery is not a guarantee the WM will honor the request.
    return queued!=0;
}
}
#endif
void raiseStack(QWidget *window,bool permanentlyTopmost){
    if(!window)return;
#ifdef Q_OS_WIN
    SetWindowPos(reinterpret_cast<HWND>(window->winId()),
        permanentlyTopmost?HWND_TOPMOST:HWND_TOP,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
#else
    Q_UNUSED(permanentlyTopmost)
    window->raise();
#endif
}
void setStackTopmost(QWidget *window,bool topmost){
    if(!window)return;
#ifdef Q_OS_WIN
    // QWidget::setWindowFlag remaps top-level windows on Windows and causes
    // a taskbar disappearance/reappearance. Change the native Z-order only.
    SetWindowPos(reinterpret_cast<HWND>(window->winId()),
        topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
#else
#if defined(Q_OS_LINUX) && defined(STACKIES_HAVE_X11)
    if(requestX11Topmost(window,topmost))return;
#endif
    // Wayland (and unsupported X11 WMs) have no universal equivalent.
    // Retain existing Qt fallback so we do not silently lose pin/unpin.
    const bool wasVisible=window->isVisible();
    window->setWindowFlag(Qt::WindowStaysOnTopHint,topmost);
    if(wasVisible)window->show();
#endif
}
bool raiseForDrag(QWidget *window,bool permanentlyTopmost){
    if(!window)return false;
#ifdef Q_OS_WIN
    SetWindowPos(reinterpret_cast<HWND>(window->winId()),
        HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#else
    // Qt topmost hints are compositor dependent on macOS, X11 and Wayland.
    // Do not mutate the persistent model's topmost bit.
    window->raise();
#endif
    return !permanentlyTopmost;
}
void restoreAfterDrag(QWidget *window,bool wasTemporarilyTopmost){
    if(!window||!wasTemporarilyTopmost)return;
#ifdef Q_OS_WIN
    SetWindowPos(reinterpret_cast<HWND>(window->winId()),
        HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#else
    // No temporary native topmost promotion was applied.
    Q_UNUSED(window)
#endif
}
bool hasReliableForeignWindowHitTest(){
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
QWidget *stackAt(const QPoint &global,const std::vector<QWidget*> &candidates,
                 QWidget *dragged,QWidget *mergePreview,QWidget *tabPreview){
#ifdef Q_OS_WIN
    // Native Z-order matters: merging into a Stack hidden beneath a browser
    // must never be possible. Match only the frontmost unexcluded HWND.
    const HWND front=topWindowExcluding(dragged,mergePreview,tabPreview);
    if(!front)return nullptr;
    for(QWidget *candidate:candidates){
        if(candidate&&candidate!=dragged&&candidate->isVisible()&&
           reinterpret_cast<HWND>(candidate->winId())==front&&
           candidate->frameGeometry().adjusted(16,18,-16,-18).contains(global))
            return candidate;
    }
    return nullptr;
#else
    Q_UNUSED(mergePreview)
    Q_UNUSED(tabPreview)
    // Compatibility fallback until native stacking is implemented per OS.
    // On Wayland, geometry/top-level placement is compositor-controlled.
    // Return no target on Wayland rather than silently merge hidden windows.
    if(QGuiApplication::platformName().contains("wayland",Qt::CaseInsensitive))
        return nullptr;
    for(QWidget *candidate:candidates){
        if(candidate&&candidate!=dragged&&candidate->isVisible()&&
           candidate->frameGeometry().adjusted(16,18,-16,-18).contains(global))
            return candidate;
    }
    return nullptr;
#endif
}
}
