// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "Appearance.h"

// Test-only probes. Counting functions have no reactive counter property.
class DocumentProbe : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
public:
    explicit DocumentProbe(QObject *parent = nullptr) : QObject(parent) {}
    QQuickTextDocument *document() const { return m_wrapper; }
    void setDocument(QQuickTextDocument *document);
    Q_INVOKABLE QVariant countValue(const QVariant &value, const QString &label);
    Q_INVOKABLE int countFor(const QString &label) const { return m_counts.value(label); }
    Q_INVOKABLE QVariantMap counts() const;
    Q_INVOKABLE void resetCounts() { m_counts.clear(); }
    Q_INVOKABLE QString longText() const;
    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE InterActive::Appearance *createAppearance();
    Q_INVOKABLE bool destroyAppearance(InterActive::Appearance *appearance);
signals:
    void documentChanged();
private:
    void observeDocument();
    QPointer<QQuickTextDocument> m_wrapper;
    QPointer<QTextDocument> m_document;
    QList<QMetaObject::Connection> m_connections;
    QMetaObject::Connection m_wrapperConnection;
    QMap<QString, int> m_counts;
};
