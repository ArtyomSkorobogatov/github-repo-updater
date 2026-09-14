#pragma once

#include "git_runner.h"

enum class RepoStatus
{
    Pending,
    UpToDate,
    Updated,
    LocalChanges,
    Diverged,
    SubmoduleChanges,
    RequiresAttention,
    Error
};

struct RepoResult
{
    QString repositoryPath;
    RepoStatus status = RepoStatus::Pending;
    QString message;
};

class RepoUpdater
{
public:
    // Blocking; invoke from a worker thread. Never retries or rolls back a mutation.
    RepoResult update(const QString &repositoryPath) const;

private:
    RepoResult checkLocalState(const QString &repositoryPath) const;
    GitRunner git;
};
