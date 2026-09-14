#include "git_runner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>

GitResult GitRunner::run(const QString &repositoryPath,
                         const QStringList &arguments,
                         int timeoutMs) const
{
    GitResult result;
    if (repositoryPath.isEmpty() || !QDir(repositoryPath).exists()
        || arguments.isEmpty() || timeoutMs <= 0) {
        result.status = GitProcessStatus::InvalidInput;
        result.errorMessage = QStringLiteral("An existing directory, Git arguments and a positive timeout are required.");
        return result;
    }

    QProcess process;
    process.setWorkingDirectory(QDir(repositoryPath).absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
    // Do not let inherited Git overrides redirect the operation elsewhere.
    for (const auto &key : environment.keys()) {
        if (key.startsWith(QStringLiteral("GIT_")))
            environment.remove(key);
    }
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    environment.insert(QStringLiteral("GIT_SSH_COMMAND"), QStringLiteral("ssh -oBatchMode=yes"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardInputFile(QProcess::nullDevice());

    QElapsedTimer timer;
    timer.start();
    process.start(QStringLiteral("git"), arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(timeoutMs)) {
        result.status = process.error() == QProcess::Timedout
            ? GitProcessStatus::TimedOut : GitProcessStatus::FailedToStart;
        result.errorMessage = process.errorString();
    } else if (!process.waitForFinished(static_cast<int>(qMax<qint64>(0, timeoutMs - timer.elapsed())))) {
        result.status = process.error() == QProcess::Timedout
            ? GitProcessStatus::TimedOut : GitProcessStatus::Crashed;
        result.errorMessage = process.errorString();
    } else if (process.exitStatus() == QProcess::CrashExit) {
        result.status = GitProcessStatus::Crashed;
        result.errorMessage = process.errorString();
    } else {
        result.status = GitProcessStatus::Finished;
        result.exitCode = process.exitCode();
    }

    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
    }
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    return result;
}
