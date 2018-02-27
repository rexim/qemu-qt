#include "graphicsremoteview.h"

#ifndef QTONLY
    #include <KDebug>
    #include <KStandardDirs>
#endif

#include <QBitmap>

GraphicsRemoteView::GraphicsRemoteView(QWidget *parent)
        : QWidget(parent),
        m_status(Disconnected),
        m_host(QString()),
        m_port(0),
        m_viewOnly(false),
        m_grabAllKeys(false),
        m_scale(false),
        m_keyboardIsGrabbed(false),
#ifndef QTONLY
        m_wallet(0),
#endif
        m_dotCursorState(CursorOff)
{
}

GraphicsRemoteView::~GraphicsRemoteView()
{
#ifndef QTONLY
    delete m_wallet;
#endif
}

GraphicsRemoteView::RemoteStatus GraphicsRemoteView::status()
{
    return m_status;
}

void GraphicsRemoteView::setStatus(GraphicsRemoteView::RemoteStatus s)
{
    if (m_status == s)
        return;

    if (((1+ m_status) != s) && (s != Disconnected)) {
        // follow state transition rules

        if (s == Disconnecting) {
            if (m_status == Disconnected)
                return;
        } else {
            Q_ASSERT(((int) s) >= 0);
            if (m_status > s) {
                m_status = Disconnected;
                emit statusChanged(Disconnected);
            }
            // smooth state transition
            RemoteStatus origState = m_status;
            for (int i = origState; i < s; ++i) {
                m_status = (RemoteStatus) i;
                emit statusChanged((RemoteStatus) i);
            }
        }
    }
    m_status = s;
    emit statusChanged(m_status);
}

bool GraphicsRemoteView::supportsScaling() const
{
    return false;
}

bool GraphicsRemoteView::supportsLocalCursor() const
{
    return false;
}

QString GraphicsRemoteView::host()
{
    return m_host;
}

QSize GraphicsRemoteView::framebufferSize()
{
    return QSize(0, 0);
}

void GraphicsRemoteView::startQuitting()
{
}

bool GraphicsRemoteView::isQuitting()
{
    return false;
}

int GraphicsRemoteView::port()
{
    return m_port;
}

void GraphicsRemoteView::updateConfiguration()
{
}

void GraphicsRemoteView::keyEvent(QKeyEvent *)
{
}

bool GraphicsRemoteView::viewOnly()
{
    return m_viewOnly;
}

void GraphicsRemoteView::setViewOnly(bool viewOnly)
{
    m_viewOnly = viewOnly;
}

bool GraphicsRemoteView::grabAllKeys()
{
    return m_grabAllKeys;
}

void GraphicsRemoteView::setGrabAllKeys(bool grabAllKeys)
{
    m_grabAllKeys = grabAllKeys;

    if (grabAllKeys) {
        m_keyboardIsGrabbed = true;
        grabKeyboard();
    } else if (m_keyboardIsGrabbed) {
        releaseKeyboard();
    }
}

void GraphicsRemoteView::showDotCursor(DotCursorState state)
{
    m_dotCursorState = state;
}

GraphicsRemoteView::DotCursorState GraphicsRemoteView::dotCursorState() const
{
    return m_dotCursorState;
}

bool GraphicsRemoteView::scaling() const
{
    return m_scale;
}

void GraphicsRemoteView::enableScaling(bool scale)
{
    m_scale = scale;
}

void GraphicsRemoteView::switchFullscreen(bool)
{
}

void GraphicsRemoteView::scaleResize(int, int)
{
}

KUrl GraphicsRemoteView::url()
{
    return m_url;
}

#ifndef QTONLY
QString GraphicsRemoteView::readWalletPassword(bool fromUserNameOnly)
{
    const QString KRDCFOLDER = "KRDC";

    window()->setDisabled(true); // WORKAROUND: disable inputs so users cannot close the current tab (see #181230)
    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), window()->winId());
    window()->setDisabled(false);

    if (m_wallet) {
        bool walletOK = m_wallet->hasFolder(KRDCFOLDER);
        if (!walletOK) {
            walletOK = m_wallet->createFolder(KRDCFOLDER);
            kDebug(5010) << "Wallet folder created";
        }
        if (walletOK) {
            kDebug(5010) << "Wallet OK";
            m_wallet->setFolder(KRDCFOLDER);
            QString password;

            QString key;
            if (fromUserNameOnly)
                key = m_url.userName();
            else
                key = m_url.prettyUrl(KUrl::RemoveTrailingSlash);

            if (m_wallet->hasEntry(key) &&
                    !m_wallet->readPassword(key, password)) {
                kDebug(5010) << "Password read OK";

                return password;
            }
        }
    }
    return QString();
}

void GraphicsRemoteView::saveWalletPassword(const QString &password, bool fromUserNameOnly)
{
    QString key;
    if (fromUserNameOnly)
        key = m_url.userName();
    else
        key = m_url.prettyUrl(KUrl::RemoveTrailingSlash);

    if (m_wallet && m_wallet->isOpen() && !m_wallet->hasEntry(key)) {
        kDebug(5010) << "Write wallet password";
        m_wallet->writePassword(key, password);
    }
}
#endif

QCursor GraphicsRemoteView::localDotCursor() const
{
#ifdef QTONLY
    return QCursor(); //TODO
#else
    QBitmap cursorBitmap(KGlobal::dirs()->findResource("appdata",
                                                       "pics/pointcursor.png"));
    QBitmap cursorMask(KGlobal::dirs()->findResource("appdata",
                                                     "pics/pointcursormask.png"));
    return QCursor(cursorBitmap, cursorMask);
#endif
}

void GraphicsRemoteView::focusInEvent(QFocusEvent *event)
{
    if (m_grabAllKeys) {
        m_keyboardIsGrabbed = true;
        grabKeyboard();
    }

    QWidget::focusInEvent(event);
}

void GraphicsRemoteView::focusOutEvent(QFocusEvent *event)
{
    if (m_grabAllKeys || m_keyboardIsGrabbed) {
        m_keyboardIsGrabbed = false;
        releaseKeyboard();
    }

    QWidget::focusOutEvent(event);
}
