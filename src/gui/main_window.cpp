#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString statusText(RepoStatus status)
{
    switch (status) {
    case RepoStatus::Pending: return QStringLiteral("Ожидает");
    case RepoStatus::Updated: return QStringLiteral("Обновлён");
    case RepoStatus::UpToDate: return QStringLiteral("Актуален");
    case RepoStatus::LocalChanges: return QStringLiteral("Локальные изменения");
    case RepoStatus::Diverged: return QStringLiteral("Ветви разошлись");
    case RepoStatus::SubmoduleChanges: return QStringLiteral("Проверить субмодули");
    case RepoStatus::RequiresAttention: return QStringLiteral("Требует внимания");
    case RepoStatus::Error: return QStringLiteral("Ошибка");
    }
    return {};
}

QIcon applicationIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#2563eb"));
    painter.drawRoundedRect(2, 2, 60, 60, 14, 14);
    painter.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(23, 18, 23, 46);
    painter.drawLine(23, 36, 43, 23);
    painter.setBrush(Qt::white);
    for (const auto &point : {QPoint(23, 18), QPoint(23, 46), QPoint(43, 23)})
        painter.drawEllipse(point, 4, 4);
    return QIcon(pixmap);
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("GitHub Repo Updater");
    setWindowIcon(applicationIcon());
    resize(960, 620);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("Обновление репозиториев"), this);
    auto titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *hint = new QLabel(QStringLiteral("Локальная работа сохраняется. Неоднозначные состояния требуют вашего внимания."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *pathLayout = new QHBoxLayout;
    directory = new QLineEdit(this);
    directory->setObjectName("directory");
    directory->setPlaceholderText(QStringLiteral("Директория с репозиториями"));
    directory->setAccessibleName(QStringLiteral("Директория с репозиториями"));
    browse = new QPushButton(QStringLiteral("Выбрать…"), this);
    pathLayout->addWidget(directory, 1);
    pathLayout->addWidget(browse);
    layout->addLayout(pathLayout);
    auto *actions = new QHBoxLayout;
    scan = new QPushButton(QStringLiteral("Найти репозитории"), this);
    scan->setObjectName("scan");
    update = new QPushButton(QStringLiteral("Обновить все"), this);
    update->setObjectName("update");
    automatic = new QCheckBox(QStringLiteral("Автоматически каждые"), this);
    automatic->setObjectName("automatic");
    interval = new QSpinBox(this);
    interval->setObjectName("interval");
    interval->setRange(1, 1440);
    interval->setSuffix(QStringLiteral(" мин"));
    actions->addWidget(scan);
    actions->addWidget(update);
    actions->addStretch();
    actions->addWidget(automatic);
    actions->addWidget(interval);
    layout->addLayout(actions);
    table = new QTableWidget(0, 3, this);
    table->setObjectName("repositories");
    table->setHorizontalHeaderLabels({QStringLiteral("Репозиторий"), QStringLiteral("Статус"), QStringLiteral("Результат")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    details = new QPlainTextEdit(this);
    details->setReadOnly(true);
    details->setPlaceholderText(QStringLiteral("Выберите репозиторий, чтобы увидеть подробности."));
    details->setMaximumHeight(130);
    layout->addWidget(details);
    summary = new QLabel(QStringLiteral("Выберите директорию. Поиск включает вложенные папки, но не заходит внутрь репозиториев."), this);
    summary->setObjectName("summary");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    setCentralWidget(central);
    menuBar()->addMenu(QStringLiteral("Файл"))->addAction(QStringLiteral("Выход"), this, &MainWindow::requestExit);

    tray = new QSystemTrayIcon(windowIcon(), this);
    tray->setToolTip(windowTitle());
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("Открыть"), this, &MainWindow::showWindow);
    trayUpdate = menu->addAction(QStringLiteral("Обновить все"), this, [this] { startWork(true); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Выход"), this, &MainWindow::requestExit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showWindow();
    });
    connect(tray, &QSystemTrayIcon::messageClicked, this, &MainWindow::showWindow);
    tray->show();

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { startWork(true); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("Директория с репозиториями"), directory->text());
        if (!path.isEmpty()) {
            directory->setText(QDir::toNativeSeparators(path));
            startWork(false);
        }
    });
    connect(scan, &QPushButton::clicked, this, [this] { startWork(false); });
    connect(update, &QPushButton::clicked, this, [this] { startWork(true); });
    connect(directory, &QLineEdit::returnPressed, this, [this] { startWork(false); });
    connect(directory, &QLineEdit::editingFinished, this, &MainWindow::configureTimer);
    connect(automatic, &QCheckBox::toggled, this, &MainWindow::configureTimer);
    connect(interval, &QSpinBox::valueChanged, this, &MainWindow::configureTimer);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto row = table->currentRow();
        if (row >= 0 && table->item(row, 2))
            details->setPlainText(table->item(row, 0)->text() + "\n\n" + table->item(row, 2)->text());
    });
    QSettings settings;
    const QSignalBlocker blockInterval(interval);
    const QSignalBlocker blockAutomatic(automatic);
    directory->setText(settings.value("directory").toString());
    interval->setValue(settings.value("intervalMinutes", 15).toInt());
    automatic->setChecked(settings.value("automatic", false).toBool());
    restoreGeometry(settings.value("geometry").toByteArray());
    configureTimer();
}

MainWindow::~MainWindow()
{
    if (worker) {
        worker->requestInterruption();
        worker->wait();
    }
}

void MainWindow::startWork(bool doUpdate)
{
    if (worker || quitting)
        return;
    if (directory->text().trimmed().isEmpty()) {
        showWindow();
        summary->setText(QStringLiteral("Сначала выберите директорию с репозиториями."));
        return;
    }
    saveSettings();
    timer->stop();
    updating = doUpdate;
    scanError = false;
    completed = updated = 0;
    attention.clear();
    table->setRowCount(0);
    details->clear();
    summary->setText(QStringLiteral("Поиск репозиториев…"));
    setBusy(true);
    worker = new UpdateWorker(directory->text().trimmed(), doUpdate, this);
    connect(worker, &UpdateWorker::repositoriesFound, this, [this](const QStringList &paths) {
        table->setRowCount(static_cast<int>(paths.size()));
        for (int row = 0; row < paths.size(); ++row) {
            table->setItem(row, 0, new QTableWidgetItem(QDir::toNativeSeparators(paths[row])));
            table->setItem(row, 1, new QTableWidgetItem(statusText(RepoStatus::Pending)));
            table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Ещё не проверен")));
            table->item(row, 0)->setToolTip(table->item(row, 0)->text());
        }
    });
    connect(worker, &UpdateWorker::repositoryStarted, this, [this](int row) {
        table->item(row, 1)->setText(QStringLiteral("Проверка…"));
        summary->setText(QStringLiteral("Обновление %1 из %2").arg(row + 1).arg(table->rowCount()));
    });
    connect(worker, &UpdateWorker::repositoryFinished, this, [this](int row, const RepoResult &result) {
        ++completed;
        if (result.status == RepoStatus::Updated)
            ++updated;
        const bool needsAttention = result.status != RepoStatus::Updated && result.status != RepoStatus::UpToDate;
        if (needsAttention)
            attention.append(result.repositoryPath + "\n" + result.message);
        table->item(row, 1)->setText(statusText(result.status));
        table->item(row, 1)->setIcon(style()->standardIcon(result.status == RepoStatus::Error
            ? QStyle::SP_MessageBoxCritical : needsAttention ? QStyle::SP_MessageBoxWarning : QStyle::SP_DialogApplyButton));
        table->item(row, 2)->setText(result.message);
        table->item(row, 2)->setToolTip(result.message);
        if (table->currentRow() == row)
            details->setPlainText(result.repositoryPath + "\n\n" + result.message);
    });
    connect(worker, &UpdateWorker::scanFailed, this, [this](const QString &message) {
        scanError = true;
        summary->setText(message);
        attention.append(message);
    });
    connect(worker, &QThread::finished, this, &MainWindow::finishWork);
    worker->start();
}

void MainWindow::finishWork()
{
    worker->wait();
    worker->deleteLater();
    worker = nullptr;
    if (quitting) {
        qApp->quit();
        return;
    }
    setBusy(false);
    if (!scanError) {
        summary->setText(updating
            ? QStringLiteral("Проверено: %1. Обновлено: %2. Требуют внимания: %3.").arg(completed).arg(updated).arg(attention.size())
            : QStringLiteral("Найдено репозиториев: %1.").arg(table->rowCount()));
    }
    if (!attention.isEmpty()) {
        for (auto *previous : findChildren<QMessageBox *>())
            previous->close();
        const auto message = QStringLiteral("Требуют внимания: %1. Откройте результаты для подробностей.").arg(attention.size());
        if (isVisible() || !QSystemTrayIcon::isSystemTrayAvailable()) {
            auto *box = new QMessageBox(QMessageBox::Warning, windowTitle(), message, QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setDetailedText(attention.join("\n\n"));
            box->setWindowModality(Qt::NonModal);
            box->show();
        } else {
            tray->showMessage(windowTitle(), message, QSystemTrayIcon::Warning, 10000);
        }
    }
    configureTimer();
}

void MainWindow::setBusy(bool busy)
{
    for (QWidget *widget : QList<QWidget *>{directory, browse, scan, update})
        widget->setEnabled(!busy);
    trayUpdate->setEnabled(!busy);
}

void MainWindow::configureTimer()
{
    timer->stop();
    interval->setEnabled(automatic->isChecked());
    if (automatic->isChecked() && !worker && !quitting && !directory->text().trimmed().isEmpty())
        timer->start(interval->value() * 60000);
    saveSettings();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("directory", directory->text());
    settings.setValue("intervalMinutes", interval->value());
    settings.setValue("automatic", automatic->isChecked());
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::showWindow()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::requestExit()
{
    if (quitting)
        return;
    saveSettings();
    timer->stop();
    quitting = true;
    if (worker) {
        worker->requestInterruption();
        showWindow();
        summary->setText(QStringLiteral("Завершение текущего репозитория перед выходом…"));
        tray->setToolTip(QStringLiteral("Завершение текущего репозитория…"));
        return;
    }
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->ignore();
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        for (auto *box : findChildren<QMessageBox *>())
            box->close();
        hide();
    } else {
        summary->setText(QStringLiteral("Системный трей недоступен. Для завершения используйте меню «Файл → Выход»."));
    }
}
