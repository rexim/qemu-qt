#ifndef GRAPHICSVNCVIEW_H_
#define GRAPHICSVNCVIEW_H_

#include "graphicsremoteview.h"
#include "vncclientthread.h"

#ifdef QTONLY
    class KConfigGroup{};
#else
    #include "vnchostpreferences.h"
#endif

#include <QClipboard>

extern "C" {
#include <rfb/rfbclient.h>
}

class GraphicsVncView: public GraphicsRemoteView
{
    Q_OBJECT

public:
    explicit GraphicsVncView(QWidget *parent = 0, const KUrl &url = KUrl(), KConfigGroup configGroup = KConfigGroup());
    ~GraphicsVncView();

    QSize framebufferSize();
    QSize sizeHint() const;
    QSize minimumSizeHint() const;
    void startQuitting();
    bool isQuitting();
    bool start();
    bool supportsScaling() const;
    bool supportsLocalCursor() const;

#ifndef QTONLY
    HostPreferences* hostPreferences();
#endif

    void setViewOnly(bool viewOnly);
    void showDotCursor(DotCursorState state);
    void enableScaling(bool scale);

    virtual void updateConfiguration();

public slots:
    void scaleResize(int w, int h);

protected:
    void paintEvent(QPaintEvent *event);
    bool event(QEvent *event);
    void resizeEvent(QResizeEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

private:
    VncClientThread vncThread;
    QClipboard *m_clipboard;
    bool m_initDone;
    int m_buttonMask;
    QMap<unsigned int, bool> m_mods;
    int m_x, m_y, m_w, m_h;
    bool m_repaint;
    bool m_quitFlag;
    bool m_firstPasswordTry;
    bool m_authenticaionCanceled;
    bool m_dontSendClipboard;
    qreal m_horizontalFactor;
    qreal m_verticalFactor;
#ifndef QTONLY
    VncHostPreferences *m_hostPreferences;
#endif
    QImage m_frame;
    bool m_forceLocalCursor;

    void keyEventHandler(QKeyEvent *e);
    void unpressModifiers();
    void wheelEventHandler(QWheelEvent *event);
    void mouseEventHandler(QMouseEvent *event);

private slots:
    void updateImage(int x, int y, int w, int h);
    void setCut(const QString &text);
    void requestPassword();
    void outputErrorMessage(const QString &message);
    void clipboardSelectionChanged();
    void clipboardDataChanged();
};

#endif  // GRAPHICSVNCVIEW_H_
