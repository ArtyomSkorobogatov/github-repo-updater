#include "git/git_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::tempPath() + "/git runner test XXXXXX");
    GitRunner runner;
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            qCritical() << message;
            ++failures;
        }
    };
    if (!directory.isValid())
        return 1;
    const auto path = directory.path();
    check(runner.run(path, {"init"}).success(), "Initialize a temporary repository");
    auto result = runner.run(path, {"status", "--porcelain=v1"});
    check(result.success() && result.stdOut.isEmpty(), "Clean status");
    QFile file(path + "/local.txt");
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    file.write("local work\n");
    file.close();
    result = runner.run(path, {"status", "--porcelain=v1"});
    check(result.success() && result.stdOut.contains("?? local.txt"), "Capture dirty status");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == "local work\n", "Preserve local content");
    result = runner.run(path, {"rev-parse", "--verify", "refs/heads/missing"});
    check(!result.success() && result.status == GitProcessStatus::Finished
          && result.exitCode != 0 && !result.stdErr.isEmpty(), "Capture Git failure and stderr");
    check(runner.run(path + "/missing", {"status"}).status == GitProcessStatus::InvalidInput,
          "Reject missing working directory");
    check(runner.run({}, {"status"}).status == GitProcessStatus::InvalidInput,
          "Reject empty working directory");
    check(runner.run(path, {}).status == GitProcessStatus::InvalidInput, "Reject empty arguments");
    check(runner.run(path, {"status"}, 0).status == GitProcessStatus::InvalidInput, "Reject invalid timeout");
    qputenv("GIT_DIR", (path + "/missing").toUtf8());
    check(runner.run(path, {"status", "--porcelain=v1"}).success(), "Ignore inherited repository overrides");
    qunsetenv("GIT_DIR");
    return failures == 0 ? 0 : 1;
}
