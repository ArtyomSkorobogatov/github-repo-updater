#include "update_worker.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

UpdateWorker::UpdateWorker(QString directory, bool update, QObject *parent)
    : QThread(parent), directory(std::move(directory)), updateRepositories(update)
{
    qRegisterMetaType<RepoResult>();
}

void UpdateWorker::run()
{
    if (!QFileInfo(directory).isDir() || !QFileInfo(directory).isReadable()) {
        emit scanFailed(QStringLiteral("Директория недоступна: ") + directory);
        return;
    }
    QStringList paths;
    QSet<QString> visited;
    QStringList pending{QFileInfo(directory).canonicalFilePath()};
    while (!pending.isEmpty() && !isInterruptionRequested()) {
        const auto path = pending.takeLast();
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || !QFileInfo(path).isReadable()) {
            emit scanFailed(QStringLiteral("Не удалось прочитать папку: ") + path);
            continue;
        }
        if (visited.contains(canonical))
            continue;
        visited.insert(canonical);
        QDir dir(path);
        if (QFileInfo::exists(dir.filePath(".git"))) {
            paths.append(path);
            continue; // Submodules belong to their root repository.
        }
        for (const auto &child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot
                                                   | QDir::NoSymLinks | QDir::Hidden, QDir::Name)) {
            if (child.fileName() != ".git")
                pending.append(child.absoluteFilePath());
        }
    }
    if (isInterruptionRequested())
        return;
    std::sort(paths.begin(), paths.end());
    emit repositoriesFound(paths);
    if (!updateRepositories)
        return;
    RepoUpdater updater;
    for (qsizetype row = 0; row < paths.size() && !isInterruptionRequested(); ++row) {
        emit repositoryStarted(static_cast<int>(row));
        emit repositoryFinished(static_cast<int>(row), updater.update(paths[row]));
    }
}
