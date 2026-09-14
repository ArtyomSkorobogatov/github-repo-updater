#include "git/repo_updater.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <stdexcept>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::tempPath() + "/repo updater test XXXXXX");
    if (!directory.isValid())
        return 1;
    GitRunner git;
    RepoUpdater updater;
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            qCritical() << message;
            ++failures;
        }
    };
    const auto run = [&](const QString &path, const QStringList &args) {
        const auto result = git.run(path, args);
        if (!result.success())
            throw std::runtime_error((args.join(' ') + ": " + result.errorMessage + result.stdErr).toStdString());
        return result.stdOut.trimmed();
    };
    const auto write = [&](const QString &path, const QByteArray &content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
            throw std::runtime_error("Cannot write fixture");
    };
    const auto commit = [&](const QString &path) {
        run(path, {"add", "."});
        run(path, {"-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                   "-c", "commit.gpgSign=false", "-c", "core.hooksPath=", "commit", "-m", "fixture"});
    };
    try {
        const auto base = directory.path();
        const auto remote = base + "/remote.git";
        const auto author = base + "/author";
        const auto local = base + "/local";
        run(base, {"init", "--bare", "--initial-branch=main", remote});
        run(base, {"clone", remote, author});
        write(author + "/file.txt", "initial\n");
        commit(author);
        run(author, {"push", "origin", "main"});
        run(base, {"clone", remote, local});
        check(updater.update(local).status == RepoStatus::UpToDate, "Already up to date");
        const auto initial = run(local, {"rev-parse", "HEAD"});
        write(author + "/file.txt", "remote update\n");
        commit(author);
        run(author, {"push", "origin", "main"});

        write(local + "/untracked.txt", "local work\n");
        check(updater.update(local).status == RepoStatus::LocalChanges, "Untracked work blocks update");
        check(run(local, {"rev-parse", "HEAD"}) == initial, "Dirty HEAD preserved");
        check(run(local, {"rev-parse", "origin/main"}) == initial, "Dirty repository is not fetched");
        QFile::remove(local + "/untracked.txt");
        write(local + "/file.txt", "local edit\n");
        check(updater.update(local).status == RepoStatus::LocalChanges, "Unstaged work blocks update");
        run(local, {"add", "file.txt"});
        check(updater.update(local).status == RepoStatus::LocalChanges, "Staged work blocks update");
        // Restore only the disposable fixture, never a user's working repository.
        write(local + "/file.txt", "initial\n");
        run(local, {"add", "file.txt"});
        auto result = updater.update(local);
        check(result.status == RepoStatus::Updated, qPrintable("Fast-forward: " + result.message));
        check(run(local, {"rev-parse", "HEAD"}) == run(author, {"rev-parse", "HEAD"}), "Exact remote commit, no merge commit");

        write(local + "/only-local.txt", "local commit\n");
        commit(local);
        const auto localHead = run(local, {"rev-parse", "HEAD"});
        check(updater.update(local).status == RepoStatus::RequiresAttention, "Local ahead requires attention");
        write(author + "/only-remote.txt", "remote commit\n");
        commit(author);
        run(author, {"push", "origin", "main"});
        check(updater.update(local).status == RepoStatus::Diverged, "Diverged branches require attention");
        check(run(local, {"rev-parse", "HEAD"}) == localHead, "Divergence preserves HEAD");
        check(run(local, {"status", "--porcelain"}).isEmpty(), "Divergence leaves clean working tree");
        check(!QFileInfo::exists(local + "/.git/MERGE_HEAD"), "No merge started");

        const auto fresh = base + "/fresh";
        run(base, {"clone", remote, fresh});
        run(fresh, {"update-index", "--assume-unchanged", "file.txt"});
        check(updater.update(fresh).status == RepoStatus::RequiresAttention, "Hidden index state requires attention");
        run(fresh, {"update-index", "--no-assume-unchanged", "file.txt"});
        write(fresh + "/.git/MERGE_HEAD", (localHead + "\n").toUtf8());
        check(updater.update(fresh).status == RepoStatus::RequiresAttention, "Unfinished operation blocks update");
        QFile::remove(fresh + "/.git/MERGE_HEAD");
        run(fresh, {"switch", "--detach"});
        check(updater.update(fresh).status == RepoStatus::RequiresAttention, "Detached HEAD requires attention");
        run(fresh, {"switch", "main"});
        run(fresh, {"branch", "--unset-upstream"});
        check(updater.update(fresh).status == RepoStatus::RequiresAttention, "Missing upstream requires attention");
        run(fresh, {"branch", "--set-upstream-to=origin/main"});
        run(fresh, {"remote", "set-url", "origin", base + "/missing.git"});
        check(updater.update(fresh).status == RepoStatus::Error, "Remote failure reported");
        run(fresh, {"remote", "set-url", "origin", remote});

        const auto sub = base + "/sub";
        run(base, {"init", "--initial-branch=main", sub});
        write(sub + "/sub.txt", "submodule\n");
        commit(sub);
        const auto nested = base + "/nested";
        run(base, {"init", "--initial-branch=main", nested});
        write(nested + "/nested.txt", "nested content\n");
        commit(nested);
        run(sub, {"-c", "protocol.file.allow=always", "submodule", "add", nested, "nested"});
        commit(sub);
        run(author, {"-c", "protocol.file.allow=always", "submodule", "add", sub, "libs/sub"});
        commit(author);
        run(author, {"push", "origin", "main"});
        const auto freshHead = run(fresh, {"rev-parse", "HEAD"});
        check(updater.update(fresh).status == RepoStatus::SubmoduleChanges, "Incoming submodule addition blocks entire update");
        check(run(fresh, {"rev-parse", "HEAD"}) == freshHead, "Submodule attention preserves root HEAD");

        const auto withSub = base + "/with sub";
        run(base, {"clone", remote, withSub});
        check(updater.update(withSub).status == RepoStatus::SubmoduleChanges, "Uninitialized submodule requires attention");
        run(withSub, {"-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive"});
        check(updater.update(withSub).status == RepoStatus::UpToDate, "Clean initialized submodule accepted");
        write(withSub + "/libs/sub/nested/nested.txt", "nested local work\n");
        auto nestedResult = updater.update(withSub);
        check(nestedResult.status == RepoStatus::SubmoduleChanges && nestedResult.message.contains("nested"),
              "Nested submodule local changes detected recursively");
        write(withSub + "/libs/sub/nested/nested.txt", "nested content\n");
        write(withSub + "/libs/sub/sub.txt", "local submodule work\n");
        check(updater.update(withSub).status == RepoStatus::SubmoduleChanges, "Dirty submodule blocks update");
        write(withSub + "/libs/sub/sub.txt", "submodule\n");
        write(withSub + "/libs/sub/new.txt", "untracked\n");
        check(updater.update(withSub).status == RepoStatus::SubmoduleChanges, "Untracked submodule work blocks update");
        commit(withSub + "/libs/sub");
        check(updater.update(withSub).status == RepoStatus::SubmoduleChanges, "Local submodule commit blocks update");
        run(withSub, {"add", "libs/sub"});
        check(updater.update(withSub).status == RepoStatus::SubmoduleChanges, "Staged submodule pointer blocks update");

        const auto ignored = base + "/ignored";
        run(base, {"clone", remote, ignored});
        run(ignored, {"-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive"});
        write(ignored + "/.git/info/exclude", "private.txt\n");
        write(ignored + "/private.txt", "precious ignored content\n");
        const auto ignoredHead = run(ignored, {"rev-parse", "HEAD"});
        write(author + "/private.txt", "incoming tracked content\n");
        commit(author);
        run(author, {"push", "origin", "main"});
        check(updater.update(ignored).status == RepoStatus::Error, "Ignored file collision refuses fast-forward");
        check(run(ignored, {"rev-parse", "HEAD"}) == ignoredHead, "Ignored collision preserves HEAD");
        QFile preserved(ignored + "/private.txt");
        check(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == "precious ignored content\n",
              "Ignored content preserved");

        check(updater.update(base).status == RepoStatus::Error, "Non-repository rejected");
        check(updater.update(base + "/missing").status == RepoStatus::Error, "Missing path rejected");
    } catch (const std::exception &exception) {
        qCritical() << exception.what();
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
