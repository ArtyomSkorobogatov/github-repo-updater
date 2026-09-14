#include "gui/main_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

class GuiTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDirectory;

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName("RepoUpdaterTests");
        QCoreApplication::setApplicationName("GuiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
        qApp->setQuitOnLastWindowClosed(false);
    }

    void init() { QSettings().clear(); }

    void scansRecursivelyWithoutEnteringRepositories()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir root(directory.path());
        QVERIFY(root.mkpath("group/one/.git"));
        QVERIFY(root.mkpath("group/one/vendor/sub/.git"));
        QVERIFY(root.mkpath("two"));
        QFile marker(root.filePath("two/.git"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("gitdir: elsewhere\n");
        marker.close();
        UpdateWorker worker(directory.path(), false);
        QSignalSpy found(&worker, &UpdateWorker::repositoriesFound);
        QSignalSpy finished(&worker, &QThread::finished);
        worker.start();
        QVERIFY(finished.wait(10000));
        worker.wait();
        QCOMPARE(found.size(), 1);
        const auto paths = found.front().front().toStringList();
        QCOMPARE(paths.size(), 2);
        QVERIFY(paths[0].endsWith("group/one"));
        QVERIFY(paths[1].endsWith("two"));
    }

    void updatesInBackgroundAndContinuesAfterAttention()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        GitRunner git;
        for (const auto &name : {"one", "two"}) {
            const auto path = directory.filePath(name);
            QVERIFY(git.run(directory.path(), {"init", path}).success());
            QFile file(path + "/local.txt");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("local work\n");
        }
        MainWindow window;
        window.show();
        window.findChild<QLineEdit *>("directory")->setText(directory.path());
        auto *tray = window.findChild<QSystemTrayIcon *>();
        const auto plainIcon = tray->icon().pixmap(64, 64).toImage();
        auto *update = window.findChild<QPushButton *>("update");
        QTest::mouseClick(update, Qt::LeftButton);
        QVERIFY(!update->isEnabled());
        auto *worker = window.findChild<UpdateWorker *>();
        QVERIFY(worker);
        QVERIFY(worker->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(update->isEnabled(), 15000);
        auto *table = window.findChild<QTableWidget *>("repositories");
        QCOMPARE(table->rowCount(), 2);
        for (int row = 0; row < 2; ++row)
            QCOMPARE(table->item(row, 1)->text(), QStringLiteral("Локальные изменения"));
        auto *popup = window.findChild<QMessageBox *>();
        QVERIFY(popup);
        QVERIFY(popup->isVisible());
        QVERIFY(popup->detailedText().contains("one"));
        QVERIFY(popup->detailedText().contains("two"));
        popup->close();
        QVERIFY(tray->toolTip().contains(QStringLiteral("Требуют внимания: 2")));
        const auto attentionIcon = tray->icon().pixmap(64, 64).toImage();
        QVERIFY(attentionIcon != plainIcon);

        // A discovery-only pass must not clear known attention results.
        auto *scan = window.findChild<QPushButton *>("scan");
        QTest::mouseClick(scan, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(scan->isEnabled(), 10000);
        QCOMPARE(tray->icon().pixmap(64, 64).toImage(), attentionIcon);

        window.findChild<QLineEdit *>("directory")->setText(directory.filePath("missing"));
        QTest::mouseClick(update, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(update->isEnabled(), 10000);
        QCOMPARE(tray->icon().pixmap(64, 64).toImage(), attentionIcon);
        QVERIFY(tray->toolTip().contains(QStringLiteral("Требуют внимания: 2")));

        QTemporaryDir emptyDirectory;
        QVERIFY(emptyDirectory.isValid());
        window.findChild<QLineEdit *>("directory")->setText(emptyDirectory.path());
        QTest::mouseClick(update, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(update->isEnabled(), 10000);
        QCOMPARE(tray->icon().pixmap(64, 64).toImage(), plainIcon);
        QVERIFY(tray->toolTip().contains(QStringLiteral("Требуют внимания: 0")));
    }

    void closingKeepsApplicationAccessibleAndTrayRestoresWindow()
    {
        MainWindow window;
        window.show();
        QVERIFY(window.isVisible());
        window.close();
        if (QSystemTrayIcon::isSystemTrayAvailable())
            QVERIFY(!window.isVisible());
        else {
            QVERIFY(window.isVisible());
            QVERIFY(window.findChild<QLabel *>("summary")->text().contains(QStringLiteral("трей недоступен")));
        }
        QVERIFY(!qApp->quitOnLastWindowClosed());
        auto *tray = window.findChild<QSystemTrayIcon *>();
        QVERIFY(tray);
        QVERIFY(tray->isVisible());
        window.hide();
        tray->activated(QSystemTrayIcon::Trigger);
        QVERIFY(window.isVisible());
        QVERIFY(tray->contextMenu());
    }

    void restoresSettingsWithoutOverwritingAutomaticOption()
    {
        QSettings settings;
        settings.setValue("directory", settingsDirectory.path());
        settings.setValue("intervalMinutes", 37);
        settings.setValue("automatic", true);
        MainWindow window;
        QCOMPARE(window.findChild<QLineEdit *>("directory")->text(), settingsDirectory.path());
        QCOMPARE(window.findChild<QSpinBox *>("interval")->value(), 37);
        QVERIFY(window.findChild<QCheckBox *>("automatic")->isChecked());
        QCOMPARE(settings.value("automatic").toBool(), true);
        QVERIFY(!window.findChild<UpdateWorker *>());
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QLabel *>("summary")->text().startsWith(QStringLiteral("Проверено: 0.")), 10000);
        QVERIFY(window.findChild<QPushButton *>("update")->isEnabled());
    }

    void startupRequiresAutomaticOptionAndSavedPath_data()
    {
        QTest::addColumn<bool>("automatic");
        QTest::addColumn<bool>("hasPath");
        QTest::newRow("automatic disabled") << false << true;
        QTest::newRow("no saved directory") << true << false;
    }

    void startupRequiresAutomaticOptionAndSavedPath()
    {
        QFETCH(bool, automatic);
        QFETCH(bool, hasPath);
        QSettings settings;
        settings.setValue("automatic", automatic);
        settings.setValue("directory", hasPath ? settingsDirectory.path() : QString());
        MainWindow window;
        const auto initialSummary = window.findChild<QLabel *>("summary")->text();
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<UpdateWorker *>());
        QCOMPARE(window.findChild<QLabel *>("summary")->text(), initialSummary);
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
