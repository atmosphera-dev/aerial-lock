// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TokenValidation_p.h"
#include "ColorMath_p.h"
#include "TokenDefinitions.h"
#include <QMetaType>
#include <QStringList>
#include <QUrl>
#include <cmath>

namespace InterActive {
namespace {

bool numeric(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Long:
    case QMetaType::ULong:
        return true;
    default:
        return false;
    }
}

bool numberInRange(const QVariant &value, qreal low, qreal high, bool integer = false)
{
    if (!numeric(value))
        return false;
    const double n = value.toDouble();
    return std::isfinite(n) && n >= low && n <= high && (!integer || n == std::floor(n));
}

QVariantMap definition(const QString &kind, qreal minimum = 0, qreal maximum = 0)
{
    return {{QStringLiteral("type"), kind}, {QStringLiteral("minimum"), minimum},
            {QStringLiteral("maximum"), maximum}};
}

} // namespace

QVariantMap TokenValidation::definitions()
{
    static const QVariantMap schema = [] {
        QVariantMap colors, metrics, typography, motion, icons;
#define IA_SCHEMA_COLOR(name, light, dark) colors.insert(QStringLiteral(#name), definition(QStringLiteral("color")));
        IA_COLOR_TOKENS(IA_SCHEMA_COLOR)
#undef IA_SCHEMA_COLOR
#define IA_SCHEMA_METRIC(name, value, min, max) metrics.insert(QStringLiteral(#name), definition(QStringLiteral("real"), min, max));
        IA_METRIC_TOKENS(IA_SCHEMA_METRIC)
#undef IA_SCHEMA_METRIC
        typography.insert(QStringLiteral("family"), definition(QStringLiteral("family")));
#define IA_SCHEMA_SIZE(name, value, min, max) typography.insert(QStringLiteral(#name), definition(QStringLiteral("real"), min, max));
        IA_TYPE_SIZE_TOKENS(IA_SCHEMA_SIZE)
#undef IA_SCHEMA_SIZE
#define IA_SCHEMA_WEIGHT(name, value, min, max) typography.insert(QStringLiteral(#name), definition(QStringLiteral("integer"), min, max));
        IA_TYPE_WEIGHT_TOKENS(IA_SCHEMA_WEIGHT)
#undef IA_SCHEMA_WEIGHT
#define IA_SCHEMA_MOTION_INT(name, value, min, max) motion.insert(QStringLiteral(#name), definition(QStringLiteral("integer"), min, max));
        IA_MOTION_INT_TOKENS(IA_SCHEMA_MOTION_INT)
#undef IA_SCHEMA_MOTION_INT
#define IA_SCHEMA_MOTION_REAL(name, value, min, max) motion.insert(QStringLiteral(#name), definition(QStringLiteral("real"), min, max));
        IA_MOTION_REAL_TOKENS(IA_SCHEMA_MOTION_REAL)
#undef IA_SCHEMA_MOTION_REAL
        icons.insert(QStringLiteral("strokeWidth"), definition(QStringLiteral("real"), 0.25, 16));
#define IA_SCHEMA_URL(name, file) icons.insert(QStringLiteral(#name), definition(QStringLiteral("url")));
        IA_ICON_URL_TOKENS(IA_SCHEMA_URL)
#undef IA_SCHEMA_URL
        return QVariantMap{{QStringLiteral("colors"), colors}, {QStringLiteral("metrics"), metrics},
                           {QStringLiteral("typography"), typography}, {QStringLiteral("motion"), motion},
                           {QStringLiteral("icons"), icons}};
    }();
    return schema;
}

QVariantMap TokenValidation::diagnostic(const QString &path, const QString &code,
                                  const QString &message, const QString &severity)
{
    return {{QStringLiteral("path"), path}, {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}, {QStringLiteral("severity"), severity}};
}

bool TokenValidation::validate(const QString &group, const QString &name, const QVariant &value,
                           QVariant *normalized, QVariantList *diagnostics, const QString &path, bool resolvedValue)
{
    const QString location = path.isEmpty() ? QStringLiteral("tokens.") + group + QLatin1Char('.') + name : path;
    const auto entry = definitions().value(group).toMap().value(name).toMap();
    const auto invalid = [&](const QString &code, const QString &message) {
        if (diagnostics)
            diagnostics->append(diagnostic(location, code, message));
        return false;
    };
    if (entry.isEmpty())
        return invalid(QStringLiteral("unknown-field"), QStringLiteral("Unknown token; ignored."));
    const QString type = entry.value(QStringLiteral("type")).toString();
    QVariant result;
    if (type == QLatin1String("color")) {
        QColor color;
        if (value.metaType().id() == QMetaType::QColor)
            color = value.value<QColor>();
        else if (value.metaType().id() == QMetaType::QString)
            color = QColor(value.toString());
        if (!color.isValid())
            return invalid(QStringLiteral("invalid-value"), QStringLiteral("Expected a valid QColor or color string."));
        if (!std::isfinite(color.redF()) || !std::isfinite(color.greenF())
            || !std::isfinite(color.blueF()) || !std::isfinite(color.alphaF())
            || color.redF() < 0 || color.redF() > 1 || color.greenF() < 0 || color.greenF() > 1
            || color.blueF() < 0 || color.blueF() > 1 || color.alphaF() < 0 || color.alphaF() > 1)
            return invalid(QStringLiteral("invalid-value"), QStringLiteral("Color channels must be finite and within [0, 1]."));
        result = color.toRgb();
    } else if (type == QLatin1String("real") || type == QLatin1String("integer")) {
        const bool integer = type == QLatin1String("integer");
        double low = entry.value(QStringLiteral("minimum")).toDouble();
        double high = entry.value(QStringLiteral("maximum")).toDouble();
        // Local values are already scaled. Their accepted range must include
        // every effective default, including a same-as-default assignment.
        if (resolvedValue && group == QLatin1String("typography") && name == QLatin1String("controlSize")) {
            low *= 0.5;
            high *= 3;
        }
        if (resolvedValue && group == QLatin1String("metrics") && name == QLatin1String("controlHeight"))
            high = 4096;
        if (!numberInRange(value, low, high, integer))
            return invalid(QStringLiteral("invalid-value"),
                           QStringLiteral("Expected a finite %1 in [%2, %3]; strings and booleans are not numbers.")
                               .arg(type).arg(low).arg(high));
        result = integer ? QVariant(value.toInt()) : QVariant(value.toDouble());
    } else if (type == QLatin1String("family")) {
        if (value.metaType().id() != QMetaType::QString || value.toString().trimmed().isEmpty()
            || value.toString().size() > 256 || value.toString().contains(QChar::Null))
            return invalid(QStringLiteral("invalid-value"), QStringLiteral("Expected a nonempty font family (at most 256 characters)."));
        result = value.toString().trimmed();
    } else if (type == QLatin1String("url")) {
        if (value.metaType().id() != QMetaType::QString && value.metaType().id() != QMetaType::QUrl)
            return invalid(QStringLiteral("invalid-value"), QStringLiteral("Expected an absolute asset URL."));
        const QUrl url = value.metaType().id() == QMetaType::QUrl ? value.toUrl() : QUrl(value.toString(), QUrl::StrictMode);
        if (!url.isValid() || url.isEmpty() || url.isRelative()
            || !QStringList{QStringLiteral("qrc"), QStringLiteral("file"), QStringLiteral("https"),
                            QStringLiteral("http"), QStringLiteral("data")}.contains(url.scheme()))
            return invalid(QStringLiteral("invalid-value"), QStringLiteral("Asset URL must use qrc, file, https, http, or data."));
        result = url;
    }
    if (normalized)
        *normalized = result;
    return true;
}

void TokenValidation::checkContrast(QVariantList *diagnostics, const QString &path,
                                const QColor &foreground, const QColor &background, qreal target)
{
    const qreal actual = ColorMath::contrastRatio(foreground, background);
    if (actual + 0.001 >= target)
        return;
    auto entry = diagnostic(path, QStringLiteral("low-contrast"),
                            QStringLiteral("Foreground contrast %1:1 is below the %2:1 target; supplied colors are retained and derived text is best-effort.")
                                .arg(actual, 0, 'f', 2).arg(target));
    entry.insert(QStringLiteral("ratio"), actual);
    entry.insert(QStringLiteral("target"), target);
    diagnostics->append(entry);
}

ResolvedAppearance TokenValidation::parseEnvelope(const QVariantMap &input)
{
    ResolvedAppearance result;
    const auto reject = [&](const QString &path, const QString &message) {
        result.accepted = false;
        result.diagnostics.append(diagnostic(path, QStringLiteral("rejected-input"), message, QStringLiteral("error")));
    };
    if (input.contains(QStringLiteral("schemaVersion"))
        && !numberInRange(input.value(QStringLiteral("schemaVersion")), 1, 1, true)) {
        reject(QStringLiteral("schemaVersion"), QStringLiteral("Only numeric schemaVersion 1 is supported; previous snapshot retained."));
        return result;
    }
    const QVariant tokenInput = input.value(QStringLiteral("tokens"));
    if (input.contains(QStringLiteral("tokens")) && tokenInput.metaType().id() != QMetaType::QVariantMap) {
        reject(QStringLiteral("tokens"), QStringLiteral("tokens must be an object; previous snapshot retained."));
        return result;
    }
    const auto schema = definitions();
    const auto suppliedGroups = tokenInput.toMap();
    for (auto it = suppliedGroups.cbegin(); it != suppliedGroups.cend(); ++it) {
        if (schema.contains(it.key()) && it.value().metaType().id() != QMetaType::QVariantMap) {
            reject(QStringLiteral("tokens.") + it.key(), QStringLiteral("A token group must be an object; previous snapshot retained."));
        }
    }
    if (!result.accepted)
        return result;

    const QSet<QString> envelope{QStringLiteral("schemaVersion"), QStringLiteral("scheme"), QStringLiteral("density"),
                                QStringLiteral("fontScale"), QStringLiteral("contrast"), QStringLiteral("reducedMotion"),
                                QStringLiteral("tokens")};
    for (auto it = input.cbegin(); it != input.cend(); ++it) {
        if (!envelope.contains(it.key()))
            result.diagnostics.append(diagnostic(it.key(), QStringLiteral("unknown-field"), QStringLiteral("Unknown input field; ignored.")));
    }
    const auto readEnum = [&](const QString &key, const QStringList &allowed, QString *destination) {
        if (!input.contains(key))
            return;
        const auto value = input.value(key);
        if (value.metaType().id() == QMetaType::QString && allowed.contains(value.toString()))
            *destination = value.toString();
        else
            result.diagnostics.append(diagnostic(key, QStringLiteral("invalid-value"), QStringLiteral("Expected one of: ") + allowed.join(QStringLiteral(", "))));
    };
    readEnum(QStringLiteral("scheme"), {QStringLiteral("light"), QStringLiteral("dark")}, &result.scheme);
    readEnum(QStringLiteral("density"), {QStringLiteral("compact"), QStringLiteral("touch")}, &result.density);
    readEnum(QStringLiteral("contrast"), {QStringLiteral("normal"), QStringLiteral("high")}, &result.contrast);
    if (input.contains(QStringLiteral("fontScale"))) {
        if (numberInRange(input.value(QStringLiteral("fontScale")), 0.5, 3))
            result.fontScale = input.value(QStringLiteral("fontScale")).toDouble();
        else
            result.diagnostics.append(diagnostic(QStringLiteral("fontScale"), QStringLiteral("invalid-value"), QStringLiteral("Expected a finite number in [0.5, 3].")));
    }
    if (input.contains(QStringLiteral("reducedMotion"))) {
        if (input.value(QStringLiteral("reducedMotion")).metaType().id() == QMetaType::Bool)
            result.reducedMotion = input.value(QStringLiteral("reducedMotion")).toBool();
        else
            result.diagnostics.append(diagnostic(QStringLiteral("reducedMotion"), QStringLiteral("invalid-value"), QStringLiteral("Expected a boolean.")));
    }
    return result;
}

void TokenValidation::applyTokens(ResolvedAppearance &result, const QVariantMap &suppliedGroups)
{
    const auto schema = definitions();
    for (auto it = suppliedGroups.cbegin(); it != suppliedGroups.cend(); ++it) {
        if (!schema.contains(it.key())) {
            result.diagnostics.append(diagnostic(QStringLiteral("tokens.") + it.key(), QStringLiteral("unknown-field"), QStringLiteral("Unknown token group; ignored.")));
            continue;
        }
        auto group = result.tokens.value(it.key()).toMap();
        const auto values = it.value().toMap();
        for (auto field = values.cbegin(); field != values.cend(); ++field) {
            QVariant normalized;
            if (validate(it.key(), field.key(), field.value(), &normalized, &result.diagnostics)) {
                group.insert(field.key(), normalized);
                result.explicitTokens.insert(it.key() + QLatin1Char('.') + field.key());
            }
        }
        result.tokens.insert(it.key(), group);
    }
}

} // namespace InterActive
