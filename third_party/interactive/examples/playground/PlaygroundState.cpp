// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PlaygroundState.h"
#include "PaletteSampleHistory.h"
#include "AppearanceTraceController.h"
#include "AirmailProfiles.h"
#include "TokenSchema.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {
const std::array<QString, 2> semanticKeys{QStringLiteral("warning"), QStringLiteral("error")};

int semanticIndex(const QString &role)
{
    for (int i = 0; i < 2; ++i)
        if (role == semanticKeys[i])
            return i;
    return -1;
}

QVariantMap colorInputs(const QVariantMap &input)
{
    return input.value(QStringLiteral("tokens")).toMap().value(QStringLiteral("colors")).toMap();
}

void patchToken(QVariantMap &input, const QString &group, const QString &key, const QVariant &value,
                bool present)
{
    auto tokens = input.value(QStringLiteral("tokens")).toMap();
    auto values = tokens.value(group).toMap();
    if (present)
        values.insert(key, value);
    else
        values.remove(key);
    if (values.isEmpty())
        tokens.remove(group);
    else
        tokens.insert(group, values);
    if (tokens.isEmpty())
        input.remove(QStringLiteral("tokens"));
    else
        input.insert(QStringLiteral("tokens"), tokens);
}

QJsonObject sampleColor(const QColor &color)
{
    const auto rgba = color.toRgb().rgba64();
    return {{QStringLiteral("hexArgb"), color.name(QColor::HexArgb)},
            {QStringLiteral("rgba16"), QJsonArray{rgba.red(), rgba.green(), rgba.blue(), rgba.alpha()}}};
}
} // namespace

PlaygroundState *PlaygroundState::create(QQmlEngine *engine, QJSEngine *)
{
    auto *appearance = engine->singletonInstance<InterActive::Appearance *>(
        "InterActive.Foundation", "Appearance");
    return new PlaygroundState(appearance, engine);
}

PlaygroundState::PlaygroundState(InterActive::Appearance *appearance, QObject *parent)
    : QObject(parent), m_appearance(appearance), m_traceBridge(new AppearanceTraceBridge(this))
{
    Q_ASSERT(m_appearance);
    connect(m_appearance, &InterActive::Appearance::resolvedChanged, this,
            [this] { reconcileCorrections(true); });
    connect(m_appearance, &InterActive::Appearance::inputChanged, this, [this] {
        reconcileCorrections();
        emit appearanceInputChanged();
    });
    connect(m_appearance, &InterActive::Appearance::inputRejected, this,
            [this](const QVariantList &diagnostics) {
        if (m_activeTransaction)
            m_activeTransaction->rejected = true;
        QStringList messages;
        for (const QVariant &entry : diagnostics) {
            const auto diagnostic = entry.toMap();
            messages.append(diagnostic.value(QStringLiteral("path")).toString() + QStringLiteral(": ")
                            + diagnostic.value(QStringLiteral("message")).toString());
        }
        rejectAppearance(messages.join(QLatin1Char('\n')));
    });
}

AppearanceTraceBridge *PlaygroundState::traceBridge() const
{
    return m_traceBridge;
}

QVariantMap PlaygroundState::appearanceInput() const
{
    return m_appearance->input();
}

bool PlaygroundState::hasAccentOverride() const
{
    return hasColorOverride(QStringLiteral("accent"));
}

bool PlaygroundState::hasColorOverride(const QString &key) const
{
    return colorInputs(appearanceInput()).contains(key);
}

bool PlaygroundState::hasAmberOverride() const { return hasColorOverride(semanticKeys[0]); }
bool PlaygroundState::hasDangerOverride() const { return hasColorOverride(semanticKeys[1]); }

bool PlaygroundState::hasAmberCorrection() const
{
    const_cast<PlaygroundState *>(this)->reconcileCorrections();
    return m_corrections[0].owned;
}

bool PlaygroundState::hasDangerCorrection() const
{
    const_cast<PlaygroundState *>(this)->reconcileCorrections();
    return m_corrections[1].owned;
}

PlaygroundState::Corrections PlaygroundState::currentCorrections()
{
    reconcileCorrections();
    return m_corrections;
}

void PlaygroundState::installCorrections(quint64 id, const Corrections &corrections)
{
    if (id <= m_installedTransaction)
        return;
    const bool changed = m_corrections[0].owned != corrections[0].owned
        || m_corrections[1].owned != corrections[1].owned;
    m_installedTransaction = id;
    m_corrections = corrections;
    if (changed)
        emit correctionsChanged();
}

void PlaygroundState::reconcileCorrections(bool installedNotification)
{
    // Rejection diagnostics run before inputRejected with the OLD installed input.
    // Equality with a staged no-op is not installation evidence. A changed input
    // is observable even to an earlier source observer that calls these getters.
    const auto installed = appearanceInput();
    for (auto *transaction = m_activeTransaction; transaction; transaction = transaction->parent) {
        if (transaction->id <= m_installedTransaction)
            return;
        if (!transaction->rejected && transaction->candidate == installed
            && (installedNotification || transaction->before != installed)) {
            installCorrections(transaction->id, transaction->corrections);
            return;
        }
    }
}

bool PlaygroundState::submit(const QVariantMap &input, const Corrections &corrections)
{
    InterActive::TraceSpan span(InterActive::TraceScope::SetAppearance, this);
    Transaction transaction{++m_nextTransaction, input, appearanceInput(), corrections, m_activeTransaction};
    const quint64 errorRevision = m_appearanceErrorRevision;
    m_activeTransaction = &transaction;
    const QPointer<PlaygroundState> guard(this);
    m_appearance->setInput(input);
    if (!guard)
        return !transaction.rejected;
    // A newer nested accepted replacement wins, including accepted no-op input.
    if (!transaction.rejected && appearanceInput() == input)
        installCorrections(transaction.id, corrections);
    if (!guard)
        return !transaction.rejected;
    m_activeTransaction = transaction.parent;
    if (!transaction.rejected && transaction.id == m_installedTransaction
        && errorRevision == m_appearanceErrorRevision && !m_appearanceError.isEmpty()) {
        m_appearanceError.clear();
        emit appearanceErrorChanged();
    }
    return !transaction.rejected;
}

QColor PlaygroundState::effectiveAccent(const QVariantMap &input) const
{
    QVariant normalized;
    QVariantList diagnostics;
    const auto colors = colorInputs(input);
    if (colors.contains(QStringLiteral("accent"))
        && InterActive::TokenSchema::validate(QStringLiteral("colors"), QStringLiteral("accent"),
            colors.value(QStringLiteral("accent")), &normalized, &diagnostics))
        return normalized.value<QColor>().toRgb();
    return InterActive::AirmailProfiles::profile(input.value(QStringLiteral("scheme")).toString(), {})
        .value(QStringLiteral("colors")).toMap().value(QStringLiteral("accent")).value<QColor>().toRgb();
}

void PlaygroundState::restoreCorrections(QVariantMap &input, Corrections &corrections)
{
    for (int i = 0; i < 2; ++i) {
        const auto &record = corrections[i];
        if (record.owned)
            patchToken(input, QStringLiteral("colors"), semanticKeys[i], record.priorRawValue,
                       record.priorKeyPresent);
        corrections[i] = {};
    }
}

void PlaygroundState::clearForChangedAccent(QVariantMap &input, Corrections &corrections) const
{
    if (effectiveAccent(input) != m_appearance->colors()->accent().toRgb())
        restoreCorrections(input, corrections);
}

void PlaygroundState::rejectAppearance(const QString &message)
{
    ++m_appearanceErrorRevision;
    m_appearanceError = message;
    emit appearanceErrorChanged();
    emit appearanceRejected(message);
}

bool PlaygroundState::setAppearance(const QVariantMap &input)
{
    // The editor, presets, and automation all enter through this one writer.
    return submit(input, {});
}

bool PlaygroundState::setAppearanceJson(const QString &json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        rejectAppearance(error.error == QJsonParseError::NoError
            ? QStringLiteral("Appearance input must be a JSON object; previous input retained.")
            : QStringLiteral("Invalid JSON: %1; previous input retained.").arg(error.errorString()));
        return false;
    }
    return setAppearance(document.object().toVariantMap());
}

bool PlaygroundState::setAppearanceOption(const QString &key, const QVariant &value)
{
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    input.insert(key, value);
    if (key == QLatin1String("tokens"))
        corrections = {};
    else
        clearForChangedAccent(input, corrections);
    return submit(input, corrections);
}

bool PlaygroundState::setAppearanceToken(const QString &group, const QString &key, const QVariant &value)
{
    // The map-replacement preparation is measured separately from the
    // Foundation call inside setAppearance.
    InterActive::TraceSpan span(InterActive::TraceScope::SetAppearanceToken, this);
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    patchToken(input, group, key, value, true);
    if (group == QLatin1String("colors")) {
        const int index = semanticIndex(key);
        if (index >= 0)
            corrections[index] = {};
        if (key == QLatin1String("accent"))
            clearForChangedAccent(input, corrections);
    }
    return submit(input, corrections);
}

bool PlaygroundState::resetAppearanceToken(const QString &group, const QString &key)
{
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    patchToken(input, group, key, {}, false);
    if (group == QLatin1String("colors")) {
        const int index = semanticIndex(key);
        if (index >= 0)
            corrections[index] = {};
        if (key == QLatin1String("accent"))
            clearForChangedAccent(input, corrections);
    }
    return submit(input, corrections);
}

bool PlaygroundState::setSemanticCorrection(const QString &role, const QColor &color)
{
    const int index = semanticIndex(role);
    QVariant normalized;
    QVariantList diagnostics;
    if (index < 0 || !InterActive::TokenSchema::validate(QStringLiteral("colors"), role, color,
                                                       &normalized, &diagnostics))
        return false;
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    auto &record = corrections[index];
    if (!record.owned) {
        const auto colors = colorInputs(input);
        record = {true, colors.contains(role), colors.value(role)};
    }
    patchToken(input, QStringLiteral("colors"), role, normalized, true);
    return submit(input, corrections);
}

bool PlaygroundState::resetSemanticCorrection(const QString &role)
{
    const int index = semanticIndex(role);
    if (index < 0)
        return false;
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    const auto record = corrections[index];
    patchToken(input, QStringLiteral("colors"), role, record.priorRawValue,
               record.owned && record.priorKeyPresent);
    corrections[index] = {};
    return submit(input, corrections);
}

bool PlaygroundState::resetAccentForCalibration()
{
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    patchToken(input, QStringLiteral("colors"), QStringLiteral("accent"), {}, false);
    restoreCorrections(input, corrections);
    return submit(input, corrections);
}

bool PlaygroundState::applyInitialPaletteSuggestions()
{
    auto corrections = currentCorrections();
    auto input = appearanceInput();
    const auto colors = colorInputs(input);
    if (colors.contains(semanticKeys[0]) || colors.contains(semanticKeys[1])
        || m_appearance->colors()->accent().toRgb()
            != effectiveAccent({{QStringLiteral("scheme"), m_appearance->scheme()}}))
        return true;
    const std::array<QColor, 2> suggestions{QColor(QStringLiteral("#FF7A18")), QColor(QStringLiteral("#FF3434"))};
    for (int i = 0; i < 2; ++i) {
        corrections[i] = {true, false, {}};
        patchToken(input, QStringLiteral("colors"), semanticKeys[i], suggestions[i], true);
    }
    return submit(input, corrections);
}

void PlaygroundState::setPaletteSamplePath(const QString &filename)
{
    setPaletteSampleWriter(std::make_unique<PaletteSampleWriter>(filename));
}

void PlaygroundState::setPaletteSampleWriter(std::unique_ptr<PaletteSampleWriter> writer)
{
    m_sampleWriter = std::move(writer);
    refreshSavedAccents();
    emit paletteSamplePathChanged();
}

QString PlaygroundState::paletteSamplePath() const
{
    return m_sampleWriter ? m_sampleWriter->filename() : QString();
}

QString PlaygroundState::opaqueAccentKey(const QColor &color) const
{
    return PaletteSampleHistory::opaqueAccentKey(color);
}

void PlaygroundState::refreshSavedAccents()
{
    const auto result = PaletteSampleHistory(paletteSamplePath()).read();
    const bool keysChanged = m_savedAccentKeys != result.keys;
    const bool statusChanged = m_paletteHistoryStatus != result.status
        || m_paletteHistoryAvailable != result.available;
    // Install the whole read result before any observer sees its notification.
    m_savedAccentKeys = result.keys;
    m_paletteHistoryStatus = result.status;
    m_paletteHistoryAvailable = result.available;
    if (keysChanged)
        emit savedAccentKeysChanged();
    if (statusChanged)
        emit paletteHistoryStatusChanged();
}

bool PlaygroundState::savePaletteSample()
{
    const auto corrections = currentCorrections();
    const QColor accent = m_appearance->colors()->accent();
    const QColor amber = m_appearance->colors()->warning();
    const QColor danger = m_appearance->colors()->error();
    const QString utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QJsonObject sample{{QStringLiteral("version"), 1}, {QStringLiteral("utc"), utc},
        {QStringLiteral("scheme"), m_appearance->scheme()}, {QStringLiteral("contrast"), m_appearance->contrast()},
        {QStringLiteral("accent"), sampleColor(accent)}, {QStringLiteral("amber"), sampleColor(amber)},
        {QStringLiteral("danger"), sampleColor(danger)},
        {QStringLiteral("corrections"), QJsonObject{{QStringLiteral("amber"), corrections[0].owned},
                                                  {QStringLiteral("danger"), corrections[1].owned}}}};
    const auto result = m_sampleWriter
        ? m_sampleWriter->append(QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n')
        : PaletteSampleWriter::Result{false, false, QStringLiteral("No usable Documents sample destination configured.")};
    m_paletteSaveFailed = !result.success;
    m_paletteSaveStatus = result.success
        ? QStringLiteral("Last saved %1 — Accent %2, Amber %3, Danger %4 — %5")
              .arg(utc, accent.name(QColor::HexArgb), amber.name(QColor::HexArgb), danger.name(QColor::HexArgb), paletteSamplePath())
        : QStringLiteral("Save failed: %1%2").arg(result.error, result.uncertain
              ? QStringLiteral(" Some data may have been appended; retrying can duplicate it.") : QString());
    emit paletteSaveStatusChanged();
    refreshSavedAccents();
    return result.success;
}

void PlaygroundState::setDraftName(const QString &value)
{
    if (m_draft.name == value)
        return;
    m_draft.name = value;
    emit draftNameChanged();
    emit draftChanged();
}

void PlaygroundState::setDraftEmail(const QString &value)
{
    if (m_draft.email == value)
        return;
    m_draft.email = value;
    emit draftEmailChanged();
    emit draftChanged();
}

void PlaygroundState::setOptIn(bool value)
{
    if (m_draft.optIn == value)
        return;
    m_draft.optIn = value;
    emit optInChanged();
    emit draftChanged();
}

void PlaygroundState::setNotifications(bool value)
{
    if (m_draft.notifications == value)
        return;
    m_draft.notifications = value;
    emit notificationsChanged();
    emit draftChanged();
}

void PlaygroundState::setDelivery(const QString &value)
{
    if ((value != QLatin1String("daily") && value != QLatin1String("weekly")) || m_draft.delivery == value)
        return;
    m_draft.delivery = value;
    emit deliveryChanged();
    emit draftChanged();
}

bool PlaygroundState::dirty() const
{
    return m_draft.name != m_committed.name || m_draft.email != m_committed.email
        || m_draft.optIn != m_committed.optIn || m_draft.notifications != m_committed.notifications
        || m_draft.delivery != m_committed.delivery;
}

bool PlaygroundState::validDraft() const
{
    return !m_draft.name.trimmed().isEmpty() && m_draft.email.endsWith(QLatin1String("@example.test"))
        && m_draft.email.indexOf(QLatin1Char('@')) > 0;
}

QString PlaygroundState::committedSummary() const
{
    return QStringLiteral("%1\n%2\n%3 updates; notifications %4; research notes %5.")
        .arg(m_committed.name, m_committed.email, m_committed.delivery,
             m_committed.notifications ? QStringLiteral("on") : QStringLiteral("off"),
             m_committed.optIn ? QStringLiteral("included") : QStringLiteral("excluded"));
}

bool PlaygroundState::applyDraft()
{
    if (!validDraft())
        return false;
    m_committed = m_draft;
    emit committedChanged();
    emit draftChanged();
    return true;
}

void PlaygroundState::resetDraft()
{
    setDraftName(m_committed.name);
    setDraftEmail(m_committed.email);
    setOptIn(m_committed.optIn);
    setNotifications(m_committed.notifications);
    setDelivery(m_committed.delivery);
}

void PlaygroundState::setDialogName(const QString &value)
{
    if (m_dialogName == value)
        return;
    m_dialogName = value;
    emit dialogNameChanged();
    emit dialogChanged();
}

void PlaygroundState::setDialogEmail(const QString &value)
{
    if (m_dialogEmail == value)
        return;
    m_dialogEmail = value;
    emit dialogEmailChanged();
    emit dialogChanged();
}

bool PlaygroundState::validDialog() const
{
    return !m_dialogName.trimmed().isEmpty() && m_dialogEmail.endsWith(QLatin1String("@example.test"))
        && m_dialogEmail.indexOf(QLatin1Char('@')) > 0;
}

void PlaygroundState::beginDialog()
{
    setDialogName(m_draft.name);
    setDialogEmail(m_draft.email);
}

bool PlaygroundState::applyDialog()
{
    if (!validDialog())
        return false;
    setDraftName(m_dialogName);
    setDraftEmail(m_dialogEmail);
    return applyDraft();
}

void PlaygroundState::cancelDialog()
{
    // Dialog edits are a transaction, not live writes into the main form.
    beginDialog();
}

void PlaygroundState::setPreviewValue(qreal value)
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, qreal(0), qreal(100));
    if (m_previewValue == value)
        return;
    m_previewValue = value;
    emit previewValueChanged();
}

void PlaygroundState::setSelectedTab(int value)
{
    if (value < 0 || value > 1 || m_selectedTab == value)
        return;
    m_selectedTab = value;
    emit selectedTabChanged();
}

void PlaygroundState::setRowSelected(bool value)
{
    if (m_rowSelected == value)
        return;
    m_rowSelected = value;
    emit rowSelectedChanged();
}

void PlaygroundState::setPinned(bool value)
{
    if (m_pinned == value)
        return;
    m_pinned = value;
    emit pinnedChanged();
}

void PlaygroundState::setCollectionVisible(bool value)
{
    if (m_collectionVisible == value)
        return;
    m_collectionVisible = value;
    if (!value)
        setRowSelected(false);
    emit collectionVisibleChanged();
}

void PlaygroundState::resetFixtures()
{
    m_committed = Profile{};
    resetDraft();
    emit committedChanged();
    emit draftChanged();
    beginDialog();
    setPreviewValue(37);
    setSelectedTab(0);
    setRowSelected(false);
    setPinned(false);
    setCollectionVisible(true);
    setColorReference({});
}

void PlaygroundState::setColorReference(const QString &value)
{
    if (m_colorReference == value)
        return;
    m_colorReference = value;
    emit colorReferenceChanged();
}
