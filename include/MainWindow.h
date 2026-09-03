#pragma once

#include "NpuInfoProvider.h"

#include <QChart>
#include <QChartView>
#include <QButtonGroup>
#include <QDateTimeAxis>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QValueAxis>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshData();
    void updateSelectedCardMappings();
    void updateMonitorCharts();
    void updateProcessPanel();
    void showChartsPage();
    void showProcessPage();
    void updateConnectionModeUi();

private:
    void setupUi();
    void setupMonitorPanel();
    void setupProcessPanel();
    NpuConnectionOptions currentConnectionOptions() const;
    void resetMonitorSeries();
    void setupChart(
        QChart **chart,
        QChartView **view,
        QDateTimeAxis **axisX,
        QValueAxis **axisY,
        const QString &title,
        const QString &yAxisLabel,
        double yMin,
        double yMax);
    void populateCards(const QVector<NpuCard> &cards);
    void populateMappings(const QVector<NpuMappingEntry> &mappings);
    QString selectedCardId() const;
    QStringList currentCardIds() const;
    void appendSeriesPoint(QLineSeries *series, qint64 timestampMs, double y);
    QLineSeries *ensureSeries(
        QHash<QString, QLineSeries *> *seriesMap,
        QChart *chart,
        QDateTimeAxis *axisX,
        QValueAxis *axisY,
        const QString &key,
        const QString &label,
        const QColor &color = QColor());

    NpuInfoProvider m_provider;
    NpuScanResult m_lastResult;

    QTableWidget *m_cardTable = nullptr;
    QTableWidget *m_mappingTable = nullptr;
    QWidget *m_monitorPanel = nullptr;
    QStackedWidget *m_monitorStack = nullptr;
    QWidget *m_chartPage = nullptr;
    QWidget *m_processPage = nullptr;
    QPushButton *m_chartViewButton = nullptr;
    QPushButton *m_processViewButton = nullptr;
    QButtonGroup *m_viewButtonGroup = nullptr;
    QTableWidget *m_processTable = nullptr;
    QLabel *m_processSummaryLabel = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_updatedLabel = nullptr;
    QLabel *m_npuSmiVersionLabel = nullptr;
    QRadioButton *m_localModeRadio = nullptr;
    QRadioButton *m_remoteModeRadio = nullptr;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLabel *m_hostLabel = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLabel *m_passwordLabel = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QPushButton *m_remoteConnectButton = nullptr;

    QTimer *m_monitorTimer = nullptr;
    int m_sampleIndex = 0;
    int m_maxSamples = 120;

    QChart *m_tempChart = nullptr;
    QChart *m_memoryChart = nullptr;
    QChart *m_powerChart = nullptr;
    QChart *m_usageChart = nullptr;
    QChartView *m_tempChartView = nullptr;
    QChartView *m_memoryChartView = nullptr;
    QChartView *m_powerChartView = nullptr;
    QChartView *m_usageChartView = nullptr;
    QDateTimeAxis *m_tempAxisX = nullptr;
    QValueAxis *m_tempAxisY = nullptr;
    QDateTimeAxis *m_memoryAxisX = nullptr;
    QValueAxis *m_memoryAxisY = nullptr;
    QDateTimeAxis *m_powerAxisX = nullptr;
    QValueAxis *m_powerAxisY = nullptr;
    QDateTimeAxis *m_usageAxisX = nullptr;
    QValueAxis *m_usageAxisY = nullptr;

    QHash<QString, QLineSeries *> m_tempSeriesByCore;
    QHash<QString, QLineSeries *> m_memorySeriesByCore;
    QHash<QString, QLineSeries *> m_powerSeriesByCard;
    QLineSeries *m_aicoreSeries = nullptr;
    QLineSeries *m_aivectorSeries = nullptr;
    QLineSeries *m_aicpuSeries = nullptr;

    NpuConnectionOptions m_activeConnectionOptions;
    bool m_scanInProgress = false;
    bool m_monitorInProgress = false;
    bool m_processInProgress = false;
    bool m_monitorRefreshPending = false;
    QString m_selectedMonitorCardId;
};
