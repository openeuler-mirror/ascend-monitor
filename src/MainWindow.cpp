#include "MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QButtonGroup>
#include <QColor>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFont>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QPen>
#include <QPixmap>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QtConcurrent>

#include <QChart>
#include <QChartView>
#include <QLegend>
#include <QLineSeries>
#include <QValueAxis>

namespace {
QString displayValue(const QString &value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("-") : value.trimmed();
}

#ifdef ASCEND_MONITOR_ENABLE_REMOTE
constexpr bool kRemoteFeatureEnabled = true;
#else
constexpr bool kRemoteFeatureEnabled = false;
#endif

QColor colorFromKey(const QString &key)
{
    const uint hash = qHash(key);
    return QColor::fromHsv(hash % 360, 190, 220);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    QSettings settings;
    m_chartRefreshSeconds = qBound(1, settings.value(QStringLiteral("refresh/chartSeconds"), 1).toInt(), 3600);
    m_deviceInfoRefreshSeconds = qBound(
        1,
        settings.value(QStringLiteral("refresh/deviceInfoSeconds"), 5).toInt(),
        3600);

    setupUi();
    qApp->installEventFilter(this);

    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(m_chartRefreshSeconds * 1000);
    connect(m_monitorTimer, &QTimer::timeout, this, &MainWindow::updateMonitorCharts);

    m_deviceInfoTimer = new QTimer(this);
    m_deviceInfoTimer->setInterval(m_deviceInfoRefreshSeconds * 1000);
    connect(m_deviceInfoTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    refreshData();
    m_monitorTimer->start();
    m_deviceInfoTimer->start();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("ascend-monitor"));
    resize(1500, 920);
    setMinimumSize(1100, 700);
    setupSettingsMenu();
    setupTitleBar();

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

void MainWindow::setupTitleBar()
{
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(36);
    m_titleBar->setStyleSheet(
        QStringLiteral(
            "QWidget#titleBar { background: palette(window); border-bottom: 1px solid #d1d5db; }"
            "QToolButton { border: none; border-radius: 0; }"
            "QToolButton:hover { background: #e5e7eb; }"
            "QToolButton#mainMenuButton::menu-indicator { image: none; }"
            "QToolButton#closeButton:hover { background: #dc2626; }"));

    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(8, 0, 0, 0);
    titleLayout->setSpacing(0);

    auto *titleLabel = new QLabel(QStringLiteral("ascend-monitor"), m_titleBar);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);
    titleLayout->addWidget(m_menuButton);

    auto *minimizeButton = new QToolButton(m_titleBar);
    minimizeButton->setToolTip(QStringLiteral("最小化"));
    minimizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));

    auto *maximizeButton = new QToolButton(m_titleBar);
    maximizeButton->setToolTip(QStringLiteral("最大化"));
    maximizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));

    auto *closeButton = new QToolButton(m_titleBar);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setToolTip(QStringLiteral("关闭"));
    closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));

    for (QToolButton *button : {minimizeButton, maximizeButton, closeButton}) {
        button->setAutoRaise(true);
        button->setFixedSize(36, 36);
        titleLayout->addWidget(button);
    }

    connect(minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maximizeButton, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);

    setMenuWidget(m_titleBar);
}

void MainWindow::setupSettingsMenu()
{
    m_menuButton = new QToolButton(this);
    m_menuButton->setObjectName(QStringLiteral("mainMenuButton"));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setToolTip(QStringLiteral("菜单"));
    m_menuButton->setFixedSize(36, 36);
    m_menuButton->setIcon(
        QIcon::fromTheme(
            QStringLiteral("open-menu-symbolic"),
            style()->standardIcon(QStyle::SP_TitleBarMenuButton)));

    auto *mainMenu = new QMenu(m_menuButton);
    QAction *configurationAction = mainMenu->addAction(QStringLiteral("配置"));

    auto *softwareInfoMenu = mainMenu->addMenu(QStringLiteral("软件信息"));
    QAction *applicationNameAction = softwareInfoMenu->addAction(
        QStringLiteral("软件名称：ascend-monitor"));
    QAction *applicationVersionAction = softwareInfoMenu->addAction(
        QStringLiteral("软件版本：0.1.0"));
    applicationNameAction->setEnabled(false);
    applicationVersionAction->setEnabled(false);
    m_menuButton->setMenu(mainMenu);

    connect(configurationAction, &QAction::triggered, this, &MainWindow::showConfigurationDialog);
}

void MainWindow::showConfigurationDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("配置"));
    dialog.setMinimumWidth(440);
    dialog.setStyleSheet(
        QStringLiteral(
            "QFrame#configurationCard {"
            "  background: palette(base);"
            "  border: 1px solid #d1d5db;"
            "  border-radius: 10px;"
            "}"
            "QToolButton#intervalButton {"
            "  border: none;"
            "  border-radius: 16px;"
            "  background: #e5e7eb;"
            "  font-size: 18px;"
            "}"
            "QToolButton#intervalButton:hover { background: #d1d5db; }"));

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(20, 20, 20, 16);
    dialogLayout->setSpacing(12);

    auto *sectionTitle = new QLabel(QStringLiteral("系统配置"), &dialog);
    QFont sectionFont = sectionTitle->font();
    sectionFont.setBold(true);
    sectionTitle->setFont(sectionFont);
    dialogLayout->addWidget(sectionTitle);

    auto *configurationCard = new QFrame(&dialog);
    configurationCard->setObjectName(QStringLiteral("configurationCard"));
    auto *configurationLayout = new QVBoxLayout(configurationCard);
    configurationLayout->setContentsMargins(14, 4, 14, 4);
    configurationLayout->setSpacing(0);

    int chartRefreshSeconds = m_chartRefreshSeconds;
    int deviceInfoRefreshSeconds = m_deviceInfoRefreshSeconds;

    const auto addIntervalRow = [configurationCard, configurationLayout](
                                    const QString &title,
                                    int *intervalSeconds) {
        auto *row = new QWidget(configurationCard);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 8, 0, 8);
        rowLayout->setSpacing(8);

        auto *label = new QLabel(title, row);
        auto *decreaseButton = new QToolButton(row);
        decreaseButton->setObjectName(QStringLiteral("intervalButton"));
        decreaseButton->setText(QStringLiteral("-"));
        decreaseButton->setFixedSize(32, 32);

        auto *valueLabel = new QLabel(row);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setMinimumWidth(54);

        auto *increaseButton = new QToolButton(row);
        increaseButton->setObjectName(QStringLiteral("intervalButton"));
        increaseButton->setText(QStringLiteral("+"));
        increaseButton->setFixedSize(32, 32);

        const auto refreshValue = [valueLabel, intervalSeconds]() {
            valueLabel->setText(QStringLiteral("%1 秒").arg(*intervalSeconds));
        };
        refreshValue();

        connect(decreaseButton, &QToolButton::clicked, row, [intervalSeconds, refreshValue]() {
            *intervalSeconds = qMax(1, *intervalSeconds - 1);
            refreshValue();
        });
        connect(increaseButton, &QToolButton::clicked, row, [intervalSeconds, refreshValue]() {
            *intervalSeconds = qMin(3600, *intervalSeconds + 1);
            refreshValue();
        });

        rowLayout->addWidget(label);
        rowLayout->addStretch(1);
        rowLayout->addWidget(decreaseButton);
        rowLayout->addWidget(valueLabel);
        rowLayout->addWidget(increaseButton);
        configurationLayout->addWidget(row);
    };

    addIntervalRow(
        QStringLiteral("图表刷新时间"),
        &chartRefreshSeconds);

    auto *separator = new QFrame(configurationCard);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: #e5e7eb;"));
    configurationLayout->addWidget(separator);

    addIntervalRow(
        QStringLiteral("状态刷新时间"),
        &deviceInfoRefreshSeconds);

    dialogLayout->addWidget(configurationCard);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    auto *saveButton = new QPushButton(QStringLiteral("保存"), &dialog);
    auto *exitButton = new QPushButton(QStringLiteral("退出"), &dialog);
    connect(saveButton, &QPushButton::clicked, &dialog, [this, &dialog, &chartRefreshSeconds, &deviceInfoRefreshSeconds]() {
        setChartRefreshInterval(chartRefreshSeconds);
        setDeviceInfoRefreshInterval(deviceInfoRefreshSeconds);
        dialog.accept();
    });
    connect(exitButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(exitButton);
    dialogLayout->addLayout(buttonLayout);

    dialog.exec();
}

void MainWindow::setChartRefreshInterval(int intervalSeconds)
{
    m_chartRefreshSeconds = qBound(1, intervalSeconds, 3600);
    if (m_monitorTimer != nullptr) {
        m_monitorTimer->setInterval(m_chartRefreshSeconds * 1000);
    }
    QSettings().setValue(QStringLiteral("refresh/chartSeconds"), m_chartRefreshSeconds);
}

void MainWindow::setDeviceInfoRefreshInterval(int intervalSeconds)
{
    m_deviceInfoRefreshSeconds = qBound(1, intervalSeconds, 3600);
    if (m_deviceInfoTimer != nullptr) {
        m_deviceInfoTimer->setInterval(m_deviceInfoRefreshSeconds * 1000);
    }
    QSettings().setValue(QStringLiteral("refresh/deviceInfoSeconds"), m_deviceInfoRefreshSeconds);
}

int MainWindow::chartHistoryDurationSeconds() const
{
    return (m_maxSamples - 1) * m_chartRefreshSeconds;
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
    m_paramViewButton = new QPushButton(QStringLiteral("参数配置"), m_monitorPanel);
    m_chartViewButton->setCheckable(true);
    m_processViewButton->setCheckable(true);
    m_paramViewButton->setCheckable(true);
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
    m_paramViewButton->setStyleSheet(switchButtonStyle);

    m_viewButtonGroup = new QButtonGroup(this);
    m_viewButtonGroup->setExclusive(true);
    m_viewButtonGroup->addButton(m_chartViewButton);
    m_viewButtonGroup->addButton(m_processViewButton);
    m_viewButtonGroup->addButton(m_paramViewButton);

    connect(m_chartViewButton, &QPushButton::clicked, this, &MainWindow::showChartsPage);
    connect(m_processViewButton, &QPushButton::clicked, this, &MainWindow::showProcessPage);
    connect(m_paramViewButton, &QPushButton::clicked, this, &MainWindow::showParamPage);

    switchLayout->addWidget(m_chartViewButton);
    switchLayout->addWidget(m_processViewButton);
    switchLayout->addWidget(m_paramViewButton);
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
    setupParamPanel();

    m_monitorStack->addWidget(m_chartPage);
    m_monitorStack->addWidget(m_processPage);
    m_monitorStack->addWidget(m_paramPage);
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
    (*axisX)->setRange(now.addSecs(-chartHistoryDurationSeconds()), now);

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

    if (m_monitorStack != nullptr && m_monitorStack->currentWidget() == m_paramPage) {
        updateParamPanel();
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
        const QDateTime minTime = maxTime.addSecs(-chartHistoryDurationSeconds());
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
        provider.setConnectionOptions(options);
        return provider.collectMonitor(cardIds);
    }));
}

void MainWindow::updateProcessPanel()
{
    if (m_processTable == nullptr || m_processSummaryLabel == nullptr) {
        return;
    }

    if (m_processInProgress || m_scanInProgress) {
        return;
    }

    const QString cardId = selectedCardId();
    const bool showAllCards = cardId.isEmpty();
    const QString targetCardId = showAllCards ? QString() : cardId;
    const NpuConnectionOptions options = m_activeConnectionOptions;
    m_processInProgress = true;

    auto *watcher = new QFutureWatcher<QVector<NpuProcessInfo>>(this);
    connect(watcher, &QFutureWatcher<QVector<NpuProcessInfo>>::finished, this, [this, watcher, showAllCards, cardId]() {
        const QVector<NpuProcessInfo> processes = watcher->result();
        watcher->deleteLater();
        m_processInProgress = false;

        m_processTable->clearSpans();
        m_processTable->setRowCount(processes.size());

        for (int row = 0; row < processes.size(); ++row) {
            const NpuProcessInfo &process = processes.at(row);
            m_processTable->setItem(row, 0, new QTableWidgetItem(displayValue(process.pid)));
            m_processTable->setItem(row, 1, new QTableWidgetItem(displayValue(process.processName)));
            m_processTable->setItem(row, 2, new QTableWidgetItem(displayValue(process.cardId)));
            m_processTable->setItem(row, 3, new QTableWidgetItem(displayValue(process.chipId)));
            m_processTable->setItem(row, 4, new QTableWidgetItem(displayValue(process.memoryMb)));
        }

        if (showAllCards) {
            m_processSummaryLabel->setText(
                QStringLiteral("All Cards | Processes: %1 | Refresh: %2s")
                    .arg(processes.size())
                    .arg(m_chartRefreshSeconds));
        } else {
            m_processSummaryLabel->setText(
                QStringLiteral("Card %1 | Processes: %2 | Refresh: %3s")
                    .arg(cardId)
                    .arg(processes.size())
                    .arg(m_chartRefreshSeconds));
        }
    });

    watcher->setFuture(QtConcurrent::run([options, targetCardId]() {
        NpuInfoProvider provider;
        provider.setConnectionOptions(options);
        return provider.collectProcesses(targetCardId);
    }));
}

void MainWindow::showChartsPage()
{
    if (m_monitorStack == nullptr || m_chartPage == nullptr) {
        return;
    }

    m_monitorStack->setCurrentWidget(m_chartPage);
    if (m_chartViewButton != nullptr) {
        m_chartViewButton->setChecked(true);
    }
}

void MainWindow::showProcessPage()
{
    if (m_monitorStack == nullptr || m_processPage == nullptr) {
        return;
    }

    m_monitorStack->setCurrentWidget(m_processPage);
    if (m_processViewButton != nullptr) {
        m_processViewButton->setChecked(true);
    }

    updateProcessPanel();
}

void MainWindow::showParamPage()
{
    if (m_monitorStack == nullptr || m_paramPage == nullptr) {
        return;
    }

    m_monitorStack->setCurrentWidget(m_paramPage);
    if (m_paramViewButton != nullptr) {
        m_paramViewButton->setChecked(true);
    }

    updateParamPanel();
}

void MainWindow::setupParamPanel()
{
    m_paramPage = new QWidget(this);
    auto *paramLayout = new QVBoxLayout(m_paramPage);
    paramLayout->setContentsMargins(0, 0, 0, 0);
    paramLayout->setSpacing(6);

    auto *paramToolbar = new QHBoxLayout();
    paramToolbar->setSpacing(8);

    m_paramCardLabel = new QLabel(QStringLiteral("Card: -"), m_paramPage);
    m_paramChipCombo = new QComboBox(m_paramPage);
    m_paramChipCombo->setMinimumWidth(160);
    m_paramRefreshButton = new QPushButton(QStringLiteral("刷新"), m_paramPage);
    m_paramEditButton = new QPushButton(QStringLiteral("修改参数"), m_paramPage);

    paramToolbar->addWidget(m_paramCardLabel);
    paramToolbar->addWidget(new QLabel(QStringLiteral("Chip:"), m_paramPage));
    paramToolbar->addWidget(m_paramChipCombo);
    paramToolbar->addStretch(1);
    paramToolbar->addWidget(m_paramRefreshButton);
    paramToolbar->addWidget(m_paramEditButton);

    m_paramTable = new QTableWidget(m_paramPage);
    m_paramTable->setColumnCount(7);
    m_paramTable->setHorizontalHeaderLabels({
        QStringLiteral("Chip ID"),
        QStringLiteral("参数"),
        QStringLiteral("当前值"),
        QStringLiteral("单位"),
        QStringLiteral("可调范围"),
        QStringLiteral("状态"),
        QStringLiteral("说明")
    });
    m_paramTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setAlternatingRowColors(true);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paramTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paramTable->setSelectionMode(QAbstractItemView::SingleSelection);

    paramLayout->addLayout(paramToolbar);
    paramLayout->addWidget(m_paramTable);

    connect(m_paramRefreshButton, &QPushButton::clicked, this, &MainWindow::updateParamPanel);
    connect(m_paramEditButton, &QPushButton::clicked, this, &MainWindow::applySelectedParam);
    connect(m_paramChipCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateParamPanel);
    connect(m_paramTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        applySelectedParam();
    });
}

QString MainWindow::selectedParamChipId() const
{
    if (m_paramChipCombo == nullptr) {
        return QString();
    }

    const QString text = m_paramChipCombo->currentData().toString().trimmed();
    if (text.isEmpty() || text == QStringLiteral("all")) {
        return QString();
    }
    return text;
}

void MainWindow::updateParamPanel()
{
    if (m_paramTable == nullptr || m_paramCardLabel == nullptr || m_paramChipCombo == nullptr) {
        return;
    }

    if (m_paramInProgress || m_scanInProgress) {
        return;
    }

    // 选中的卡片，未选中时取第一张卡
    QString cardId = selectedCardId();
    if (cardId.isEmpty() && !m_lastResult.cards.isEmpty()) {
        cardId = m_lastResult.cards.first().cardId.trimmed();
    }
    if (cardId.isEmpty()) {
        m_paramCardLabel->setText(QStringLiteral("Card: -"));
        m_paramTable->setRowCount(0);
        m_paramChipCombo->clear();
        return;
    }

    // 芯片列表来自映射表（排除 Mcu）
    QStringList chipIds;
    for (const NpuMappingEntry &mapping : m_lastResult.mappings) {
        if (mapping.cardId.trimmed() != cardId) {
            continue;
        }
        if (mapping.chipName.trimmed().compare(QStringLiteral("Mcu"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (!mapping.chipId.trimmed().isEmpty() && !chipIds.contains(mapping.chipId.trimmed())) {
            chipIds.append(mapping.chipId.trimmed());
        }
    }
    if (chipIds.isEmpty()) {
        chipIds.append(QStringLiteral("0"));
    }

    const int previousBlock = m_paramChipCombo->blockSignals(true);
    m_paramChipCombo->clear();
    m_paramChipCombo->addItem(QStringLiteral("全部芯片"), QStringLiteral("all"));
    for (const QString &chipId : chipIds) {
        m_paramChipCombo->addItem(QStringLiteral("Chip %1").arg(chipId), chipId);
    }
    m_paramChipCombo->blockSignals(previousBlock);

    m_paramCardLabel->setText(QStringLiteral("Card: %1").arg(cardId));

    const QString selectedChip = selectedParamChipId();
    QStringList targetChips = chipIds;
    if (!selectedChip.isEmpty()) {
        targetChips = QStringList{selectedChip};
    }

    m_paramInProgress = true;
    const NpuConnectionOptions options = m_activeConnectionOptions;

    auto *watcher = new QFutureWatcher<QVector<NpuDeviceParam>>(this);
    connect(watcher, &QFutureWatcher<QVector<NpuDeviceParam>>::finished, this, [this, watcher]() {
        const QVector<NpuDeviceParam> params = watcher->result();
        watcher->deleteLater();
        m_paramInProgress = false;

        if (m_paramTable == nullptr) {
            return;
        }

        m_paramTable->clearSpans();
        m_paramTable->setRowCount(params.size());

        for (int row = 0; row < params.size(); ++row) {
            const NpuDeviceParam &param = params.at(row);

            auto *chipItem = new QTableWidgetItem(param.chipId);
            auto *nameItem = new QTableWidgetItem(param.name);
            auto *valueItem = new QTableWidgetItem(
                param.supported ? param.value : QStringLiteral("-"));
            auto *unitItem = new QTableWidgetItem(param.unit);
            auto *rangeItem = new QTableWidgetItem(
                param.editable
                    ? QStringLiteral("%1 - %2").arg(param.minValue).arg(param.maxValue)
                    : QStringLiteral("-"));
            auto *statusItem = new QTableWidgetItem(
                param.supported
                    ? (param.editable ? QStringLiteral("可配置") : QStringLiteral("只读"))
                    : QStringLiteral("不支持"));
            auto *descriptionItem = new QTableWidgetItem(param.description);

            chipItem->setToolTip(param.chipId);
            valueItem->setToolTip(param.value);
            descriptionItem->setToolTip(param.description);

            // 通过用户角色保存参数上下文，供修改参数时使用
            chipItem->setData(Qt::UserRole, param.cardId);
            chipItem->setData(Qt::UserRole + 1, param.chipId);
            chipItem->setData(Qt::UserRole + 2, param.key);
            chipItem->setData(Qt::UserRole + 3, param.minValue);
            chipItem->setData(Qt::UserRole + 4, param.maxValue);
            chipItem->setData(Qt::UserRole + 5, param.supported && param.editable);
            chipItem->setData(Qt::UserRole + 6, param.name);
            chipItem->setData(Qt::UserRole + 7, param.value);

            if (!param.supported) {
                const QBrush mutedBrush(QColor(107, 114, 128));
                valueItem->setForeground(mutedBrush);
                statusItem->setForeground(mutedBrush);
            }

            m_paramTable->setItem(row, 0, chipItem);
            m_paramTable->setItem(row, 1, nameItem);
            m_paramTable->setItem(row, 2, valueItem);
            m_paramTable->setItem(row, 3, unitItem);
            m_paramTable->setItem(row, 4, rangeItem);
            m_paramTable->setItem(row, 5, statusItem);
            m_paramTable->setItem(row, 6, descriptionItem);
        }
    });

    watcher->setFuture(QtConcurrent::run([options, cardId, targetChips]() {
        NpuInfoProvider provider;
        provider.setConnectionOptions(options);
        return provider.collectDeviceParams(cardId, targetChips);
    }));
}

void MainWindow::applySelectedParam()
{
    if (m_paramTable == nullptr || m_paramInProgress) {
        return;
    }

    const int row = m_paramTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("修改参数"), QStringLiteral("请先在参数列表中选择一个参数。"));
        return;
    }

    const QTableWidgetItem *chipItem = m_paramTable->item(row, 0);
    if (chipItem == nullptr) {
        return;
    }

    const QString cardId = chipItem->data(Qt::UserRole).toString();
    const QString chipId = chipItem->data(Qt::UserRole + 1).toString();
    const QString paramKey = chipItem->data(Qt::UserRole + 2).toString();
    const double minValue = chipItem->data(Qt::UserRole + 3).toDouble();
    const double maxValue = chipItem->data(Qt::UserRole + 4).toDouble();
    const bool canEdit = chipItem->data(Qt::UserRole + 5).toBool();
    const QString paramName = chipItem->data(Qt::UserRole + 6).toString();
    const QString currentValue = chipItem->data(Qt::UserRole + 7).toString();

    if (!canEdit) {
        QMessageBox::information(
            this,
            QStringLiteral("修改参数"),
            QStringLiteral("当前参数不支持修改（该芯片不提供此参数或不可配置）。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改参数"));
    dialog.setMinimumWidth(380);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *infoLabel = new QLabel(
        QStringLiteral("Card %1 / Chip %2：%3\n当前值：%4").arg(cardId, chipId, paramName, currentValue),
        &dialog);
    dialogLayout->addWidget(infoLabel);

    auto *valueLayout = new QHBoxLayout();
    valueLayout->addWidget(new QLabel(QStringLiteral("新值："), &dialog));
    auto *valueSpinBox = new QDoubleSpinBox(&dialog);
    valueSpinBox->setDecimals(0);
    valueSpinBox->setRange(minValue, maxValue);
    valueSpinBox->setValue(currentValue.toDouble());
    valueSpinBox->setSuffix(QStringLiteral(" "));
    valueLayout->addWidget(valueSpinBox);
    dialogLayout->addLayout(valueLayout);

    auto *hintLabel = new QLabel(
        QStringLiteral("注意：参数修改将直接影响设备运行状态，请确认后再执行。"),
        &dialog);
    hintLabel->setStyleSheet(QStringLiteral("color: #b45309;"));
    dialogLayout->addWidget(hintLabel);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    auto *applyButton = new QPushButton(QStringLiteral("应用"), &dialog);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);
    dialogLayout->addLayout(buttonLayout);
    connect(applyButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newValue = QString::number(valueSpinBox->value(), 'f', 0);
    const QString commandPreview = QStringLiteral("npu-smi set -t %1 -i %2 -c %3 -d %4")
                                       .arg(paramKey, cardId, chipId, newValue);

    const QMessageBox::StandardButton confirm = QMessageBox::warning(
        this,
        QStringLiteral("确认修改"),
        QStringLiteral("即将执行：\n%1\n\n是否继续？").arg(commandPreview),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    m_paramInProgress = true;
    m_statusLabel->setText(QStringLiteral("正在应用参数修改..."));
    const NpuConnectionOptions options = m_activeConnectionOptions;

    auto *watcher = new QFutureWatcher<NpuParamSetResult>(this);
    connect(watcher, &QFutureWatcher<NpuParamSetResult>::finished, this, [this, watcher]() {
        const NpuParamSetResult result = watcher->result();
        watcher->deleteLater();
        m_paramInProgress = false;

        if (result.success) {
            m_statusLabel->setText(QStringLiteral("参数修改成功：%1").arg(result.command));
        } else {
            m_statusLabel->setText(QStringLiteral("参数修改失败：%1").arg(result.message));
            QMessageBox::critical(
                this,
                QStringLiteral("修改参数"),
                QStringLiteral("参数修改失败：\n%1").arg(result.message));
        }

        updateParamPanel();
    });

    watcher->setFuture(QtConcurrent::run([options, cardId, chipId, paramKey, newValue]() {
        NpuInfoProvider provider;
        provider.setConnectionOptions(options);
        return provider.setDeviceParam(cardId, chipId, paramKey, newValue);
    }));
}

void MainWindow::updateConnectionModeUi()
{
    if (!kRemoteFeatureEnabled) {
        if (m_localModeRadio != nullptr) {
            m_localModeRadio->setChecked(true);
        }
        if (m_remoteModeRadio != nullptr) {
            m_remoteModeRadio->setChecked(false);
            m_remoteModeRadio->setVisible(false);
        }
    }

    const bool remoteMode = kRemoteFeatureEnabled && m_remoteModeRadio != nullptr && m_remoteModeRadio->isChecked();

    if (m_userLabel != nullptr) {
        m_userLabel->setVisible(remoteMode);
    }
    if (m_userEdit != nullptr) {
        m_userEdit->setVisible(remoteMode);
    }
    if (m_hostLabel != nullptr) {
        m_hostLabel->setVisible(remoteMode);
    }
    if (m_hostEdit != nullptr) {
        m_hostEdit->setVisible(remoteMode);
    }
    if (m_passwordLabel != nullptr) {
        m_passwordLabel->setVisible(remoteMode);
    }
    if (m_passwordEdit != nullptr) {
        m_passwordEdit->setVisible(remoteMode);
    }
    if (m_remoteConnectButton != nullptr) {
        m_remoteConnectButton->setVisible(remoteMode);
    }
}

NpuConnectionOptions MainWindow::currentConnectionOptions() const
{
    NpuConnectionOptions options;
    options.useSsh = kRemoteFeatureEnabled && m_remoteModeRadio != nullptr && m_remoteModeRadio->isChecked();
    options.user = m_userEdit != nullptr ? m_userEdit->text().trimmed() : QString();
    options.host = m_hostEdit != nullptr ? m_hostEdit->text().trimmed() : QString();
    options.password = m_passwordEdit != nullptr ? m_passwordEdit->text() : QString();
    return options;
}

void MainWindow::resetMonitorSeries()
{
    for (QLineSeries *series : std::as_const(m_tempSeriesByCore)) {
        if (series != nullptr && m_tempChart != nullptr) {
            m_tempChart->removeSeries(series);
            delete series;
        }
    }
    m_tempSeriesByCore.clear();

    for (QLineSeries *series : std::as_const(m_memorySeriesByCore)) {
        if (series != nullptr && m_memoryChart != nullptr) {
            m_memoryChart->removeSeries(series);
            delete series;
        }
    }
    m_memorySeriesByCore.clear();

    for (QLineSeries *series : std::as_const(m_powerSeriesByCard)) {
        if (series != nullptr && m_powerChart != nullptr) {
            m_powerChart->removeSeries(series);
            delete series;
        }
    }
    m_powerSeriesByCard.clear();

    if (m_aicoreSeries != nullptr) {
        m_aicoreSeries->clear();
    }
    if (m_aivectorSeries != nullptr) {
        m_aivectorSeries->clear();
    }
    if (m_aicpuSeries != nullptr) {
        m_aicpuSeries->clear();
    }

    m_sampleIndex = 0;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress
        && !isMaximized()
        && windowHandle() != nullptr) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
#else
            const QPoint globalPosition = mouseEvent->globalPos();
#endif
            const QRect frame = frameGeometry();
            constexpr int resizeMargin = 6;
            Qt::Edges edges;
            if (globalPosition.x() <= frame.left() + resizeMargin) {
                edges |= Qt::LeftEdge;
            } else if (globalPosition.x() >= frame.right() - resizeMargin) {
                edges |= Qt::RightEdge;
            }
            if (globalPosition.y() <= frame.top() + resizeMargin) {
                edges |= Qt::TopEdge;
            } else if (globalPosition.y() >= frame.bottom() - resizeMargin) {
                edges |= Qt::BottomEdge;
            }
            if (edges != Qt::Edges() && windowHandle()->startSystemResize(edges)) {
                return true;
            }
        }
    }

    const auto *titleBarWidget = qobject_cast<const QWidget *>(watched);
    const bool isTitleBarEvent =
        titleBarWidget != nullptr
        && m_titleBar != nullptr
        && (titleBarWidget == m_titleBar || m_titleBar->isAncestorOf(titleBarWidget))
        && qobject_cast<const QToolButton *>(watched) == nullptr;

    if (isTitleBarEvent) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const auto globalPosition = [mouseEvent]() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            return mouseEvent->globalPosition().toPoint();
#else
            return mouseEvent->globalPos();
#endif
        };

        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (mouseEvent->button() == Qt::LeftButton) {
                m_draggingTitleBar = true;
                m_titleBarDragOffset = globalPosition() - frameGeometry().topLeft();
                return true;
            }
            break;
        case QEvent::MouseMove:
            if (m_draggingTitleBar && (mouseEvent->buttons() & Qt::LeftButton)) {
                move(globalPosition() - m_titleBarDragOffset);
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (mouseEvent->button() == Qt::LeftButton) {
                m_draggingTitleBar = false;
                return true;
            }
            break;
        case QEvent::MouseButtonDblClick:
            if (mouseEvent->button() == Qt::LeftButton) {
                isMaximized() ? showNormal() : showMaximized();
                return true;
            }
            break;
        default:
            break;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *clickedWidget = qobject_cast<QWidget *>(watched);

        const auto clickedInTable = [clickedWidget](QTableWidget *table) -> bool {
            if (table == nullptr || clickedWidget == nullptr) {
                return false;
            }
            return clickedWidget == table || table->isAncestorOf(clickedWidget);
        };

        const bool insideAnyTable = clickedInTable(m_cardTable)
            || clickedInTable(m_mappingTable)
            || clickedInTable(m_processTable);

        if (!insideAnyTable) {
            if (m_cardTable != nullptr) {
                m_cardTable->clearSelection();
                m_cardTable->setCurrentCell(-1, -1);
            }
            if (m_mappingTable != nullptr) {
                m_mappingTable->clearSelection();
                m_mappingTable->setCurrentCell(-1, -1);
            }
            if (m_processTable != nullptr) {
                m_processTable->clearSelection();
                m_processTable->setCurrentCell(-1, -1);
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::selectedCardId() const
{
    const int row = m_cardTable->currentRow();
    if (row >= 0) {
        const QTableWidgetItem *item = m_cardTable->item(row, 0);
        if (item != nullptr) {
            return item->text().trimmed();
        }
    }

    const auto selectedItems = m_cardTable->selectedItems();
    if (!selectedItems.isEmpty()) {
        return selectedItems.first()->text().trimmed();
    }

    return QString();
}

QStringList MainWindow::currentCardIds() const
{
    QStringList ids;
    for (const NpuCard &card : m_lastResult.cards) {
        if (!card.cardId.trimmed().isEmpty()) {
            ids.append(card.cardId.trimmed());
        }
    }
    ids.removeDuplicates();
    return ids;
}

void MainWindow::appendSeriesPoint(QLineSeries *series, qint64 timestampMs, double y)
{
    if (series == nullptr) {
        return;
    }

    series->append(static_cast<qreal>(timestampMs), y);
    while (series->count() > m_maxSamples) {
        series->remove(0);
    }
}

QLineSeries *MainWindow::ensureSeries(
    QHash<QString, QLineSeries *> *seriesMap,
    QChart *chart,
    QDateTimeAxis *axisX,
    QValueAxis *axisY,
    const QString &key,
    const QString &label,
    const QColor &color)
{
    if (chart == nullptr || axisX == nullptr || axisY == nullptr) {
        return nullptr;
    }

    if (seriesMap != nullptr && seriesMap->contains(key)) {
        return seriesMap->value(key);
    }

    auto *series = new QLineSeries(chart);
    series->setName(label);
    if (color.isValid()) {
        QPen pen(color);
        pen.setWidth(2);
        series->setPen(pen);
    }

    chart->addSeries(series);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    if (seriesMap != nullptr) {
        seriesMap->insert(key, series);
    }

    return series;
}