// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QVariantList>

namespace InterActive {

// GUI-thread observation of public Qt environment notifications. No theme discovery.
class FontEnvironment : public QObject
{
    Q_OBJECT
public:
    static FontEnvironment *shared();
    quint64 generation() const { return m_generation; }
signals:
    void invalidated();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    explicit FontEnvironment(QObject *parent);
    void observeScreens();
    void screenEnvironmentChanged();
    void invalidate();
    QVariantList screenEnvironment() const;
    quint64 m_generation = 1;
    QVariantList m_screens;
    QList<QMetaObject::Connection> m_screenConnections;
};

} // namespace InterActive
