// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "Appearance.h"
#include "Presentation.h"
#include "StateTable.h"
#include <QFont>
#include <QPointer>

namespace InterActive {

struct ControlAppearancePrivate;
struct PaletteUpdate;

class INTERACTIVE_FOUNDATION_EXPORT ControlAppearance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(InterActive::Appearance *source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY familyChanged FINAL)
    Q_PROPERTY(InterActive::Presentation::Tone tone READ tone WRITE setTone NOTIFY toneChanged FINAL)
    Q_PROPERTY(InterActive::Presentation::Validation validation READ validation WRITE setValidation NOTIFY validationChanged FINAL)
#define IA_LOCAL_PROPERTY(type, name, upper, group, token) \
    Q_PROPERTY(type name READ name WRITE set##upper RESET reset##upper NOTIFY name##Changed FINAL)
    IA_CONTROL_PROPERTIES(IA_LOCAL_PROPERTY)
#undef IA_LOCAL_PROPERTY
    Q_PROPERTY(QFont resolvedFont READ resolvedFont NOTIFY resolvedFontChanged FINAL)
    Q_PROPERTY(qreal headingPixelSize READ headingPixelSize NOTIFY headingPixelSizeChanged FINAL)
    Q_PROPERTY(int headingWeight READ headingWeight NOTIFY headingWeightChanged FINAL)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged FINAL)
    Q_PROPERTY(InterActive::StateTable stateTable READ stateTable NOTIFY stateTableChanged FINAL)
public:
    explicit ControlAppearance(QObject *parent = nullptr);
    ~ControlAppearance() override;
    Appearance *source() const { return m_source.data(); }
    void setSource(Appearance *source);
    QString family() const;
    void setFamily(const QString &family);
    Presentation::Tone tone() const { return m_tone; }
    void setTone(Presentation::Tone tone);
    Presentation::Validation validation() const { return m_validation; }
    void setValidation(Presentation::Validation validation);
#define IA_LOCAL_METHODS(type, name, upper, group, token) \
    type name() const; \
    void set##upper(const type &value); \
    void reset##upper();
    IA_CONTROL_PROPERTIES(IA_LOCAL_METHODS)
#undef IA_LOCAL_METHODS
    QFont resolvedFont() const { return m_font; }
    qreal headingPixelSize() const;
    int headingWeight() const;
    QVariantList diagnostics() const;
    StateTable stateTable() const;
    Q_INVOKABLE void resetOverrides();
signals:
    void resolvedChanged();
    void sourceChanged();
    void familyChanged();
    void toneChanged();
    void validationChanged();
    void resolvedFontChanged();
    void headingPixelSizeChanged();
    void headingWeightChanged();
    void diagnosticsChanged();
    void stateTableChanged();
#define IA_LOCAL_SIGNAL(type, name, upper, group, token) void name##Changed();
    IA_CONTROL_PROPERTIES(IA_LOCAL_SIGNAL)
#undef IA_LOCAL_SIGNAL
private:
    void resolve();
    void resolve(const PaletteUpdate &update, bool forceNoncolors = false);
    void resolveParts(bool colors, bool noncolors, bool selectors);
    void drainDiagnostics();
    void initializeNotifications();
    void installNotifications();
    void drainNotifications();
    QPointer<Appearance> m_source;
    QMetaObject::Connection m_sourceChanged;
    QMetaObject::Connection m_sourceDestroyed;
    std::unique_ptr<ControlAppearancePrivate> m_private;
    Presentation::Tone m_tone = Presentation::Auto;
    Presentation::Validation m_validation = Presentation::None;
    QFont m_font;
};

} // namespace InterActive
