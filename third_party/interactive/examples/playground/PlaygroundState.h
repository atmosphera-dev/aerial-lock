// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "Appearance.h"
#include "PaletteSampleWriter.h"
#include "interactive_playground_scene_export.h"
#include <QtQml/qqmlregistration.h>
#include <array>
#include <memory>

class AppearanceTraceBridge;
class QQmlEngine;
class QJSEngine;

class INTERACTIVE_PLAYGROUND_SCENE_EXPORT PlaygroundState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap appearanceInput READ appearanceInput NOTIFY appearanceInputChanged FINAL)
    Q_PROPERTY(bool hasAccentOverride READ hasAccentOverride NOTIFY appearanceInputChanged FINAL)
    Q_PROPERTY(bool hasAmberOverride READ hasAmberOverride NOTIFY appearanceInputChanged FINAL)
    Q_PROPERTY(bool hasDangerOverride READ hasDangerOverride NOTIFY appearanceInputChanged FINAL)
    Q_PROPERTY(bool hasAmberCorrection READ hasAmberCorrection NOTIFY correctionsChanged FINAL)
    Q_PROPERTY(bool hasDangerCorrection READ hasDangerCorrection NOTIFY correctionsChanged FINAL)
    Q_PROPERTY(QString paletteSamplePath READ paletteSamplePath NOTIFY paletteSamplePathChanged FINAL)
    Q_PROPERTY(QString paletteSaveStatus READ paletteSaveStatus NOTIFY paletteSaveStatusChanged FINAL)
    Q_PROPERTY(bool paletteSaveFailed READ paletteSaveFailed NOTIFY paletteSaveStatusChanged FINAL)
    Q_PROPERTY(QStringList savedAccentKeys READ savedAccentKeys NOTIFY savedAccentKeysChanged FINAL)
    Q_PROPERTY(QString paletteHistoryStatus READ paletteHistoryStatus NOTIFY paletteHistoryStatusChanged FINAL)
    Q_PROPERTY(bool paletteHistoryAvailable READ paletteHistoryAvailable NOTIFY paletteHistoryStatusChanged FINAL)
    Q_PROPERTY(QString appearanceError READ appearanceError NOTIFY appearanceErrorChanged FINAL)
    Q_PROPERTY(QString draftName READ draftName WRITE setDraftName NOTIFY draftNameChanged FINAL)
    Q_PROPERTY(QString draftEmail READ draftEmail WRITE setDraftEmail NOTIFY draftEmailChanged FINAL)
    Q_PROPERTY(bool optIn READ optIn WRITE setOptIn NOTIFY optInChanged FINAL)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY notificationsChanged FINAL)
    Q_PROPERTY(QString delivery READ delivery WRITE setDelivery NOTIFY deliveryChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged FINAL)
    Q_PROPERTY(bool validDraft READ validDraft NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString committedSummary READ committedSummary NOTIFY committedChanged FINAL)
    Q_PROPERTY(QString dialogName READ dialogName WRITE setDialogName NOTIFY dialogNameChanged FINAL)
    Q_PROPERTY(QString dialogEmail READ dialogEmail WRITE setDialogEmail NOTIFY dialogEmailChanged FINAL)
    Q_PROPERTY(bool validDialog READ validDialog NOTIFY dialogChanged FINAL)
    Q_PROPERTY(qreal previewValue READ previewValue WRITE setPreviewValue NOTIFY previewValueChanged FINAL)
    Q_PROPERTY(int selectedTab READ selectedTab WRITE setSelectedTab NOTIFY selectedTabChanged FINAL)
    Q_PROPERTY(bool rowSelected READ rowSelected WRITE setRowSelected NOTIFY rowSelectedChanged FINAL)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged FINAL)
    Q_PROPERTY(bool collectionVisible READ collectionVisible WRITE setCollectionVisible NOTIFY collectionVisibleChanged FINAL)
    Q_PROPERTY(QString colorReference READ colorReference WRITE setColorReference NOTIFY colorReferenceChanged FINAL)
    Q_PROPERTY(AppearanceTraceBridge *traceBridge READ traceBridge CONSTANT FINAL)

public:
    static PlaygroundState *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    explicit PlaygroundState(InterActive::Appearance *appearance, QObject *parent = nullptr);

    QVariantMap appearanceInput() const;
    bool hasAccentOverride() const;
    bool hasAmberOverride() const;
    bool hasDangerOverride() const;
    bool hasAmberCorrection() const;
    bool hasDangerCorrection() const;
    QString appearanceError() const { return m_appearanceError; }
    Q_INVOKABLE bool setAppearance(const QVariantMap &input);
    Q_INVOKABLE bool setAppearanceJson(const QString &json);
    Q_INVOKABLE bool setAppearanceOption(const QString &key, const QVariant &value);
    Q_INVOKABLE bool setAppearanceToken(const QString &group, const QString &key, const QVariant &value);
    Q_INVOKABLE bool resetAppearanceToken(const QString &group, const QString &key);
    Q_INVOKABLE bool setSemanticCorrection(const QString &role, const QColor &color);
    Q_INVOKABLE bool resetSemanticCorrection(const QString &role);
    Q_INVOKABLE bool resetAccentForCalibration();
    Q_INVOKABLE bool applyInitialPaletteSuggestions();
    void setPaletteSamplePath(const QString &filename);
    // Test injection uses the same writer and state path; no arbitrary-file QML API.
    void setPaletteSampleWriter(std::unique_ptr<PaletteSampleWriter> writer);
    QString paletteSamplePath() const;
    QString paletteSaveStatus() const { return m_paletteSaveStatus; }
    bool paletteSaveFailed() const { return m_paletteSaveFailed; }
    Q_INVOKABLE bool savePaletteSample();
    QStringList savedAccentKeys() const { return m_savedAccentKeys; }
    QString paletteHistoryStatus() const { return m_paletteHistoryStatus; }
    bool paletteHistoryAvailable() const { return m_paletteHistoryAvailable; }
    Q_INVOKABLE void refreshSavedAccents();
    Q_INVOKABLE QString opaqueAccentKey(const QColor &color) const;

    QString draftName() const { return m_draft.name; }
    QString draftEmail() const { return m_draft.email; }
    bool optIn() const { return m_draft.optIn; }
    bool notifications() const { return m_draft.notifications; }
    QString delivery() const { return m_draft.delivery; }
    void setDraftName(const QString &value);
    void setDraftEmail(const QString &value);
    void setOptIn(bool value);
    void setNotifications(bool value);
    void setDelivery(const QString &value);
    bool dirty() const;
    bool validDraft() const;
    QString committedSummary() const;
    Q_INVOKABLE bool applyDraft();
    Q_INVOKABLE void resetDraft();

    QString dialogName() const { return m_dialogName; }
    QString dialogEmail() const { return m_dialogEmail; }
    void setDialogName(const QString &value);
    void setDialogEmail(const QString &value);
    bool validDialog() const;
    Q_INVOKABLE void beginDialog();
    Q_INVOKABLE bool applyDialog();
    Q_INVOKABLE void cancelDialog();

    qreal previewValue() const { return m_previewValue; }
    int selectedTab() const { return m_selectedTab; }
    bool rowSelected() const { return m_rowSelected; }
    bool pinned() const { return m_pinned; }
    bool collectionVisible() const { return m_collectionVisible; }
    QString colorReference() const { return m_colorReference; }
    void setPreviewValue(qreal value);
    void setSelectedTab(int value);
    void setRowSelected(bool value);
    void setPinned(bool value);
    void setCollectionVisible(bool value);
    void setColorReference(const QString &value);
    Q_INVOKABLE void resetFixtures();

    // Trace writer for handler spans; inert unless a profiling session is armed.
    AppearanceTraceBridge *traceBridge() const;

signals:
    void appearanceInputChanged();
    void correctionsChanged();
    void paletteSamplePathChanged();
    void paletteSaveStatusChanged();
    void savedAccentKeysChanged();
    void paletteHistoryStatusChanged();
    void appearanceErrorChanged();
    void appearanceRejected(const QString &message);
    void draftNameChanged();
    void draftEmailChanged();
    void optInChanged();
    void notificationsChanged();
    void deliveryChanged();
    void draftChanged();
    void committedChanged();
    void dialogNameChanged();
    void dialogEmailChanged();
    void dialogChanged();
    void previewValueChanged();
    void selectedTabChanged();
    void rowSelectedChanged();
    void pinnedChanged();
    void collectionVisibleChanged();
    void colorReferenceChanged();

private:
    struct Profile {
        QString name = QStringLiteral("Maya Chen");
        QString email = QStringLiteral("maya.chen@example.test");
        bool optIn = true;
        bool notifications = true;
        QString delivery = QStringLiteral("daily");
    };
    void rejectAppearance(const QString &message);
    struct Correction {
        bool owned = false;
        bool priorKeyPresent = false;
        QVariant priorRawValue;
    };
    using Corrections = std::array<Correction, 2>;
    struct Transaction {
        quint64 id;
        QVariantMap candidate;
        QVariantMap before;
        Corrections corrections;
        Transaction *parent;
        bool rejected = false;
    };
    bool submit(const QVariantMap &input, const Corrections &corrections);
    void reconcileCorrections(bool installedNotification = false);
    void installCorrections(quint64 id, const Corrections &corrections);
    Corrections currentCorrections();
    QColor effectiveAccent(const QVariantMap &input) const;
    void clearForChangedAccent(QVariantMap &input, Corrections &corrections) const;
    static void restoreCorrections(QVariantMap &input, Corrections &corrections);
    bool hasColorOverride(const QString &key) const;
    Corrections m_corrections;
    Transaction *m_activeTransaction = nullptr;
    quint64 m_nextTransaction = 0;
    quint64 m_installedTransaction = 0;
    std::unique_ptr<PaletteSampleWriter> m_sampleWriter;
    QString m_paletteSaveStatus;
    bool m_paletteSaveFailed = false;
    QStringList m_savedAccentKeys;
    QString m_paletteHistoryStatus;
    bool m_paletteHistoryAvailable = false;
    InterActive::Appearance *const m_appearance;
    AppearanceTraceBridge *const m_traceBridge;
    QString m_appearanceError;
    quint64 m_appearanceErrorRevision = 0;
    Profile m_draft;
    Profile m_committed;
    QString m_dialogName = m_draft.name;
    QString m_dialogEmail = m_draft.email;
    qreal m_previewValue = 37;
    int m_selectedTab = 0;
    bool m_rowSelected = false;
    bool m_pinned = false;
    bool m_collectionVisible = true;
    QString m_colorReference;
};
