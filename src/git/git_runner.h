#pragma once

#include <QString>
#include <QStringList>

enum class GitProcessStatus
{
    Finished,
    FailedToStart,
    Crashed,
    TimedOut,
    InvalidInput
};

struct GitResult
{
    GitProcessStatus status = GitProcessStatus::FailedToStart;
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
    QString errorMessage;

    bool success() const
    {
        return status == GitProcessStatus::Finished && exitCode == 0;
    }
};

class GitRunner
{
public:
    // Blocking: call from a worker thread when used by the GUI.
    // Arguments are trusted application input; update policy belongs to the caller.
    // A timeout stops Git but does not roll back changes: inspect repository state
    // before any subsequent operation, and never automatically retry a mutation.
    GitResult run(
        const QString &repositoryPath,
        const QStringList &arguments,
        int timeoutMs = 30000
    ) const;
};
