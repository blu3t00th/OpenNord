#pragma once

#include "gui/RpcClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QSystemTrayIcon;

namespace opennord {

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createLoginPage();
    QWidget *createHomePage();
    QWidget *createLocationsPage();
    QWidget *createSettingsPage();
    QWidget *createAccountPage();
    QWidget *createDependencyPage();
    QWidget *createSidebar();
    void applyTheme();
    void refreshStatus();
    void applyStatus(const QJsonObject &state);
    void loadLocations(bool force = false);
    void loadSettings();
    void loadAccount();
    void showError(const QString &message);
    void setBusy(bool busy, QString text = {});
    void updateLocationTable();
    void updateAutoStart(bool enabled);
    void updateTechnologyControls();
    void updateDependencyPage();
    void setupTrayIcon();
    void runElevatedServiceCommand(const QString &command, bool enableAutoStart);
    [[nodiscard]] bool selectedEngineReady() const;

    RpcClient rpc_;
    QStackedWidget *pages_{};
    QListWidget *navigation_{};
    QTimer *statusTimer_{};
    bool statusInFlight_{};
    bool authenticated_{};
    bool wireGuardReady_{};
    bool openVpnReady_{};
    bool autoConnectAttempted_{};
    bool exiting_{};
    QString connectionStatus_;
    QString selectedTechnology_{QStringLiteral("nordlynx")};
    QString selectedOpenVpnProtocol_{QStringLiteral("udp")};
    QJsonArray locations_;
    QSystemTrayIcon *trayIcon_{};

    QLineEdit *tokenInput_{};
    QPushButton *loginButton_{};
    QLabel *loginServiceError_{};
    QLabel *homeEyebrow_{};
    QLabel *homeTitle_{};
    QLabel *homeDescription_{};
    QLabel *homeServer_{};
    QLabel *homeError_{};
    QPushButton *powerButton_{};
    QLineEdit *serverSearch_{};
    QTableWidget *serverTable_{};
    QLabel *locationCount_{};
    QCheckBox *autoConnect_{};
    QCheckBox *launchAtStartup_{};
    QCheckBox *killSwitch_{};
    QCheckBox *allowLan_{};
    QComboBox *technology_{};
    QComboBox *openVpnProtocol_{};
    QLabel *policyNote_{};
    QLineEdit *preferredCountry_{};
    QLineEdit *dnsServers_{};
    QLabel *accountName_{};
    QLabel *diagnostics_{};
    QLabel *dependencyHeading_{};
    QLabel *dependencyDescription_{};
    QLabel *dependencyPath_{};
    QPushButton *dependencyInstall_{};
};

}
