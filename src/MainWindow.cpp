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

    refreshData();
    m_monitorTimer->start();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("ascend-monitor"));
    resize(1500, 920);
    setMinimumSize(1100, 700);

    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);

    auto *logoLabel = new QLabel(this);
    const QPixmap logoPixmap(QStringLiteral(":/icons/app-logo.jpg"));
    if (!logoPixmap.isNull()) {
        logoLabel->setPixmap(logoPixmap.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoLabel->setText(QStringLiteral("NPU"));
    }
    logoLabel->setFixedSize(32, 32);
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setToolTip(QStringLiteral("ascend-monitor"));

    m_localModeRadio = new QRadioButton(QStringLiteral("Local"), this);
    m_remoteModeRadio = new QRadioButton(QStringLiteral("Remote"), this);
    m_localModeRadio->setChecked(true);

    auto *connectionModeGroup = new QButtonGroup(this);
    connectionModeGroup->setExclusive(true);
    connectionModeGroup->addButton(m_localModeRadio);
    connectionModeGroup->addButton(m_remoteModeRadio);

    m_userEdit = new QLineEdit(QStringLiteral("root"), this);
    m_userEdit->setPlaceholderText(QStringLiteral("user"));
    m_userEdit->setMaximumWidth(120);

    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_hostEdit->setPlaceholderText(QStringLiteral("host"));
    m_hostEdit->setMinimumWidth(180);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(QStringLiteral("password"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setMinimumWidth(160);

    m_remoteConnectButton = new QPushButton(QStringLiteral("连接"), this);
    connect(m_remoteConnectButton, &QPushButton::clicked, this, &MainWindow::refreshData);

    m_userLabel = new QLabel(QStringLiteral("User"), this);
    m_hostLabel = new QLabel(QStringLiteral("Host"), this);
    m_passwordLabel = new QLabel(QStringLiteral("Password"), this);

    connect(m_localModeRadio, &QRadioButton::toggled, this, &MainWindow::updateConnectionModeUi);
    connect(m_remoteModeRadio, &QRadioButton::toggled, this, &MainWindow::updateConnectionModeUi);

    m_statusLabel = new QLabel(QStringLiteral("Scanning npu-smi..."), this);
    m_updatedLabel = new QLabel(QStringLiteral("Last update: -"), this);
    m_npuSmiVersionLabel = new QLabel(QString(), this);
    m_npuSmiVersionLabel->setStyleSheet(QStringLiteral("color: #4b5563;"));

    toolbarLayout->addWidget(logoLabel);
    toolbarLayout->addWidget(m_localModeRadio);
    toolbarLayout->addWidget(m_remoteModeRadio);
    toolbarLayout->addWidget(m_userLabel);
    toolbarLayout->addWidget(m_userEdit);
    toolbarLayout->addWidget(m_hostLabel);
    toolbarLayout->addWidget(m_hostEdit);
    toolbarLayout->addWidget(m_passwordLabel);
    toolbarLayout->addWidget(m_passwordEdit);
    toolbarLayout->addWidget(m_remoteConnectButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(m_npuSmiVersionLabel);

    if (!kRemoteFeatureEnabled) {
        m_remoteModeRadio->setChecked(false);
        m_remoteModeRadio->setVisible(false);
    }

    updateConnectionModeUi();

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_cardTable = new QTableWidget(this);
    m_cardTable->setColumnCount(8);
    m_cardTable->setHorizontalHeaderLabels({
        QStringLiteral("Card ID"),
        QStringLiteral("Software Version"),
        QStringLiteral("Firmware Version"),
        QStringLiteral("Product Name"),
        QStringLiteral("Serial Number"),
        QStringLiteral("Chip Count"),
        QStringLiteral("Memory Total"),
        QStringLiteral("Health")
    });
    m_cardTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_cardTable->verticalHeader()->setVisible(false);
    m_cardTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_cardTable->setAlternatingRowColors(true);
    m_cardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cardTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_cardTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cardTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    connect(m_cardTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateSelectedCardMappings);
    connect(m_cardTable, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
        updateSelectedCardMappings();
    });

    auto *mappingSection = new QWidget(this);
    auto *mappingSectionLayout = new QVBoxLayout(mappingSection);
    mappingSectionLayout->setContentsMargins(0, 0, 0, 0);
    mappingSectionLayout->setSpacing(4);

    auto *mappingDivider = new QFrame(mappingSection);
    mappingDivider->setFrameShape(QFrame::HLine);
    mappingDivider->setFrameShadow(QFrame::Sunken);

    m_mappingTable = new QTableWidget(mappingSection);
    m_mappingTable->setColumnCount(8);
    m_mappingTable->setHorizontalHeaderLabels({
        QStringLiteral("Logic NPU ID"),
        QStringLiteral("Card ID"),
        QStringLiteral("Chip ID"),
        QStringLiteral("Chip Name"),
        QStringLiteral("Memory Total"),
        QStringLiteral("AI Core"),
        QStringLiteral("Control Core"),
        QStringLiteral("Data Core")
    });
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setMinimumSectionSize(90);
    m_mappingTable->verticalHeader()->setVisible(false);
    m_mappingTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappingTable->setAlternatingRowColors(true);
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mappingTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    mappingSectionLayout->addWidget(mappingDivider);
    mappingSectionLayout->addWidget(m_mappingTable);

    setupMonitorPanel();

    splitter->addWidget(m_cardTable);
    splitter->addWidget(mappingSection);
    splitter->addWidget(m_monitorPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 7);

    rootLayout->addLayout(toolbarLayout);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(centralWidget);

    auto *windowStatusBar = statusBar();
    windowStatusBar->setSizeGripEnabled(true);
    windowStatusBar->addWidget(m_statusLabel, 1);
    windowStatusBar->addPermanentWidget(m_updatedLabel);
}

void MainWindow::setupProcessPanel()
{
    m_processPage = new QWidget(this);
    auto *processLayout = new QVBoxLayout(m_processPage);
    processLayout->setContentsMargins(0, 0, 0, 0);
    processLayout->setSpacing(6);

    m_processSummaryLabel = new QLabel(QStringLiteral("Select a Card ID to view processes."), m_processPage);
    m_processTable = new QTableWidget(m_processPage);
    m_processTable->setColumnCount(5);
    m_processTable->setHorizontalHeaderLabels({
        QStringLiteral("PID"),
        QStringLiteral("Process Name"),
        QStringLiteral("Card ID"),
        QStringLiteral("Chip ID"),
        QStringLiteral("Process Memory(MB)")
    });
    m_processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_processTable->verticalHeader()->setVisible(false);
    m_processTable->setAlternatingRowColors(true);
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    processLayout->addWidget(m_processSummaryLabel);
    processLayout->addWidget(m_processTable);
}

void MainWindow::setupMonitorPanel()
{
    m_monitorPanel = new QWidget(this);
    auto *panelLayout = new QVBoxLayout(m_monitorPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);

    auto *switchLayout = new QHBoxLayout();
    switchLayout->setSpacing(8);

    m_chartViewButton = new QPushButton(QStringLiteral("监控图表"), m_monitorPanel);
    m_processViewButton = new QPushButton(QStringLiteral("进程监控"), m_monitorPanel);
    m_chartViewButton->setCheckable(true);
    m_processViewButton->setCheckable(true);
    m_chartViewButton->setChecked(true);

    const QString switchButtonStyle = QStringLiteral(
        "QPushButton {"
        "  min-width: 120px;"
        "  padding: 6px 10px;"
        "  border: none;"
        "  border-bottom: 2px solid transparent;"
        "  background: transparent;"
        "  color: #4b5563;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  color: #1f2937;"
        "}"
        "QPushButton:checked {"
        "  color: #0b5ed7;"
        "  border-bottom: 3px solid #0b5ed7;"
        "  font-weight: 700;"
        "}"
        "QPushButton:pressed {"
        "  color: #084298;"
        "}");
    m_chartViewButton->setStyleSheet(switchButtonStyle);
    m_processViewButton->setStyleSheet(switchButtonStyle);

    m_viewButtonGroup = new QButtonGroup(this);
    m_viewButtonGroup->setExclusive(true);
    m_viewButtonGroup->addButton(m_chartViewButton);
    m_viewButtonGroup->addButton(m_processViewButton);

    connect(m_chartViewButton, &QPushButton::clicked, this, &MainWindow::showChartsPage);
    connect(m_processViewButton, &QPushButton::clicked, this, &MainWindow::showProcessPage);

    switchLayout->addWidget(m_chartViewButton);
    switchLayout->addWidget(m_processViewButton);
    switchLayout->addStretch(1);

    m_monitorStack = new QStackedWidget(m_monitorPanel);
    m_chartPage = new QWidget(m_monitorStack);
    auto *grid = new QGridLayout(m_chartPage);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    setupChart(&m_tempChart, &m_tempChartView, &m_tempAxisX, &m_tempAxisY,
               QStringLiteral("Core Temperature"), QStringLiteral("C"), 0.0, 120.0);
    setupChart(&m_memoryChart, &m_memoryChartView, &m_memoryAxisX, &m_memoryAxisY,
               QStringLiteral("Core Memory Usage"), QStringLiteral("%"), 0.0, 100.0);
    setupChart(&m_powerChart, &m_powerChartView, &m_powerAxisX, &m_powerAxisY,
               QStringLiteral("Card Power"), QStringLiteral("W"), 0.0, 400.0);
    setupChart(&m_usageChart, &m_usageChartView, &m_usageAxisX, &m_usageAxisY,
               QStringLiteral("NPU Usage"), QStringLiteral("%"), 0.0, 100.0);

    m_aicoreSeries = ensureSeries(nullptr, m_usageChart, m_usageAxisX, m_usageAxisY,
                                  QStringLiteral("aicore"), QStringLiteral("Aicore Usage"), QColor(220, 53, 69));
    m_aivectorSeries = ensureSeries(nullptr, m_usageChart, m_usageAxisX, m_usageAxisY,
                                    QStringLiteral("aivector"), QStringLiteral("Aivector Usage"), QColor(0, 123, 255));
    m_aicpuSeries = ensureSeries(nullptr, m_usageChart, m_usageAxisX, m_usageAxisY,
                                 QStringLiteral("aicpu"), QStringLiteral("Aicpu Usage"), QColor(40, 167, 69));

    grid->addWidget(m_tempChartView, 0, 0);
    grid->addWidget(m_memoryChartView, 0, 1);
    grid->addWidget(m_powerChartView, 1, 0);
    grid->addWidget(m_usageChartView, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);

    setupProcessPanel();

    m_monitorStack->addWidget(m_chartPage);
    m_monitorStack->addWidget(m_processPage);
    m_monitorStack->setCurrentWidget(m_chartPage);

    panelLayout->addLayout(switchLayout);
    panelLayout->addWidget(m_monitorStack, 1);
}

void MainWindow::setupChart(
    QChart **chart,
    QChartView **view,
    QDateTimeAxis **axisX,
    QValueAxis **axisY,
    const QString &title,
    const QString &yAxisLabel,
    double yMin,
    double yMax)
{
    *chart = new QChart();
    (*chart)->setTitle(title);
    (*chart)->legend()->setVisible(true);
    (*chart)->legend()->setAlignment(Qt::AlignBottom);

    QFont titleFont = (*chart)->titleFont();
    titleFont.setPointSize(qMax(10, titleFont.pointSize() - 1));
    titleFont.setBold(true);
    (*chart)->setTitleFont(titleFont);

    QFont legendFont = (*chart)->legend()->font();
    legendFont.setPointSize(qMax(8, legendFont.pointSize() - 1));
    (*chart)->legend()->setFont(legendFont);

    *axisX = new QDateTimeAxis();
    (*axisX)->setTitleText(QString());
    (*axisX)->setFormat(QStringLiteral("mm:ss"));
    (*axisX)->setTickCount(4);
    (*axisX)->setLabelsAngle(-35);
    const QDateTime now = QDateTime::currentDateTime();
    (*axisX)->setRange(now.addSecs(-(m_maxSamples - 1)), now);

    *axisY = new QValueAxis();
    (*axisY)->setTitleText(yAxisLabel);
    (*axisY)->setRange(yMin, yMax);

    (*chart)->addAxis(*axisX, Qt::AlignBottom);
    (*chart)->addAxis(*axisY, Qt::AlignLeft);

    *view = new QChartView(*chart, m_monitorPanel);
    (*view)->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::refreshData()
{
    if (m_scanInProgress) {
        return;
    }

    m_statusLabel->setText(QStringLiteral("Refreshing device inventory..."));

    const NpuConnectionOptions options = currentConnectionOptions();
    m_activeConnectionOptions = options;
    m_scanInProgress = true;

    auto *watcher = new QFutureWatcher<NpuScanResult>(this);
    connect(watcher, &QFutureWatcher<NpuScanResult>::finished, this, [this, watcher]() {
        m_lastResult = watcher->result();
        watcher->deleteLater();
        m_scanInProgress = false;

        const QString versionText = m_lastResult.npuSmiVersion.trimmed();
        m_npuSmiVersionLabel->setText(
            versionText.isEmpty()
                ? QString()
                : QStringLiteral("npu-smi: %1").arg(versionText));

        populateCards(m_lastResult.cards);
        updateSelectedCardMappings();

        if (!m_lastResult.errorMessage.isEmpty()) {
            m_statusLabel->setText(m_lastResult.errorMessage);
        } else if (m_lastResult.hasData()) {
            m_statusLabel->setText(QStringLiteral("Loaded %1 card(s) and %2 mapping row(s).")
                                       .arg(m_lastResult.cards.size())
                                       .arg(m_lastResult.mappings.size()));
        } else {
            m_statusLabel->setText(QStringLiteral("No NPU information was returned by npu-smi."));
        }

        m_updatedLabel->setText(QStringLiteral("Last update: %1")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));

        updateMonitorCharts();
        updateProcessPanel();
    });

    watcher->setFuture(QtConcurrent::run([options]() {
        NpuInfoProvider provider;
        provider.setConnectionOptions(options);
        return provider.scan();
    }));
}

void MainWindow::populateCards(const QVector<NpuCard> &cards)
{
    m_cardTable->clearSpans();
    m_cardTable->setRowCount(cards.size());

    for (int row = 0; row < cards.size(); ++row) {
        const NpuCard &card = cards.at(row);
        m_cardTable->setItem(row, 0, new QTableWidgetItem(displayValue(card.cardId)));
        m_cardTable->setItem(row, 1, new QTableWidgetItem(displayValue(card.softwareVersion)));
        m_cardTable->setItem(row, 2, new QTableWidgetItem(displayValue(card.firmwareVersion)));
        m_cardTable->setItem(row, 3, new QTableWidgetItem(displayValue(card.productName)));
        m_cardTable->setItem(row, 4, new QTableWidgetItem(displayValue(card.serialNumber)));
        m_cardTable->setItem(row, 5, new QTableWidgetItem(displayValue(card.chipCount)));
        m_cardTable->setItem(row, 6, new QTableWidgetItem(displayValue(card.memoryTotal)));
        m_cardTable->setItem(row, 7, new QTableWidgetItem(displayValue(card.health)));
    }

    if (cards.isEmpty()) {
        m_cardTable->setRowCount(0);
        m_cardTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        return;
    }

    if (cards.size() > 10) {
        m_cardTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    } else {
        m_cardTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    m_cardTable->clearSelection();
    m_cardTable->setCurrentCell(-1, -1);
}

void MainWindow::populateMappings(const QVector<NpuMappingEntry> &mappings)
{
    QVector<NpuMappingEntry> visibleMappings;
    for (const NpuMappingEntry &entry : mappings) {
        if (entry.chipName.trimmed().compare(QStringLiteral("Mcu"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        visibleMappings.append(entry);
    }

    m_mappingTable->clearSpans();
    m_mappingTable->setRowCount(visibleMappings.size());

    for (int row = 0; row < visibleMappings.size(); ++row) {
        const NpuMappingEntry &entry = visibleMappings.at(row);
        m_mappingTable->setItem(row, 0, new QTableWidgetItem(displayValue(entry.logicId)));
        m_mappingTable->setItem(row, 1, new QTableWidgetItem(displayValue(entry.cardId)));
        m_mappingTable->setItem(row, 2, new QTableWidgetItem(displayValue(entry.chipId)));
        m_mappingTable->setItem(row, 3, new QTableWidgetItem(displayValue(entry.chipName)));
        m_mappingTable->setItem(row, 4, new QTableWidgetItem(displayValue(entry.memoryTotal)));
        m_mappingTable->setItem(row, 5, new QTableWidgetItem(displayValue(entry.aiCore)));
        m_mappingTable->setItem(row, 6, new QTableWidgetItem(displayValue(entry.controlCore)));
        m_mappingTable->setItem(row, 7, new QTableWidgetItem(displayValue(entry.dataCore)));
    }

    if (visibleMappings.isEmpty()) {
        m_mappingTable->setRowCount(0);
        m_mappingTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        return;
    }

    if (!selectedCardId().isEmpty()) {
        m_mappingTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_mappingTable->verticalHeader()->setDefaultSectionSize(28);
    } else if (visibleMappings.size() > 10) {
        m_mappingTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    } else {
        m_mappingTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
}

void MainWindow::updateSelectedCardMappings()
{
    const QString cardId = selectedCardId();
    if (m_selectedMonitorCardId != cardId) {
        m_selectedMonitorCardId = cardId;
        resetMonitorSeries();
        if (m_monitorInProgress) {
            m_monitorRefreshPending = true;
        } else {
            updateMonitorCharts();
        }
    }

    if (cardId.isEmpty()) {
        populateMappings(m_lastResult.mappings);
        updateProcessPanel();
        return;
    }

    QVector<NpuMappingEntry> filteredMappings;
    bool selectedOk = false;
    const int selectedCardInt = cardId.toInt(&selectedOk);
    for (const NpuMappingEntry &mapping : m_lastResult.mappings) {
        const QString mappingId = mapping.cardId.trimmed();
        bool mappingOk = false;
        const int mappingInt = mappingId.toInt(&mappingOk);
        const bool sameText = mappingId == cardId;
        const bool sameNumber = selectedOk && mappingOk && selectedCardInt == mappingInt;
        if (sameText || sameNumber) {
            filteredMappings.append(mapping);
        }
    }
    populateMappings(filteredMappings);
    updateProcessPanel();
}

void MainWindow::updateMonitorCharts()
{
    if (m_scanInProgress) {
        return;
    }

    if (m_monitorInProgress) {
        m_monitorRefreshPending = true;
        return;
    }

    QStringList cardIds;
    const QString selectedCard = selectedCardId();
    if (!selectedCard.isEmpty()) {
        cardIds.append(selectedCard);
    } else {
        cardIds = currentCardIds();
    }

    if (cardIds.isEmpty()) {
        return;
    }

    m_monitorInProgress = true;
    const NpuConnectionOptions options = m_activeConnectionOptions;

    auto *watcher = new QFutureWatcher<NpuMonitorSnapshot>(this);
    connect(watcher, &QFutureWatcher<NpuMonitorSnapshot>::finished, this, [this, watcher]() {
        const NpuMonitorSnapshot snapshot = watcher->result();
        watcher->deleteLater();
        m_monitorInProgress = false;

        ++m_sampleIndex;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        double aicoreSum = 0.0;
        double aivectorSum = 0.0;
        double aicpuSum = 0.0;
        int aicoreCount = 0;
        int aivectorCount = 0;
        int aicpuCount = 0;

        for (const NpuCoreMonitor &monitor : snapshot.coreMonitors) {
            const QString coreKey = QStringLiteral("%1-c%2").arg(monitor.cardId, monitor.chipId);

            if (monitor.hasTemperature) {
                auto *series = ensureSeries(&m_tempSeriesByCore, m_tempChart, m_tempAxisX, m_tempAxisY,
                                            coreKey, coreKey, colorFromKey(coreKey));
                appendSeriesPoint(series, nowMs, monitor.temperature);
            }

            if (monitor.hasMemoryUsageRate) {
                auto *series = ensureSeries(&m_memorySeriesByCore, m_memoryChart, m_memoryAxisX, m_memoryAxisY,
                                            coreKey, coreKey, colorFromKey(coreKey));
                appendSeriesPoint(series, nowMs, monitor.memoryUsageRate);
            }

            if (monitor.hasAicoreUsage) {
                aicoreSum += monitor.aicoreUsage;
                ++aicoreCount;
            }
            if (monitor.hasAivectorUsage) {
                aivectorSum += monitor.aivectorUsage;
                ++aivectorCount;
            }
            if (monitor.hasAicpuUsage) {
                aicpuSum += monitor.aicpuUsage;
                ++aicpuCount;
            }
        }

        if (aicoreCount > 0) {
            appendSeriesPoint(m_aicoreSeries, nowMs, aicoreSum / aicoreCount);
        }
        if (aivectorCount > 0) {
            appendSeriesPoint(m_aivectorSeries, nowMs, aivectorSum / aivectorCount);
        }
        if (aicpuCount > 0) {
            appendSeriesPoint(m_aicpuSeries, nowMs, aicpuSum / aicpuCount);
        }

        for (const NpuPowerMonitor &powerMonitor : snapshot.powerMonitors) {
            if (!powerMonitor.hasPower) {
                continue;
            }
            const QString key = QStringLiteral("card-%1").arg(powerMonitor.cardId);
            auto *series = ensureSeries(&m_powerSeriesByCard, m_powerChart, m_powerAxisX, m_powerAxisY,
                                        key, key, colorFromKey(key));
            appendSeriesPoint(series, nowMs, powerMonitor.power);
        }

        const QDateTime maxTime = QDateTime::fromMSecsSinceEpoch(nowMs);
        const QDateTime minTime = maxTime.addSecs(-(m_maxSamples - 1));
        m_tempAxisX->setRange(minTime, maxTime);
        m_memoryAxisX->setRange(minTime, maxTime);
        m_powerAxisX->setRange(minTime, maxTime);
        m_usageAxisX->setRange(minTime, maxTime);

        if (!snapshot.errors.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("Monitor refresh has %1 issue(s).").arg(snapshot.errors.size()));
        }

        if (m_monitorRefreshPending) {
            m_monitorRefreshPending = false;
            updateMonitorCharts();
            return;
        }

        if (m_monitorStack != nullptr && m_monitorStack->currentWidget() == m_processPage) {
            updateProcessPanel();
        }
    });

    watcher->setFuture(QtConcurrent::run([options, cardIds]() {
        NpuInfoProvider provider;
