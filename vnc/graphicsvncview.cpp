#include "graphicsvncview.h"

#ifdef QTONLY
    #include <QMessageBox>
    #include <QInputDialog>
    #define KMessageBox QMessageBox
#else
    #include "settings.h"
    #include <KActionCollection>
    #include <KMainWindow>
    #include <KMessageBox>
    #include <KPasswordDialog>
    #include <KXMLGUIClient>
#endif

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>

// Definition of key modifier mask constants
#define KMOD_Alt_R 	0x01
#define KMOD_Alt_L 	0x02
#define KMOD_Meta_L 	0x04
#define KMOD_Control_L 	0x08
#define KMOD_Shift_L	0x10

GraphicsVncView::GraphicsVncView(QWidget *parent, const KUrl &url, KConfigGroup configGroup)
        : GraphicsRemoteView(parent),
        m_initDone(false),
        m_buttonMask(0),
        m_repaint(false),
        m_quitFlag(false),
        m_firstPasswordTry(true),
        m_authenticaionCanceled(false),
        m_dontSendClipboard(false),
        m_horizontalFactor(1.0),
        m_verticalFactor(1.0),
        m_forceLocalCursor(false)
{
    m_url = url;
    m_host = url.host();
    m_port = url.port();

    connect(&vncThread, SIGNAL(imageUpdated(int, int, int, int)), this, SLOT(updateImage(int, int, int, int)), Qt::BlockingQueuedConnection);
    connect(&vncThread, SIGNAL(gotCut(const QString&)), this, SLOT(setCut(const QString&)), Qt::BlockingQueuedConnection);
    connect(&vncThread, SIGNAL(passwordRequest()), this, SLOT(requestPassword()), Qt::BlockingQueuedConnection);
    connect(&vncThread, SIGNAL(outputErrorMessage(QString)), this, SLOT(outputErrorMessage(QString)));

    m_clipboard = QApplication::clipboard();
    connect(m_clipboard, SIGNAL(selectionChanged()), this, SLOT(clipboardSelectionChanged()));
    connect(m_clipboard, SIGNAL(dataChanged()), this, SLOT(clipboardDataChanged()));

#ifndef QTONLY
    m_hostPreferences = new VncHostPreferences(configGroup, this);
#else
    Q_UNUSED(configGroup);
#endif
}

GraphicsVncView::~GraphicsVncView()
{
    unpressModifiers();

    // Disconnect all signals so that we don't get any more callbacks from the client thread
    disconnect(&vncThread, SIGNAL(imageUpdated(int, int, int, int)), this, SLOT(updateImage(int, int, int, int)));
    disconnect(&vncThread, SIGNAL(gotCut(const QString&)), this, SLOT(setCut(const QString&)));
    disconnect(&vncThread, SIGNAL(passwordRequest()), this, SLOT(requestPassword()));
    disconnect(&vncThread, SIGNAL(outputErrorMessage(QString)), this, SLOT(outputErrorMessage(QString)));

    startQuitting();
}

bool GraphicsVncView::eventFilter(QObject *obj, QEvent *event)
{
    if (m_viewOnly) {
        if (event->type() == QEvent::KeyPress ||
                event->type() == QEvent::KeyRelease ||
                event->type() == QEvent::MouseButtonDblClick ||
                event->type() == QEvent::MouseButtonPress ||
                event->type() == QEvent::MouseButtonRelease ||
                event->type() == QEvent::Wheel ||
                event->type() == QEvent::MouseMove)
            return true;
    }

    return GraphicsRemoteView::eventFilter(obj, event);
}

QSize GraphicsVncView::framebufferSize()
{
    return m_frame.size();
}

QSize GraphicsVncView::sizeHint() const
{
    return size();
}

QSize GraphicsVncView::minimumSizeHint() const
{
    return size();
}

void GraphicsVncView::scaleResize(int w, int h)
{
    GraphicsRemoteView::scaleResize(w, h);

    kDebug(5011) << w << h;
    if (m_scale) {
        m_verticalFactor = (qreal) h / m_frame.height();
        m_horizontalFactor = (qreal) w / m_frame.width();

#ifndef QTONLY
        if (Settings::keepAspectRatio()) {
            m_verticalFactor = m_horizontalFactor = qMin(m_verticalFactor, m_horizontalFactor);
        }
#else
        m_verticalFactor = m_horizontalFactor = qMin(m_verticalFactor, m_horizontalFactor);
#endif

        const qreal newW = m_frame.width() * m_horizontalFactor;
        const qreal newH = m_frame.height() * m_verticalFactor;
        setMaximumSize(newW, newH); //This is a hack to force Qt to center the view in the scroll area
        resize(newW, newH);
    }
}

void GraphicsVncView::updateConfiguration()
{
    GraphicsRemoteView::updateConfiguration();

    // Update the scaling mode in case KeepAspectRatio changed
    scaleResize(parentWidget()->width(), parentWidget()->height());
}

void GraphicsVncView::startQuitting()
{
    kDebug(5011) << "about to quit";

    const bool connected = status() == GraphicsRemoteView::Connected;

    setStatus(Disconnecting);

    m_quitFlag = true;

    if (connected) {
        vncThread.stop();
    }

    vncThread.quit();

    const bool quitSuccess = vncThread.wait(500);

    kDebug(5011) << "Quit VNC thread success:" << quitSuccess;

    setStatus(Disconnected);
}

bool GraphicsVncView::isQuitting()
{
    return m_quitFlag;
}

bool GraphicsVncView::start()
{
    if(m_host.isEmpty())
        vncThread.setHost(m_url.path());
    else
    {
        vncThread.setHost(m_host);
        vncThread.setPort(m_port);
    }
    RemoteView::Quality quality;
#ifdef QTONLY
    quality = (RemoteView::Quality)((QCoreApplication::arguments().count() > 2) ?
        QCoreApplication::arguments().at(2).toInt() : 2);
#else
    quality = m_hostPreferences->quality();
#endif

    vncThread.setQuality(quality);

    // set local cursor on by default because low quality mostly means slow internet connection
    if (quality == RemoteView::Low) {
        showDotCursor(GraphicsRemoteView::CursorOn);
#ifndef QTONLY
        // KRDC does always just have one main window, so at(0) is safe
        KXMLGUIClient *mainWindow = dynamic_cast<KXMLGUIClient*>(KMainWindow::memberList().at(0));
        if (mainWindow)
            mainWindow->actionCollection()->action("show_local_cursor")->setChecked(true);
#endif
    }

    setStatus(Connecting);

    vncThread.start();
    return true;
}

bool GraphicsVncView::supportsScaling() const
{
    return true;
}

bool GraphicsVncView::supportsLocalCursor() const
{
    return true;
}

void GraphicsVncView::requestPassword()
{
    kDebug(5011) << "request password";

    if (m_authenticaionCanceled) {
        startQuitting();
        return;
    }

    setStatus(Authenticating);

#ifndef QTONLY
    if (m_hostPreferences->walletSupport()) {
        QString walletPassword = readWalletPassword();

        if (!walletPassword.isNull()) {
            vncThread.setPassword(walletPassword);
            return;
        }
    }
#endif

    if (!m_url.password().isNull()) {
        vncThread.setPassword(m_url.password());
        return;
    }

#ifdef QTONLY
    bool ok;
    QString password = QInputDialog::getText(this, //krazy:exclude=qclasses
                                             tr("Password required"),
                                             tr("Please enter the password for the remote desktop:"),
                                             QLineEdit::Password, QString(), &ok);
    m_firstPasswordTry = false;
    if (ok)
        vncThread.setPassword(password);
    else
        m_authenticaionCanceled = true;
#else
    KPasswordDialog dialog(this);
    dialog.setPrompt(m_firstPasswordTry ? i18n("Access to the system requires a password.")
                                        : i18n("Authentication failed. Please try again."));
    if (dialog.exec() == KPasswordDialog::Accepted) {
        m_firstPasswordTry = false;
        vncThread.setPassword(dialog.password());
    } else {
        kDebug(5011) << "password dialog not accepted";
        m_authenticaionCanceled = true;
    }
#endif
}

void GraphicsVncView::outputErrorMessage(const QString &message)
{
    kDebug(5011) << message;

    if (message == "INTERNAL:APPLE_VNC_COMPATIBILTY") {
        setCursor(localDotCursor());
        m_forceLocalCursor = true;
        return;
    }

    startQuitting();

#ifndef QTONLY
    KMessageBox::error(this, message, i18n("VNC failure"));
#endif
    emit errorMessage(i18n("VNC failure"), message);
}

#ifndef QTONLY
HostPreferences* GraphicsVncView::hostPreferences()
{
    return m_hostPreferences;
}
#endif

void GraphicsVncView::updateImage(int x, int y, int w, int h)
{
//     kDebug(5011) << "got update" << width() << height();

    m_x = x;
    m_y = y;
    m_w = w;
    m_h = h;

    if (m_horizontalFactor != 1.0 || m_verticalFactor != 1.0) {
        // If the view is scaled, grow the update rectangle to avoid artifacts
        m_x-=1;
        m_y-=1;
        m_w+=2;
        m_h+=2;
    }

    m_frame = vncThread.image();

    if (!m_initDone) {
        setAttribute(Qt::WA_StaticContents);
        setAttribute(Qt::WA_OpaquePaintEvent);
        installEventFilter(this);

        setCursor(((m_dotCursorState == CursorOn) || m_forceLocalCursor) ? localDotCursor() : Qt::BlankCursor);

        setMouseTracking(true); // get mouse events even when there is no mousebutton pressed
        setFocusPolicy(Qt::WheelFocus);
        setStatus(Connected);
//         emit framebufferSizeChanged(m_frame.width(), m_frame.height());
        emit connected();

        if (m_scale) {
            if (parentWidget())
                scaleResize(parentWidget()->width(), parentWidget()->height());
        }

        m_initDone = true;

#ifndef QTONLY
        if (m_hostPreferences->walletSupport()) {
            saveWalletPassword(vncThread.password());
        }
#endif
    }

    if ((y == 0 && x == 0) && (m_frame.size() != size())) {
        kDebug(5011) << "Updating framebuffer size";
        if (m_scale) {
            setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
            if (parentWidget())
                scaleResize(parentWidget()->width(), parentWidget()->height());
        } else {
            kDebug(5011) << "Resizing: " << m_frame.width() << m_frame.height();
            resize(m_frame.width(), m_frame.height());
            setMaximumSize(m_frame.width(), m_frame.height()); //This is a hack to force Qt to center the view in the scroll area
            setMinimumSize(m_frame.width(), m_frame.height());
        }
        emit framebufferSizeChanged(m_frame.width(), m_frame.height());
    }

    m_repaint = true;
    repaint(qRound(m_x * m_horizontalFactor), qRound(m_y * m_verticalFactor), qRound(m_w * m_horizontalFactor), qRound(m_h * m_verticalFactor));
    m_repaint = false;
}

void GraphicsVncView::setViewOnly(bool viewOnly)
{
    GraphicsRemoteView::setViewOnly(viewOnly);

    m_dontSendClipboard = viewOnly;

    if (viewOnly)
        setCursor(Qt::ArrowCursor);
    else
        setCursor(m_dotCursorState == CursorOn ? localDotCursor() : Qt::BlankCursor);
}

void GraphicsVncView::showDotCursor(DotCursorState state)
{
    GraphicsRemoteView::showDotCursor(state);

    setCursor(state == CursorOn ? localDotCursor() : Qt::BlankCursor);
}

void GraphicsVncView::enableScaling(bool scale)
{
    GraphicsRemoteView::enableScaling(scale);

    if (scale) {
        setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        setMinimumSize(1, 1);
        if (parentWidget())
            scaleResize(parentWidget()->width(), parentWidget()->height());
    } else {
        m_verticalFactor = 1.0;
        m_horizontalFactor = 1.0;

        setMaximumSize(m_frame.width(), m_frame.height()); //This is a hack to force Qt to center the view in the scroll area
        setMinimumSize(m_frame.width(), m_frame.height());
        resize(m_frame.width(), m_frame.height());
    }
}

void GraphicsVncView::setCut(const QString &text)
{
    m_dontSendClipboard = true;
    m_clipboard->setText(text, QClipboard::Clipboard);
    m_clipboard->setText(text, QClipboard::Selection);
    m_dontSendClipboard = false;
}

void GraphicsVncView::paintEvent(QPaintEvent *event)
{
//     kDebug(5011) << "paint event: x: " << m_x << ", y: " << m_y << ", w: " << m_w << ", h: " << m_h;
    if (m_frame.isNull() || m_frame.format() == QImage::Format_Invalid) {
        kDebug(5011) << "no valid image to paint";
        //GraphicsRemoteView::paintEvent(event);
        return;
    }

    //event->accept();

    QPainter painter(this);

    if (m_repaint) {
//         kDebug(5011) << "normal repaint";
        painter.drawImage(QRect(qRound(m_x*m_horizontalFactor), qRound(m_y*m_verticalFactor),
                                qRound(m_w*m_horizontalFactor), qRound(m_h*m_verticalFactor)),
                          m_frame.copy(m_x, m_y, m_w, m_h).scaled(qRound(m_w*m_horizontalFactor),
                                                                  qRound(m_h*m_verticalFactor),
                                                                  Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
//         kDebug(5011) << "resize repaint";
        QRect rect = event->rect();
        if (rect.width() != width() || rect.height() != height()) {
//             kDebug(5011) << "Partial repaint";
            const int sx = rect.x()/m_horizontalFactor;
            const int sy = rect.y()/m_verticalFactor;
            const int sw = rect.width()/m_horizontalFactor;
            const int sh = rect.height()/m_verticalFactor;
            painter.drawImage(rect,
                              m_frame.copy(sx, sy, sw, sh).scaled(rect.width(), rect.height(),
                                                                  Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        } else {
//             kDebug(5011) << "Full repaint" << width() << height() << m_frame.width() << m_frame.height();
            painter.drawImage(QRect(0, 0, width(), height()),
                              m_frame.scaled(m_frame.width() * m_horizontalFactor, m_frame.height() * m_verticalFactor,
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
    }

    //GraphicsRemoteView::paintEvent(event);
}

void GraphicsVncView::resizeEvent(QResizeEvent *event)
{
    GraphicsRemoteView::resizeEvent(event);
    update();
}

bool GraphicsVncView::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
//         kDebug(5011) << "keyEvent";
        keyEventHandler(static_cast<QKeyEvent*>(event));
        return true;
        break;
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
//         kDebug(5011) << "mouseEvent";
        mouseEventHandler(static_cast<QMouseEvent*>(event));
        return true;
        break;
    case QEvent::Wheel:
//         kDebug(5011) << "wheelEvent";
        wheelEventHandler(static_cast<QWheelEvent*>(event));
        return true;
        break;
    default:
        return GraphicsRemoteView::event(event);
    }
}

void GraphicsVncView::mouseEventHandler(QMouseEvent *e)
{
    if (e->type() != QEvent::MouseMove) {
        if ((e->type() == QEvent::MouseButtonPress) ||
                (e->type() == QEvent::MouseButtonDblClick)) {
            if (e->button() & Qt::LeftButton)
                m_buttonMask |= 0x01;
            if (e->button() & Qt::MidButton)
                m_buttonMask |= 0x02;
            if (e->button() & Qt::RightButton)
                m_buttonMask |= 0x04;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            if (e->button() & Qt::LeftButton)
                m_buttonMask &= 0xfe;
            if (e->button() & Qt::MidButton)
                m_buttonMask &= 0xfd;
            if (e->button() & Qt::RightButton)
                m_buttonMask &= 0xfb;
        }
    }

    vncThread.mouseEvent(qRound(e->x() / m_horizontalFactor), qRound(e->y() / m_verticalFactor), m_buttonMask);
}

void GraphicsVncView::wheelEventHandler(QWheelEvent *event)
{
    int eb = 0;
    if (event->delta() < 0)
        eb |= 0x10;
    else
        eb |= 0x8;

    const int x = qRound(event->x() / m_horizontalFactor);
    const int y = qRound(event->y() / m_verticalFactor);

    vncThread.mouseEvent(x, y, eb | m_buttonMask);
    vncThread.mouseEvent(x, y, m_buttonMask);
}

void GraphicsVncView::keyEventHandler(QKeyEvent *e)
{
// parts of this code are based on http://italc.sourcearchive.com/documentation/1.0.9.1/vncview_8cpp-source.html
    rfbKeySym k = e->nativeVirtualKey();

    // we do not handle Key_Backtab separately as the Shift-modifier
    // is already enabled
    if (e->key() == Qt::Key_Backtab) {
        k = XK_Tab;
    }

    const bool pressed = (e->type() == QEvent::KeyPress);

    // handle modifiers
    if (k == XK_Shift_L || k == XK_Control_L || k == XK_Meta_L || k == XK_Alt_L) {
        if (pressed) {
            m_mods[k] = true;
        } else if (m_mods.contains(k)) {
            m_mods.remove(k);
        } else {
            unpressModifiers();
        }
    }

    if (k) {
        vncThread.keyEvent(k, pressed);
    }
}

void GraphicsVncView::unpressModifiers()
{
    const QList<unsigned int> keys = m_mods.keys();
    QList<unsigned int>::const_iterator it = keys.constBegin();
    while (it != keys.end()) {
        vncThread.keyEvent(*it, false);
        it++;
    }
    m_mods.clear();
}

void GraphicsVncView::clipboardSelectionChanged()
{
    kDebug(5011);

    if (m_status != Connected)
        return;

    if (m_clipboard->ownsSelection() || m_dontSendClipboard)
        return;

    const QString text = m_clipboard->text(QClipboard::Selection);

    vncThread.clientCut(text);
}

void GraphicsVncView::clipboardDataChanged()
{
    kDebug(5011);

    if (m_status != Connected)
        return;

    if (m_clipboard->ownsClipboard() || m_dontSendClipboard)
        return;

    const QString text = m_clipboard->text(QClipboard::Clipboard);

    vncThread.clientCut(text);
}
