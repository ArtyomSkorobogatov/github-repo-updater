#pragma once

#include <QMainWindow>
#include "update_worker.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QCheckBox;
class QSpinBox;
class QTimer;
class QSystemTrayIcon;
class QAction;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void startWork(bool update);
    void finishWork();
    void showWindow();
    void requestExit();
    void saveSettings();
    void configureTimer();
    void setBusy(bool busy);

    QLineEdit *directory;
    QPushButton *browse;
    QPushButton *scan;
    QPushButton *update;
    QTableWidget *table;
    QPlainTextEdit *details;
    QLabel *summary;
    QCheckBox *automatic;
    QSpinBox *interval;
    QTimer *timer;
    QSystemTrayIcon *tray;
    QAction *trayUpdate;
    UpdateWorker *worker = nullptr;
    bool quitting = false;
    bool updating = false;
    bool scanError = false;
    int completed = 0;
    int updated = 0;
    int attentionRepositoryCount = 0;
    QStringList attention;
};
