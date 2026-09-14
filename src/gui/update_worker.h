#pragma once

#include "../git/repo_updater.h"
#include <QThread>

Q_DECLARE_METATYPE(RepoResult)

class UpdateWorker final : public QThread
{
    Q_OBJECT
public:
    UpdateWorker(QString directory, bool update, QObject *parent = nullptr);

signals:
    void repositoriesFound(const QStringList &paths);
    void repositoryStarted(int row);
    void repositoryFinished(int row, const RepoResult &result);
    void scanFailed(const QString &message);

protected:
    void run() override;

private:
    QString directory;
    bool updateRepositories;
};
