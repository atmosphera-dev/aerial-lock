// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QColor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>
#ifdef IA_TEST_PLAYGROUND
#include <QTemporaryDir>
#include <QLockFile>
#include <memory>
#endif

class QPointingDevice;

// Public Qt APIs only: a popup's content item identifies its effective host.
class TestProbe : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit TestProbe(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QQuickWindow *hostWindow(QQuickItem *item) const;
    Q_INVOKABLE QRect screenAvailableGeometry(QQuickItem *item) const;
    Q_INVOKABLE bool exposed(QQuickItem *item) const;
    Q_INVOKABLE bool render(QQuickItem *item, int timeout = 5000) const;
    Q_INVOKABLE bool activate(QQuickItem *item) const;
    Q_INVOKABLE bool activateForInput(QQuickItem *item) const;
    Q_INVOKABLE bool inputHostReady(QQuickItem *item) const;
    Q_INVOKABLE bool click(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE bool press(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE bool move(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE bool release(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE bool keyClick(QQuickItem *hostItem, int key, int modifiers = 0) const;
    Q_INVOKABLE bool isWayland() const;
    Q_INVOKABLE bool nativeKeyHostReady(QQuickItem *hostItem) const;
    Q_INVOKABLE bool nativeKeyClick(QQuickItem *hostItem, int key) const;
    Q_INVOKABLE bool typeAscii(QQuickItem *hostItem, const QString &text) const;
    Q_INVOKABLE QStringList appearanceProperties(QObject *control) const;
    Q_INVOKABLE QVariantMap accessibleState(QQuickItem *item) const;
    Q_INVOKABLE bool accessibleAction(QQuickItem *item, const QString &action) const;
    // Capture tooling only: snapshots include the actual parent/window clipping.
    Q_INVOKABLE QString captureDirectory() const;
    Q_INVOKABLE bool createCaptureMode(const QString &mode) const;
    Q_INVOKABLE QString captureDataUrl(QQuickItem *item, qreal margin) const;
    Q_INVOKABLE bool writeCaptureManifest(const QString &text) const;
    Q_INVOKABLE quint64 colorPlaneRebuilds(QQuickItem *surface) const;
    Q_INVOKABLE QVariantMap colorPlaneCache(QQuickItem *surface) const;
    Q_INVOKABLE QColor paintedRasterPixel(QQuickItem *item, int x, int y, qreal dpr) const;
    Q_INVOKABLE qreal effectiveDpr(QQuickItem *item) const;
    Q_INVOKABLE bool touch(QQuickItem *item, const QString &phase, qreal x, qreal y);
    Q_INVOKABLE bool captureWindow(QQuickItem *item, const QString &name) const;
    Q_INVOKABLE QColor renderedPixel(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE int idleFrames(QQuickItem *item, int milliseconds) const;
private:
    QPointingDevice *m_touchDevice = nullptr;
public:
#ifdef IA_TEST_PLAYGROUND
    Q_INVOKABLE bool freshPaletteSamples();
    Q_INVOKABLE bool paletteSampleExists() const;
    Q_INVOKABLE QString paletteSampleBytes() const;
    Q_INVOKABLE QVariantList paletteSamples() const;
    Q_INVOKABLE bool paletteSampleLock(bool locked);
    Q_INVOKABLE bool writePaletteHistory(const QVariantList &accents, bool malformed = false);
    Q_INVOKABLE bool paletteHistoryReadable(bool readable);
private:
    QTemporaryDir m_samples;
    int m_sampleSequence = 0;
    std::unique_ptr<QLockFile> m_sampleLock;
#endif
};
