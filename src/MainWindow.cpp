#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColor>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFont>
#include <QPen>
#include <QPixmap>
#include <QRadioButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <QChart>
#include <QChartView>
#include <QLegend>
#include <QLineSeries>
#include <QValueAxis>

static constexpr bool kRemoteFeatureEnabled =
#ifdef ASCEND_MONITOR_ENABLE_REMOTE
    true;
#else
    false;
#endif

namespace {

QString displayValue(const QString &value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("-") : value.trimmed();
}

QColor colorFromKey(const QString &key)
{
    uint hash = qHash(key);
    int h = static_cast<int>(hash % 360);
    return QColor::fromHsv(h, 180, 200);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    qApp->installEventFilter(this);

    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(1000);
    connect(m_monitorTimer, &QTimer::timeout, this, &MainWindow::updateMonitorCharts);
