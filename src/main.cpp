#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("ascend-monitor");
    QApplication::setOrganizationName("ascend-monitor");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-logo.jpg")));

    QString lockRoot = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (lockRoot.isEmpty()) {
        lockRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    if (!lockRoot.isEmpty()) {
        QDir().mkpath(lockRoot);
    }

    QLockFile instanceLock(lockRoot + QStringLiteral("/ascend-monitor.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock()) {
        QMessageBox::warning(
            nullptr,
            QStringLiteral("ascend-monitor"),
            QStringLiteral("ascend-monitor is already running."));
        return 0;
    }

    MainWindow window;
    window.setWindowIcon(QIcon(QStringLiteral(":/icons/app-logo.jpg")));
    window.show();
    return app.exec();
}