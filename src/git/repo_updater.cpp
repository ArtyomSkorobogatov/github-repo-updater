#include "repo_updater.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace {
RepoResult error(const QString &path, const QString &operation, const GitResult &result)
{
    return {path, RepoStatus::Error,
            operation + "\n" + result.errorMessage + "\n" + result.stdErr};
}
}

RepoResult RepoUpdater::checkLocalState(const QString &path) const
{
    for (const auto &marker : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD",
                               "rebase-merge", "rebase-apply", "sequencer", "BISECT_START", "index.lock"}) {
        auto result = git.run(path, {"rev-parse", "--git-path", marker});
        if (!result.success())
            return error(path, QStringLiteral("Не удалось проверить состояние Git."), result);
        if (QFileInfo::exists(QDir(path).absoluteFilePath(result.stdOut.trimmed())))
            return {path, RepoStatus::RequiresAttention,
                    QStringLiteral("Есть незавершённая операция Git или блокировка: ") + marker};
    }

    auto result = git.run(path, {"status", "--porcelain=v1", "-z", "--untracked-files=all", "--ignore-submodules=all"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось проверить локальные изменения."), result);
    if (!result.stdOut.isEmpty())
        return {path, RepoStatus::LocalChanges, QStringLiteral("Обновление пропущено: имеются локальные изменения.")};

    result = git.run(path, {"ls-files", "-v", "-z"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось проверить индекс."), result);
    for (const auto &entry : result.stdOut.split(QChar('\0'), Qt::SkipEmptyParts)) {
        if (entry.front() == QChar('S') || entry.front().isLower())
            return {path, RepoStatus::RequiresAttention,
                    QStringLiteral("Индекс содержит skip-worktree или assume-unchanged; требуется ручная проверка.")};
    }

    result = git.run(path, {"ls-files", "--stage", "-z"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось проверить субмодули."), result);
    for (const auto &entry : result.stdOut.split(QChar('\0'), Qt::SkipEmptyParts)) {
        if (!entry.startsWith(QStringLiteral("160000 ")))
            continue;
        const auto relativePath = entry.mid(entry.indexOf('\t') + 1);
        const auto subPath = QDir(path).absoluteFilePath(relativePath);
        const auto expected = entry.section(' ', 1, 1);
        if (!QFileInfo::exists(subPath + "/.git"))
            return {path, RepoStatus::SubmoduleChanges,
                    QStringLiteral("Субмодуль требует ручной инициализации: ") + relativePath};
        const auto head = git.run(subPath, {"rev-parse", "--verify", "HEAD"});
        if (!head.success())
            return error(path, QStringLiteral("Не удалось проверить субмодуль: ") + relativePath, head);
        if (head.stdOut.trimmed() != expected)
            return {path, RepoStatus::SubmoduleChanges,
                    QStringLiteral("Субмодуль находится на другом коммите: ") + relativePath};
        const auto subResult = checkLocalState(subPath);
        if (subResult.status != RepoStatus::Pending)
            return {path, subResult.status == RepoStatus::Error ? RepoStatus::Error : RepoStatus::SubmoduleChanges,
                    QStringLiteral("Субмодуль требует внимания: ") + relativePath + "\n" + subResult.message};
    }
    result = git.run(path, {"status", "--porcelain=v1", "-z", "--untracked-files=all", "--ignore-submodules=none"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось проверить изменения указателей субмодулей."), result);
    if (!result.stdOut.isEmpty())
        return {path, RepoStatus::SubmoduleChanges,
                QStringLiteral("Есть локальные изменения субмодулей или их указателей в индексе.")};
    return {path, RepoStatus::Pending, {}};
}

RepoResult RepoUpdater::update(const QString &repositoryPath) const
{
    const auto path = QFileInfo(repositoryPath).canonicalFilePath();
    if (repositoryPath.isEmpty() || path.isEmpty() || !QFileInfo(path).isDir())
        return {repositoryPath, RepoStatus::Error, QStringLiteral("Директория не существует.")};
    auto result = git.run(path, {"rev-parse", "--show-toplevel"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось открыть рабочий репозиторий."), result);
    if (QFileInfo(result.stdOut.trimmed()).canonicalFilePath() != path)
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Нужно указать корень репозитория.")};
    const auto localState = checkLocalState(path);
    if (localState.status != RepoStatus::Pending)
        return localState;

    const auto branch = git.run(path, {"symbolic-ref", "--quiet", "HEAD"});
    if (!branch.success())
        return {path, RepoStatus::RequiresAttention, QStringLiteral("HEAD не указывает на ветвь.")};
    const auto head = git.run(path, {"rev-parse", "--verify", "HEAD"});
    if (!head.success())
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Ветка не содержит коммитов.")};
    const auto upstream = git.run(path, {"rev-parse", "--symbolic-full-name", "@{upstream}"});
    if (!upstream.success())
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Для ветки не настроен upstream.")};
    const auto branchName = branch.stdOut.trimmed().mid(QStringLiteral("refs/heads/").size());
    const auto remote = git.run(path, {"config", "--get", "branch." + branchName + ".remote"});
    if (!remote.success() || remote.stdOut.trimmed().isEmpty())
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Для ветки не настроен remote.")};
    const auto source = git.run(path, {"config", "--get-all", "branch." + branchName + ".merge"});
    const auto sourceRef = source.stdOut.trimmed();
    const auto trackingRef = upstream.stdOut.trimmed();
    if (!source.success() || !sourceRef.startsWith("refs/heads/") || sourceRef.contains('\n')
        || !trackingRef.startsWith("refs/remotes/" + remote.stdOut.trimmed() + "/"))
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Нестандартная настройка upstream; требуется ручная проверка.")};

    // Fetch updates remote-tracking refs and objects, not the working tree or local branch.
    // Explicit refspec prevents custom fetch configuration from updating local branches.
    result = git.run(path, {"-c", "core.hooksPath=" + QProcess::nullDevice(),
                           "fetch", "--no-tags", "--no-prune", "--no-prune-tags",
                           "--no-recurse-submodules", "--no-write-fetch-head", "--refmap=",
                           "--", remote.stdOut.trimmed(), sourceRef + ":" + trackingRef});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось получить изменения с remote."), result);
    const auto target = git.run(path, {"rev-parse", "--verify", upstream.stdOut.trimmed() + "^{commit}"});
    if (!target.success())
        return error(path, QStringLiteral("Не удалось определить коммит upstream."), target);
    const auto oldHead = head.stdOut.trimmed();
    const auto newHead = target.stdOut.trimmed();
    result = git.run(path, {"merge-base", "--is-ancestor", oldHead, newHead});
    if (!result.success()) {
        if (result.status != GitProcessStatus::Finished || result.exitCode != 1)
            return error(path, QStringLiteral("Не удалось сравнить историю ветвей."), result);
        result = git.run(path, {"merge-base", "--is-ancestor", newHead, oldHead});
        if (result.success())
            return {path, RepoStatus::RequiresAttention, QStringLiteral("Локальная ветка опережает upstream. Обновление пропущено.")};
        if (result.status != GitProcessStatus::Finished || result.exitCode != 1)
            return error(path, QStringLiteral("Не удалось сравнить историю ветвей."), result);
        return {path, RepoStatus::Diverged, QStringLiteral("Локальная и удалённая ветви разошлись. Обновление пропущено.")};
    }

    // Switching submodules safely requires a separate multi-repository transaction.
    // Until that exists, reject the entire update before changing the root HEAD.
    result = git.run(path, {"diff", "--raw", "--no-renames", "--no-abbrev", "-z", oldHead, newHead, "--"});
    if (!result.success())
        return error(path, QStringLiteral("Не удалось проверить входящие изменения."), result);
    const auto entries = result.stdOut.split(QChar('\0'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i + 1 < entries.size(); i += 2) {
        if (entries[i].startsWith(":160000 ") || entries[i].section(' ', 1, 1) == "160000"
            || entries[i + 1] == ".gitmodules")
            return {path, RepoStatus::SubmoduleChanges,
                    QStringLiteral("Входящее обновление меняет субмодули; требуется ручное обновление: ") + entries[i + 1]};
    }

    // Recheck after network access; never knowingly apply to a changed local state.
    const auto currentState = checkLocalState(path);
    if (currentState.status != RepoStatus::Pending)
        return currentState;
    const auto currentHead = git.run(path, {"rev-parse", "--verify", "HEAD"});
    const auto currentBranch = git.run(path, {"symbolic-ref", "--quiet", "HEAD"});
    if (!currentHead.success() || !currentBranch.success()
        || currentHead.stdOut != head.stdOut || currentBranch.stdOut != branch.stdOut)
        return {path, RepoStatus::RequiresAttention, QStringLiteral("Состояние репозитория изменилось во время проверки.")};
    if (oldHead == newHead)
        return {path, RepoStatus::UpToDate, QStringLiteral("Репозиторий уже обновлён.")};

    // This only advances HEAD: no merge commit, rebase, autostash or submodule checkout.
    result = git.run(path, {"-c", "core.hooksPath=" + QProcess::nullDevice(), "-c", "submodule.recurse=false",
                           "merge", "--ff-only", "--no-autostash", "--no-edit", "--no-overwrite-ignore", newHead});
    if (!result.success())
        return error(path, QStringLiteral("Fast-forward не завершён. Проверьте состояние репозитория; автоматический откат не выполнялся."), result);
    return {path, RepoStatus::Updated, QStringLiteral("Репозиторий обновлён через fast-forward.")};
}
