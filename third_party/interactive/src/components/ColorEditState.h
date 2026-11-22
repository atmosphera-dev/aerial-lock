// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QColor>
#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace InterActive {
// Internal editor model. Display text is never the authority for an applied color.
class ColorEditState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor colorValue READ colorValue WRITE synchronize NOTIFY changed FINAL)
    Q_PROPERTY(QColor currentColor READ currentColor NOTIFY changed FINAL)
    Q_PROPERTY(double hue READ hue NOTIFY changed FINAL)
    Q_PROPERTY(double saturation READ saturation NOTIFY changed FINAL)
    Q_PROPERTY(double lightness READ lightness NOTIFY changed FINAL)
    Q_PROPERTY(double opacity READ opacity NOTIFY changed FINAL)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap fields READ fields NOTIFY changed FINAL)
    Q_PROPERTY(QString argb READ argb NOTIFY changed FINAL)
public:
    explicit ColorEditState(QObject *parent = nullptr);
    QColor colorValue() const { return m_authority; }
    QColor currentColor() const { return m_color; }
    double hue() const { return m_hue; }
    double saturation() const { return m_hslS; }
    double lightness() const { return m_hslL; }
    double opacity() const { return m_color.alphaF(); }
    int mode() const { return m_mode; }
    int revision() const { return m_revision; }
    QVariantMap fields() const { return m_fields; }
    QString argb() const { return m_color.name(QColor::HexArgb).toUpper(); }
    void synchronize(const QColor &value);
    void setMode(int mode);
    Q_INVOKABLE QString text(const QString &field) const;
    Q_INVOKABLE QString error(const QString &field) const;
    Q_INVOKABLE bool pending(const QString &field) const;
    Q_INVOKABLE void setDraft(const QString &field, const QString &text);
    Q_INVOKABLE bool commit(const QString &field);
    Q_INVOKABLE void editPlane(double saturation, double lightness);
    Q_INVOKABLE void editHue(double degrees);
    Q_INVOKABLE void editOpacity(double opacity);
signals:
    void changed();
    void colorEdited(const QColor &value);
private:
    struct Draft { QString text; QString error; };
    void notify();
    void adopt(const QColor &color);
    bool publish(const QColor &color);
    bool publishHsl(double hue, double saturation, double lightness);
    bool publishHsv(double hue, double saturation, double value);
    bool finishEdit(const QColor &proposal, bool changed);
    QColor m_authority = Qt::white;
    QColor m_color = Qt::white;
    double m_hue = 0;
    double m_hslS = 0;
    double m_hslL = 1;
    double m_hsvS = 0;
    double m_hsvV = 1;
    int m_mode = 0;
    int m_revision = 0;
    QHash<QString, Draft> m_drafts;
    QVariantMap m_fields;
};
} // namespace InterActive
