#include "QGCKeychain.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QLatin1StringView>
#include <QtCore/QSettings>
#ifdef QGC_HAS_KEYCHAIN
#include <QtCore/QEventLoop>
#include <atomic>
#include <mutex>
#include <qtkeychain/keychain.h>
#endif

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(QGCKeychainLog, "Utilities.Platform.QGCKeychain")

namespace {

constexpr QLatin1StringView kSettingsGroup{"KeychainFallback"};

#ifdef QGC_HAS_KEYCHAIN
QString serviceName()
{
    return QCoreApplication::applicationName();
}
#endif

QSettings settingsStore()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
}

bool fallbackWrite(const QString& key, const QByteArray& data)
{
    QSettings s = settingsStore();
    s.beginGroup(kSettingsGroup);
    s.setValue(key, data);
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

QByteArray fallbackRead(const QString& key)
{
    QSettings s = settingsStore();
    s.beginGroup(kSettingsGroup);
    const QByteArray result = s.value(key).toByteArray();
    s.endGroup();
    return result;
}

bool fallbackRemove(const QString& key)
{
    QSettings s = settingsStore();
    s.beginGroup(kSettingsGroup);
    s.remove(key);
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

#ifdef QGC_HAS_KEYCHAIN
// Once the backend proves unavailable, skip all future probes for the session.
std::atomic<bool> g_qtkeychainUnavailable{false};

// One-time probe: avoids per-call DBus round-trip and KDE unlock prompt on a doomed session.
void probeBackendOnce()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
        if (!QKeychain::isAvailable()) {
            qCInfo(QGCKeychainLog) << "qtkeychain reports no available backend; using QSettings fallback";
            g_qtkeychainUnavailable.store(true, std::memory_order_relaxed);
        }
    });
}

QKeychain::Error runJob(QKeychain::Job* job)
{
    if (!QCoreApplication::instance()) {
        qCWarning(QGCKeychainLog) << "No QCoreApplication — skipping keychain operation";
        return QKeychain::OtherError;
    }
    QEventLoop loop;
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->start();
    loop.exec();
    return job->error();
}

bool indicatesMissingBackend(QKeychain::Error err)
{
    // NoBackendAvailable = headless; AccessDenied = dismissed prompt — both treated as missing.
    return err == QKeychain::NoBackendAvailable || err == QKeychain::AccessDenied;
}

// Linux libsecret backend reports DBus ServiceUnknown as OtherError — recognize it as "no backend".
bool isMissingSecretService(QKeychain::Error err, const QString& errorString)
{
    if (err != QKeychain::OtherError) {
        return false;
    }
    return errorString.contains(QLatin1String("org.freedesktop.secrets")) ||
           errorString.contains(QLatin1String("ServiceUnknown"));
}
#endif // QGC_HAS_KEYCHAIN

}  // namespace

bool QGCKeychain::writeBinary(const QString& key, const QByteArray& data)
{
#ifdef QGC_HAS_KEYCHAIN
    probeBackendOnce();
    if (!g_qtkeychainUnavailable.load(std::memory_order_relaxed)) {
        QKeychain::WritePasswordJob job(serviceName());
        job.setInsecureFallback(false);  // we manage our own fallback to keep one canonical store
        job.setKey(key);
        job.setBinaryData(data);
        const auto err = runJob(&job);
        if (err == QKeychain::NoError) {
            return true;
        }
        if (indicatesMissingBackend(err) || isMissingSecretService(err, job.errorString())) {
            qCInfo(QGCKeychainLog) << "qtkeychain unavailable, using QSettings fallback:" << job.errorString();
            g_qtkeychainUnavailable.store(true, std::memory_order_relaxed);
        } else {
            qCWarning(QGCKeychainLog) << "Keychain write error:" << job.errorString();
            return false;
        }
    }
#endif // QGC_HAS_KEYCHAIN
    // No qtkeychain backend built in (headless ghost, QGC_ENABLE_QML=OFF) --
    // no D-Bus session/keychain daemon to talk to anyway, so go straight to
    // the same QSettings-backed store used as qtkeychain's runtime fallback.
    return fallbackWrite(key, data);
}

QByteArray QGCKeychain::readBinary(const QString& key)
{
#ifdef QGC_HAS_KEYCHAIN
    probeBackendOnce();
    if (!g_qtkeychainUnavailable.load(std::memory_order_relaxed)) {
        QKeychain::ReadPasswordJob job(serviceName());
        job.setInsecureFallback(false);
        job.setKey(key);
        const auto err = runJob(&job);
        if (err == QKeychain::NoError) {
            return job.binaryData();
        }
        if (indicatesMissingBackend(err) || isMissingSecretService(err, job.errorString())) {
            qCInfo(QGCKeychainLog) << "qtkeychain unavailable, using QSettings fallback:" << job.errorString();
            g_qtkeychainUnavailable.store(true, std::memory_order_relaxed);
            // fall through to QSettings
        } else if (err == QKeychain::EntryNotFound) {
            // Entry may have been written by a prior session with no backend — try fallback store.
        } else {
            qCWarning(QGCKeychainLog) << "Keychain read error:" << job.errorString();
            return {};
        }
    }
#endif // QGC_HAS_KEYCHAIN
    return fallbackRead(key);
}

bool QGCKeychain::remove(const QString& key)
{
    bool keychainOk = false;
#ifdef QGC_HAS_KEYCHAIN
    probeBackendOnce();
    if (!g_qtkeychainUnavailable.load(std::memory_order_relaxed)) {
        QKeychain::DeletePasswordJob job(serviceName());
        job.setInsecureFallback(false);
        job.setKey(key);
        const auto err = runJob(&job);
        if (err == QKeychain::NoError || err == QKeychain::EntryNotFound) {
            keychainOk = true;
        } else if (indicatesMissingBackend(err) || isMissingSecretService(err, job.errorString())) {
            qCInfo(QGCKeychainLog) << "qtkeychain unavailable, using QSettings fallback:" << job.errorString();
            g_qtkeychainUnavailable.store(true, std::memory_order_relaxed);
        } else {
            qCWarning(QGCKeychainLog) << "Keychain remove error:" << job.errorString();
        }
    }
#endif // QGC_HAS_KEYCHAIN
    // Always clear the QSettings fallback too — entry may live there from a prior session.
    const bool fallbackOk = fallbackRemove(key);
    return keychainOk || fallbackOk;
}
