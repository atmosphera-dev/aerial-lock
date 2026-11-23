#pragma once
#include "common/client/service-connection.h"
#include <QAbstractListModel>
#include <QJsonArray>

namespace Aerial::Settings {
class PortalClient;
// Stable view identity; configuration and mutations remain in SourcesController.
class SourceRows : public QAbstractListModel {
public:
    explicit SourceRows(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : entries.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "sourceRow"}}; }
    void synchronize(const QVariantList &next);
private:
    QVariantList entries;
};
class SourcesController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QAbstractItemModel *rowModel READ rowModel CONSTANT)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY changed)
    Q_PROPERTY(bool sheetOpen READ sheetOpen NOTIFY changed)
    Q_PROPERTY(bool appleAvailable READ appleAvailable NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit SourcesController(Client::ServiceConnection &service, PortalClient &portal, QObject *parent = nullptr);
    // Notifies only on a real row change, so unrelated facts (progress,
    // busy state) never recreate row delegates under the pointer.
    QVariantList rows() const { return shown; }
    QAbstractItemModel *rowModel() { return &stableRows; }
    QVariantMap draft() const;
    bool sheetOpen() const { return opened; }
    bool appleAvailable() const;
    QString error() const { return problem.isEmpty() ? actionProblem : problem; }
    // One sources write at a time; rows are inert until it settles.
    bool busy() const { return pending != 0; }
    Q_INVOKABLE void begin(const QString &id = {});
    Q_INVOKABLE void field(const QString &key, const QVariant &value);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void choosePath();
    Q_INVOKABLE void save();
    Q_INVOKABLE void toggle(const QString &id, bool enabled);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void retry(const QString &id);
    Q_INVOKABLE void beginReorder(const QString &id);
    Q_INVOKABLE void dropBefore(const QString &id);
    Q_INVOKABLE void move(const QString &id, int delta);
signals:
    void changed();
    void rowsChanged();
private:
    QJsonArray sources() const;
    QVariantList computeRows() const;
    QVariantList shown;
    SourceRows stableRows{this};
    void reorderTo(int index);
    void submit(const QJsonArray &array, const QString &revision, const QString &owner, bool sheet);
    Client::ServiceConnection &service;
    PortalClient &portal;
    QJsonArray captured, dragged;
    QJsonObject fields;
    QString revision, owner, editing, dragId, dragRevision, dragOwner, problem, actionProblem;
    quint64 pending = 0, refresh = 0;
    quint64 chooser = 0;
    bool opened = false, savingSheet = false;
};
}
