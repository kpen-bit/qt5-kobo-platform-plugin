
#include "koboplatformintegration.h"

#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#include <QtFbSupport/private/qfbbackingstore_p.h>
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtInputSupport/private/qevdevtouchmanager_p.h>
#include <QtServiceSupport/private/qgenericunixservices_p.h>
#include <qpa/qplatforminputcontextfactory_p.h>

KoboPlatformIntegration::KoboPlatformIntegration(const QStringList &paramList)
    : m_paramList(paramList),
      m_primaryScreen(nullptr),
      m_inputContext(nullptr),
      m_fontDb(new QGenericUnixFontDatabase),
      m_services(new QGenericUnixServices),
      m_kbdMgr(nullptr),
      koboKeyboard(nullptr),
      koboAdditions(nullptr),
      debug(false)
{
    koboDevice = determineDevice();

    if (!m_primaryScreen)
        m_primaryScreen = new KoboFbScreen(paramList, &koboDevice);
}

KoboPlatformIntegration::~KoboPlatformIntegration()
{
    QWindowSystemInterface::handleScreenRemoved(m_primaryScreen);
}

void KoboPlatformIntegration::initialize()
{
    if (m_primaryScreen->initialize())
        QWindowSystemInterface::handleScreenAdded(m_primaryScreen);
    else
        qWarning("kobofb: Failed to initialize screen");

    m_inputContext = QPlatformInputContextFactory::create();

    createInputHandlers();
}

bool KoboPlatformIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap)
    {
        case ThreadedPixmaps:
            return true;
        case WindowManagement:
            return false;
        default:
            return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformBackingStore *KoboPlatformIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QFbBackingStore(window);
}

QPlatformWindow *KoboPlatformIntegration::createPlatformWindow(QWindow *window) const
{
    return new QFbWindow(window);
}

QAbstractEventDispatcher *KoboPlatformIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

QList<QPlatformScreen *> KoboPlatformIntegration::screens() const
{
    QList<QPlatformScreen *> list;
    list.append(m_primaryScreen);
    return list;
}

QPlatformFontDatabase *KoboPlatformIntegration::fontDatabase() const
{
    return m_fontDb.data();
}

QPlatformServices *KoboPlatformIntegration::services() const
{
    return m_services.data();
}

void KoboPlatformIntegration::createInputHandlers()
{
    QString touchscreenDevice("/dev/input/event1");
    QRegularExpression rotTouchRx("touchscreen_rotate=(.*)");
    QRegularExpression touchDevRx("touchscreen_device=(.*)");
    QRegularExpression touchDriverRx("touchscreen_driver=(.*)");
    QRegularExpression touchInvXRx("touchscreen_invert_x=(.*)");
    QRegularExpression touchInvYRx("touchscreen_invert_y=(.*)");

    for (const QString &arg : qAsConst(m_paramList))
    {
        if (arg.contains("debug"))
            debug = true;

        QRegularExpressionMatch match;
        if (arg.contains(touchDevRx, &match))
            touchscreenDevice = match.captured(1);
        if (arg.contains(rotTouchRx, &match))
        {
            bool ok = false;
            int rotation = match.captured(1).toInt(&ok);
            if (ok)
                koboDevice.touchscreenTransform.rotation = rotation;
        }
        if (arg.contains(touchInvXRx, &match) && match.captured(1).toInt() > 0)
        {
            koboDevice.touchscreenTransform.invertX = true;
        }
        if (arg.contains(touchInvYRx, &match) && match.captured(1).toInt() > 0)
        {
            koboDevice.touchscreenTransform.invertY = true;
        }
    }

    QString evdevTouchArgs(
        QString("%1:rotate=%2").arg(touchscreenDevice).arg(koboDevice.touchscreenTransform.rotation));
    if (koboDevice.touchscreenTransform.invertX)
        evdevTouchArgs += ":invertx";
    if (koboDevice.touchscreenTransform.invertY)
        evdevTouchArgs += ":inverty";

    new QEvdevTouchManager("EvdevTouch", evdevTouchArgs, this);

    koboKeyboard = new KoboButtonIntegration(this, "/dev/input/event0", debug);
    koboAdditions = new KoboPlatformAdditions(this, koboDevice);
}

QPlatformNativeInterface *KoboPlatformIntegration::nativeInterface() const
{
    return const_cast<KoboPlatformIntegration *>(this);
}

KoboDeviceDescriptor *KoboPlatformIntegration::deviceDescriptor()
{
    return &koboDevice;
}

QFunctionPointer KoboPlatformIntegration::platformFunction(const QByteArray &function) const
{
    if (function == KoboPlatformFunctions::setFrontlightLevelIdentifier())
        return QFunctionPointer(setFrontlightLevelStatic);
    else if (function == KoboPlatformFunctions::getBatteryLevelIdentifier())
        return QFunctionPointer(getBatteryLevelStatic);
    else if (function == KoboPlatformFunctions::isBatteryChargingIdentifier())
        return QFunctionPointer(isBatteryChargingStatic);
    else if (function == KoboPlatformFunctions::setPartialRefreshModeIdentifier())
        return QFunctionPointer(setPartialRefreshModeStatic);
    else if (function == KoboPlatformFunctions::doManualRefreshIdentifier())
        return QFunctionPointer(doManualRefreshStatic);
    else if (function == KoboPlatformFunctions::getKoboDeviceDescriptorIdentifier())
        return QFunctionPointer(getKoboDeviceDescriptorStatic);

    else if (function == KoboPlatformFunctions::testInternetConnectionIdentifier())
        return QFunctionPointer(KoboWifiManager::testInternetConnection);
    else if (function == KoboPlatformFunctions::enableWiFiConnectionIdentifier())
        return QFunctionPointer(KoboWifiManager::enableWiFiConnection);
    else if (function == KoboPlatformFunctions::disableWiFiConnectionIdentifier())
        return QFunctionPointer(KoboWifiManager::disableWiFiConnection);

    return 0;
}

void KoboPlatformIntegration::setFrontlightLevelStatic(int val, int temp)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->koboAdditions->setFrontlightLevel(val, temp);
}

int KoboPlatformIntegration::getBatteryLevelStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return self->koboAdditions->getBatteryLevel();
}

bool KoboPlatformIntegration::isBatteryChargingStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return self->koboAdditions->isBatteryCharging();
}

void KoboPlatformIntegration::setPartialRefreshModeStatic(PartialRefreshMode partial_refresh_mode)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->setPartialRefreshMode(partial_refresh_mode);
}

void KoboPlatformIntegration::doManualRefreshStatic(QRect region)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->doManualRefresh(region);
}

KoboDeviceDescriptor KoboPlatformIntegration::getKoboDeviceDescriptorStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return *self->deviceDescriptor();
}
