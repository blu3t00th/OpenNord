#include "gui/MainWindow.h"
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

namespace opennord {
class MainWindowTests final : public QObject
{
    Q_OBJECT
    static QJsonObject state(const QString &status)
    {
        return {{QStringLiteral("authenticated"), true}, {QStringLiteral("wireGuardReady"), true},
            {QStringLiteral("openVpnReady"), true}, {QStringLiteral("status"), status},
            {QStringLiteral("server"), QJsonObject{{QStringLiteral("hostname"), QStringLiteral("se123.nordvpn.com")},
                {QStringLiteral("city"), QStringLiteral("Stockholm")}, {QStringLiteral("country"), QStringLiteral("Sweden")}}}};
    }
private slots:
    void pollingDoesNotUnlockPendingConnection()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        QVERIFY(!window.statusTimer_->isActive());
        window.applyStatus(state(QStringLiteral("disconnected")));
        QVERIFY(window.powerButton_->isEnabled());
        window.setBusy(true, QStringLiteral("Connecting…"));
        window.applyStatus(state(QStringLiteral("disconnected")));
        QVERIFY(!window.powerButton_->isEnabled());
        QCOMPARE(window.powerButton_->text(), QStringLiteral("Connecting…"));
        window.setBusy(false);
        window.applyStatus(state(QStringLiteral("connected")));
        QVERIFY(window.powerButton_->isEnabled());
        QCOMPARE(window.powerButton_->text(), QStringLiteral("Disconnect"));
    }
    void serviceLossInvalidatesProtection()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("connected")));
        window.showServiceUnavailable(QStringLiteral("Service stopped"));
        QCOMPARE(window.homeStatus_->text(), QStringLiteral("STATUS UNAVAILABLE"));
        QCOMPARE(window.homeRoute_->text(), QStringLiteral("Not verified"));
        QVERIFY(!window.powerButton_->isEnabled());
        QVERIFY(!window.saveSettingsButton_->isEnabled());
        QVERIFY(window.homeDescription_->text().contains(QStringLiteral("could not be verified")));
        window.applyStatus(state(QStringLiteral("disconnected")));
        QVERIFY(window.powerButton_->isEnabled());
        QCOMPARE(window.homeRoute_->text(), QStringLiteral("No active VPN tunnel"));
    }
    void unknownStateCannotConnect()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("unexpected")));
        QVERIFY(!window.powerButton_->isEnabled());
        QCOMPARE(window.homeStatus_->text(), QStringLiteral("STATUS UNKNOWN"));
    }
    void locationFilterClearsStaleSelection()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("disconnected")));
        window.locations_ = {QJsonObject{{QStringLiteral("country"), QStringLiteral("Sweden")},
            {QStringLiteral("countryCode"), QStringLiteral("SE")}, {QStringLiteral("city"), QStringLiteral("Stockholm")},
            {QStringLiteral("countryId"), 208}, {QStringLiteral("cityId"), 1}, {QStringLiteral("serverCount"), 12}}};
        window.updateLocationTable();
        QVERIFY(window.locationConnectButton_->isEnabled());
        window.serverSearch_->setText(QStringLiteral("no matching city"));
        QCOMPARE(window.serverTable_->rowCount(), 0);
        QVERIFY(!window.locationConnectButton_->isEnabled());
        window.serverSearch_->clear();
        QCOMPARE(window.serverTable_->rowCount(), 1);
        QVERIFY(window.locationConnectButton_->isEnabled());
    }
    void renderPreviews()
    {
        const auto directory = qEnvironmentVariable("OPENNORD_PREVIEW_DIR");
        if (directory.isEmpty()) return;
        QVERIFY(QDir().mkpath(directory));
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("login.png"))));
        window.applyStatus(state(QStringLiteral("disconnected")));
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("home.png"))));
        window.applyStatus(state(QStringLiteral("connected")));
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("connected-demo.png"))));
        window.pages_->setCurrentIndex(4);
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("settings.png"))));
        window.resize(960, 640);
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("settings-small.png"))));
    }
};
}
QTEST_MAIN(opennord::MainWindowTests)
#include "GuiTests.moc"
