#include <QApplication>
#include "gui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("GitHubRepoUpdater");
    app.setApplicationName("GitHubRepoUpdater");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.show();

    return app.exec();
}
