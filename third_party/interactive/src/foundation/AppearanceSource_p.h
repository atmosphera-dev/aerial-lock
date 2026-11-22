// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "PaletteState_p.h"

namespace InterActive {

// Private, source-owned publication boundary. Public Appearance metadata never
// mentions PaletteUpdate; consumers keep the same QObject connection lifetime
// and synchronous dispatch semantics on this internal object instead.
class AppearanceSource : public QObject
{
    Q_OBJECT
public:
    explicit AppearanceSource(QObject *owner) : QObject(owner) {}
    static AppearanceSource *from(const QObject *owner)
    {
        return owner->findChild<AppearanceSource *>(Qt::FindDirectChildrenOnly);
    }
    const AppearanceStatePtr &state() const { return m_state; }
    void install(AppearanceStatePtr state) { m_state = std::move(state); }
    void publish(const PaletteUpdate &update) { emit completed(update); }
    PropertyNotifications notifications;
signals:
    void completed(const InterActive::PaletteUpdate &update);
private:
    AppearanceStatePtr m_state;
};

} // namespace InterActive
