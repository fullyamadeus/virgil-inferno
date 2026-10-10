#pragma once

#include "domain/records.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <functional>

namespace virgil {

// Non-copyable RAII owner. Construct, use and destroy on the same worker thread.
class Database {
public:
    Database(const QString& filename, bool create, const QString& expectedIdentity = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    QSqlQuery query(const QString& sql, const QVariantList& values = {}) const;
    void begin();
    void commit();
    void rollback() noexcept;
    QString libraryId() const;
    Snapshot records() const;
    void insert(const Semester& semester);
    void insert(const Course& course);
    void reconcile(const QList<Material>& discovered, const std::function<void()>& checkpoint, bool ownTransaction = true);
    void saveMaterial(const Material& material);

    void relocate(const QString& source, const QString& destination);
    void reconcileFolders(const QList<Folder>& folders, const std::function<void()>& checkpoint);
    void createSnapshot(const QString& filename) const;
    void verifySnapshot(const QString& filename) const;
    static constexpr int schemaVersion = 6;
    static constexpr int applicationId = 0x5647524C;

private:
    QString connection_;
    void initialize(bool create, const QString& expectedIdentity);
};

class Transaction {
public:
    explicit Transaction(Database& database) : database_(database) { database_.begin(); }
    ~Transaction() { if (!committed_) database_.rollback(); }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit() { database_.commit(); committed_ = true; }
private:
    Database& database_;
    bool committed_ = false;
};

} // namespace virgil
