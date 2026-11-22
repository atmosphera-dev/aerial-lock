// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private Foundation state and finite ordinary-control projections.
#include "TokenDefinitions.h"
#include "TokenSchema.h"
#include "Presentation.h"
#include "StateTable.h"
#include <QMetaProperty>
#include <QObject>
#include <array>
#include <bitset>
#include <memory>
#include <map>
#include <optional>
#include <variant>

namespace InterActive {

enum class ColorRole {
#define IA_ROLE(name, light, dark) name,
    IA_COLOR_TOKENS(IA_ROLE)
#undef IA_ROLE
    Count
};
constexpr size_t ColorRoleCount = size_t(ColorRole::Count);
using ColorRoleMask = std::bitset<ColorRoleCount>;

enum class ControlColor {
#define IA_CONTROL_INDEX(type, name, upper, group, token) name,
    IA_CONTROL_COLOR_PROPERTIES(IA_CONTROL_INDEX)
    Count
};
constexpr size_t ControlColorCount = size_t(ControlColor::Count);
using ControlColorMapping = std::array<ColorRole, ControlColorCount>;
using ControlFindings = std::array<QVariantList, 6>;
inline constexpr std::array<std::pair<ControlColor, ControlColor>, 6> ControlDiagnosticPairs{{
    {ControlColor::foregroundColor, ControlColor::backgroundColor},
    {ControlColor::foregroundColor, ControlColor::hoverBackgroundColor},
    {ControlColor::foregroundColor, ControlColor::pressedBackgroundColor},
    {ControlColor::selectedForegroundColor, ControlColor::selectedBackgroundColor},
    {ControlColor::inactiveForegroundColor, ControlColor::inactiveBackgroundColor},
    {ControlColor::focusColor, ControlColor::backgroundColor}}};

enum class ControlNoncolor {
    IA_CONTROL_NONCOLOR_PROPERTIES(IA_CONTROL_INDEX)
    Count
};
constexpr size_t ControlNoncolorCount = size_t(ControlNoncolor::Count);
using ControlNoncolorValue = std::variant<qreal, QString, int>;
using ControlNoncolorValues = std::array<ControlNoncolorValue, ControlNoncolorCount>;

enum class ControlInput {
    IA_CONTROL_PROPERTIES(IA_CONTROL_INDEX)
    family, tone, validation, Count
};
#undef IA_CONTROL_INDEX

template<typename T, size_t N> struct SparseControlValues {
    using Value = T;
    std::array<T, N> values{};
    std::bitset<N> presence;
    template<typename Index> bool contains(Index index) const { return presence.test(size_t(index)); }
    template<typename Index> const T &value(Index index) const { return values[size_t(index)]; }
    template<typename Index> void set(Index index, const T &value)
    {
        values[size_t(index)] = value;
        presence.set(size_t(index));
    }
    template<typename Index> bool reset(Index index)
    {
        const bool removed = contains(index);
        presence.reset(size_t(index));
        return removed;
    }
    bool operator==(const SparseControlValues &other) const
    {
        if (presence != other.presence)
            return false;
        for (size_t i = 0; i < N; ++i) {
            if (presence.test(i) && values[i] != other.values[i])
                return false;
        }
        return true;
    }
};
using ControlColorValues = SparseControlValues<QColor, ControlColorCount>;
using ControlNoncolorOverrides = SparseControlValues<ControlNoncolorValue, ControlNoncolorCount>;
using ControlInputErrors = SparseControlValues<QVariantMap, size_t(ControlInput::Count)>;

enum class StateTableColor { Placeholder, Selection, SelectionText, Link, ChoiceTrack, ChoiceDisabledTrack, ChoiceGap,
    ValueRemainder, ScrollHover, ScrollPressed, MenuPopup, TabCanvas, Count };
using StateTableSharedColors = std::array<QColor, size_t(StateTableColor::Count)>;

enum class ControlFamily { Button, Tool, Field, Surface, Popup, Label, Delegate, Tab, Choice, Tooltip, MenuItem, TabBar,
    Slider, Progress, ScrollBar, Count };
constexpr size_t ControlFamilyCount = size_t(ControlFamily::Count);

struct StateTableRecipe {
    ControlFamily family = ControlFamily::Button;
    std::bitset<ControlColorCount> owned;
    qreal target = 5, hoverBlend = 0.08, pressedBlend = 0.16;
    QColor accent, onAccent;
    bool coupleBorder = false;
    bool operator==(const StateTableRecipe &other) const
    {
        return family == other.family && owned == other.owned && target == other.target
            && hoverBlend == other.hoverBlend && pressedBlend == other.pressedBlend
            && accent == other.accent && onAccent == other.onAccent && coupleBorder == other.coupleBorder;
    }
};

struct StateTableData {
    std::array<StateColors, size_t(Presentation::Disabled) + 1> states;
    // The unchanged facade properties and diagnostics share the same snapshot.
    ControlColorValues values;
    ControlColorValues sourceValues;
    StateTableRecipe recipe;
    StateTableSharedColors sharedColors;
    QColor backdrop;
    bool invalidFocus = false;
    std::shared_ptr<const ControlFindings> findings;
    std::shared_ptr<const ControlFindings> sourceFindings;
};

// Decode the schema boundary once per accepted snapshot, not once per facade.
struct MetricRecord {
#define IA_METRIC_FIELD(name, ...) qreal name = 0;
    IA_METRIC_TOKENS(IA_METRIC_FIELD)
#undef IA_METRIC_FIELD
    static MetricRecord from(const QVariantMap &values);
    bool operator==(const MetricRecord &other) const;
};

struct TypographyRecord {
    QString family;
#define IA_TYPE_SIZE_FIELD(name, ...) qreal name = 0;
    IA_TYPE_SIZE_TOKENS(IA_TYPE_SIZE_FIELD)
#undef IA_TYPE_SIZE_FIELD
#define IA_TYPE_WEIGHT_FIELD(name, ...) int name = 0;
    IA_TYPE_WEIGHT_TOKENS(IA_TYPE_WEIGHT_FIELD)
#undef IA_TYPE_WEIGHT_FIELD
    static TypographyRecord from(const QVariantMap &values);
    bool operator==(const TypographyRecord &other) const;
};

struct FamilyDescriptor {
    const char *name;
    ControlColorMapping colors;
    qreal MetricRecord::*height;
    qreal MetricRecord::*radius;
    qreal MetricRecord::*horizontalPadding;
    qreal MetricRecord::*verticalPadding;
    qreal TypographyRecord::*fontSize;
    bool interactive;
    bool semanticTextOnly;
    bool destructiveTextOnly = false;
    bool textContrast = true;
    bool usesSemanticText(Presentation::Tone tone) const
    {
        return semanticTextOnly || (destructiveTextOnly && tone == Presentation::Destructive);
    }
};
std::optional<ControlFamily> controlFamily(const QString &family);
const FamilyDescriptor &familyDescriptor(ControlFamily family);
const char *controlColorName(ControlColor color);
const QString &controlColorKey(ControlColor color);
ControlColorMapping controlColorMapping(ControlFamily family, Presentation::Tone tone,
                                       Presentation::Validation validation, bool dark = false);

struct PaletteInputs {
    QString scheme;
    QString contrast;
    QVariantMap colors;
    QSet<QString> presence;
    qreal hoverBlend = 0;
    qreal pressedBlend = 0;
    qreal semanticBlend = 0;
    static PaletteInputs from(const ResolvedAppearance &prepared);
    bool operator==(const PaletteInputs &other) const;
};

struct DiagnosticPair {
    ColorRole foreground;
    ColorRole background;
    bool focus = false;
    QVariantList findings;
};

struct PaletteState {
    std::array<QColor, ColorRoleCount> colors;
    ColorRoleMask presence;
    QVariantMap values;
    QVariantList diagnostics;
    QColor backdrop;
    bool dark = false;
    qreal target = 5; // Readability/diagnostic floor, independent of label derivation.
    qreal labelTarget = LabelContrastTarget;
    qreal hoverBlend = 0.08;
    qreal pressedBlend = 0.16;
    QList<DiagnosticPair> pairs;

    QColor color(ColorRole role) const { return colors[size_t(role)]; }
    bool explicitRole(ColorRole role) const { return presence.test(size_t(role)); }
    QVariantList pairFindings(ColorRole foreground, ColorRole background,
                              const QString &path, bool focus = false) const;
    std::shared_ptr<const ControlFindings> controlFindings(const ControlColorMapping &mapping) const;
    std::shared_ptr<const StateTableData> stateTable(ControlFamily family, Presentation::Tone tone,
        Presentation::Validation validation) const;
    static std::shared_ptr<const PaletteState> calculate(const ResolvedAppearance &prepared,
        const std::shared_ptr<const PaletteState> &previous, const void *owner, quint64 revision);
    static const std::shared_ptr<const PaletteState> &fallback();
private:
    // GUI-thread-only lazy memoization within this immutable palette. Auto and
    // Neutral share a slot; only combinations actually requested are built.
    mutable std::array<std::shared_ptr<const StateTableData>, ControlFamilyCount * 4 * 3> m_stateTables;
    mutable std::map<ControlColorMapping, std::shared_ptr<const ControlFindings>> m_controlFindings;
};

struct AppearanceState {
    quint64 revision = 0;
    ResolvedAppearance snapshot;
    std::shared_ptr<const PaletteState> palette;
    MetricRecord metrics;
    TypographyRecord typography;
    void installNoncolors();
};
using AppearanceStatePtr = std::shared_ptr<const AppearanceState>;

enum NoncolorGroup : quint8 { MetricsGroup = 1, TypographyGroup = 2, MotionGroup = 4, IconsGroup = 8 };

struct PaletteUpdate {
    AppearanceStatePtr before;
    AppearanceStatePtr after;
    ColorRoleMask changedRoles;
    ColorRoleMask changedPresence;
    quint8 changedGroups = 0;
    static PaletteUpdate between(const AppearanceStatePtr &before, const AppearanceStatePtr &after);
};

// Same-stack notification obligations, separate from calculation/publication.
// Nested installation compares with last-notified values, not an obsolete delta.
struct PropertyNotifications {
    struct Property { QString name; QMetaProperty metadata; };
    QVariantMap last;
    QSet<QString> pending;
    QStringList properties;
    QList<Property> metadata;
    bool draining = false;
    void initialize(QObject *object, const QStringList &names = {});
    void installed(QObject *object);
    void drain(QObject *object);
};

} // namespace InterActive
