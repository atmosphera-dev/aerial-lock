// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ColorEditState.h"
#include <QRegularExpression>
#include <cmath>

namespace InterActive {
namespace {
bool sameRgba(const QColor &a, const QColor &b)
{
    return a == b || (a.redF() == b.redF() && a.greenF() == b.greenF()
        && a.blueF() == b.blueF() && a.alphaF() == b.alphaF());
}
QString number(double value) { return QString::number(value, 'f', 1).remove(QRegularExpression("\\.0$")); }
bool unit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}
ColorEditState::ColorEditState(QObject *parent) : QObject(parent) { notify(); }
void ColorEditState::notify()
{
    QVariantMap fields;
    for (const auto &key : {"hex", "hexAlpha", "rgbRed", "rgbGreen", "rgbBlue", "rgbAlpha",
                           "hsvHue", "hsvSaturation", "hsvValue", "hsvAlpha",
                           "hslHue", "hslSaturation", "hslLightness", "hslAlpha"}) {
        fields.insert(key, QVariantMap{{"text", text(key)}, {"error", error(key)}, {"pending", pending(key)}});
    }
    m_fields = fields;
    ++m_revision;
    emit changed();
}
void ColorEditState::adopt(const QColor &color)
{
    m_color = color;
    const auto hsl = color.toHsl();
    const auto hsv = color.toHsv();
    if (color.spec() == QColor::Hsv && color.hsvHueF() >= 0)
        m_hue = color.hsvHueF() * 360;
    else if (hsl.hslHueF() >= 0)
        m_hue = hsl.hslHueF() * 360;
    m_hslS = hsl.hslSaturationF();
    m_hslL = hsl.lightnessF();
    m_hsvS = hsv.hsvSaturationF();
    m_hsvV = hsv.valueF();
}
void ColorEditState::synchronize(const QColor &value)
{
    if (!value.isValid()) return;
    m_authority = value;
    if (sameRgba(m_color, value)) return;
    adopt(value);
    notify();
}
void ColorEditState::setMode(int mode)
{
    if (mode < 0 || mode > 3 || mode == m_mode) return;
    m_mode = mode;
    notify();
}
QString ColorEditState::text(const QString &field) const
{
    const auto draft = m_drafts.constFind(field);
    if (draft != m_drafts.cend()) return draft->text;
    if (field == "hex") return m_color.name(QColor::HexRgb).toUpper();
    if (field.endsWith("Alpha")) return number(opacity() * 100);
    if (field == "rgbRed") return QString::number(m_color.red());
    if (field == "rgbGreen") return QString::number(m_color.green());
    if (field == "rgbBlue") return QString::number(m_color.blue());
    if (field.endsWith("Hue")) return number(m_hue);
    if (field == "hslSaturation") return number(m_hslS * 100);
    if (field == "hslLightness") return number(m_hslL * 100);
    if (field == "hsvSaturation") return number(m_hsvS * 100);
    if (field == "hsvValue") return number(m_hsvV * 100);
    return {};
}
QString ColorEditState::error(const QString &field) const { return m_drafts.value(field).error; }
bool ColorEditState::pending(const QString &field) const { return m_drafts.contains(field); }
void ColorEditState::setDraft(const QString &field, const QString &value)
{
    if (pending(field) && m_drafts.value(field).text == value) return;
    m_drafts.insert(field, {value, {}});
    notify();
}
bool ColorEditState::commit(const QString &field)
{
    if (!pending(field)) return true;
    const QString input = m_drafts.value(field).text.trimmed();
    QColor value = m_color;
    bool valid = false;
    double channel = 0;
    QString message;
    if (field == "hex") {
        static const QRegularExpression hex("^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
        valid = hex.match(input).hasMatch();
        if (valid) value = QColor::fromString(input);
        message = "Use #RRGGBB or #AARRGGBB.";
    } else {
        bool parsed = false;
        channel = input.toDouble(&parsed);
        const bool rgb = field.startsWith("rgb") && !field.endsWith("Alpha");
        const double maximum = rgb ? 255 : field.endsWith("Hue") ? 360 : 100;
        valid = parsed && std::isfinite(channel) && channel >= 0 && channel <= maximum
            && (!rgb || std::floor(channel) == channel);
        message = rgb ? "0-255, integer"
            : field.endsWith("Hue") ? "0-360 deg" : "0-100%";
        if (valid) {
            if (field == "rgbRed") value.setRedF(channel / 255);
            else if (field == "rgbGreen") value.setGreenF(channel / 255);
            else if (field == "rgbBlue") value.setBlueF(channel / 255);
            else if (field.endsWith("Alpha")) value.setAlphaF(channel / 100);
            else if (field != "hslHue" && field != "hslSaturation" && field != "hslLightness"
                     && field != "hsvHue" && field != "hsvSaturation" && field != "hsvValue") valid = false;
        }
    }
    if (!valid) { m_drafts[field].error = message; notify(); return false; }
    m_drafts.remove(field);
    bool applied = true;
    if (field.startsWith("hsl") && !field.endsWith("Alpha"))
        applied = publishHsl(field.endsWith("Hue") ? channel : m_hue,
                   field.endsWith("Saturation") ? channel / 100 : m_hslS,
                   field.endsWith("Lightness") ? channel / 100 : m_hslL);
    else if (field.startsWith("hsv") && !field.endsWith("Alpha"))
        applied = publishHsv(field.endsWith("Hue") ? channel : m_hue,
                   field.endsWith("Saturation") ? channel / 100 : m_hsvS,
                   field.endsWith("Value") ? channel / 100 : m_hsvV);
    else if (field.endsWith("Alpha")) {
        const bool changed = value != m_color;
        m_color = value;
        applied = finishEdit(value, changed);
    } else applied = publish(value);
    if (!applied) {
        m_drafts.insert(field, {input, {}});
        notify();
    }
    return true;
}
bool ColorEditState::finishEdit(const QColor &proposal, bool changed)
{
    notify();
    if (changed) emit colorEdited(proposal);
    // A controlled editor cannot label a rejected/hidden request as applied.
    const bool accepted = sameRgba(m_authority, proposal);
    if (!sameRgba(m_color, m_authority)) {
        adopt(m_authority);
        notify();
    }
    return accepted;
}
bool ColorEditState::publish(const QColor &value)
{
    const bool changed = value != m_color;
    adopt(value);
    return finishEdit(value, changed);
}
bool ColorEditState::publishHsl(double hue, double saturation, double lightness)
{
    const QColor value = QColor::fromHslF(hue == 360 ? 0 : hue / 360, saturation, lightness, opacity());
    const bool changed = value != m_color;
    adopt(value);
    m_hue = hue; m_hslS = saturation; m_hslL = lightness;
    return finishEdit(value, changed);
}
bool ColorEditState::publishHsv(double hue, double saturation, double brightness)
{
    const QColor value = QColor::fromHsvF(hue == 360 ? 0 : hue / 360, saturation, brightness, opacity());
    const bool changed = value != m_color;
    adopt(value);
    m_hue = hue; m_hsvS = saturation; m_hsvV = brightness;
    return finishEdit(value, changed);
}
void ColorEditState::editPlane(double saturation, double lightness)
{
    if (unit(saturation) && unit(lightness)) publishHsl(m_hue, saturation, lightness);
}
void ColorEditState::editHue(double degrees)
{
    if (!std::isfinite(degrees) || degrees < 0 || degrees > 360) return;
    if (m_mode == 2) publishHsv(degrees, m_hsvS, m_hsvV);
    else publishHsl(degrees, m_hslS, m_hslL);
}
void ColorEditState::editOpacity(double alpha)
{
    if (!unit(alpha) || opacity() == alpha) return;
    QColor value = m_color;
    value.setAlphaF(alpha);
    if (value == m_color) return;
    m_color = value;
    finishEdit(value, true);
}
} // namespace InterActive
