#include "gui/MainWindow.h"
#include "gui/GraphiteMap.h"
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
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
                {QStringLiteral("city"), QStringLiteral("Stockholm")}, {QStringLiteral("country"), QStringLiteral("Sweden")},
                {QStringLiteral("countryCode"), QStringLiteral("SE")}}}};
    }
    static QJsonArray locations()
    {
        QJsonArray result;
        const QStringList countries{QStringLiteral("Sweden"), QStringLiteral("Norway"), QStringLiteral("Germany"), QStringLiteral("Netherlands")};
        const QStringList cities{QStringLiteral("Stockholm"), QStringLiteral("Oslo"), QStringLiteral("Frankfurt"), QStringLiteral("Amsterdam")};
        const QStringList codes{QStringLiteral("SE"), QStringLiteral("NO"), QStringLiteral("DE"), QStringLiteral("NL")};
        for (int i = 0; i < countries.size(); ++i) result.append(QJsonObject{
            {QStringLiteral("country"), countries[i]}, {QStringLiteral("city"), cities[i]},
            {QStringLiteral("countryCode"), codes[i]}, {QStringLiteral("countryId"), i + 1},
            {QStringLiteral("cityId"), i + 10}, {QStringLiteral("serverCount"), 12}});
        return result;
    }
private slots:
    void chosenLocationSurvivesFilteringAndStatusPolls()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("disconnected")));
        window.locations_ = locations();
        window.updateLocationTable();
        QCOMPARE(window.locationTiles_.size(), 4);
        window.locationTiles_.first()->click();
        QCOMPARE(window.selectedLocation_.value(QStringLiteral("countryCode")).toString(), QStringLiteral("SE"));
        QCOMPARE(window.homeLocation_->currentIndex(), 1);
        window.homeSearch_->setText(QStringLiteral("Norway"));
        QCOMPARE(window.locationTiles_.size(), 1);
        window.applyStatus(state(QStringLiteral("disconnected")));
        QCOMPARE(window.selectedLocation_.value(QStringLiteral("countryCode")).toString(), QStringLiteral("SE"));
        QCOMPARE(window.homeLocation_->currentData().toJsonObject().value(QStringLiteral("cityId")).toInt(), 10);
        QVERIFY(window.connectionArt_->accessibleDescription().contains(QStringLiteral("Sweden")));
        window.setBusy(true, QStringLiteral("Connecting…"));
        QVERIFY(!window.homeLocation_->isEnabled());
        QVERIFY(!window.locationTiles_.first()->isEnabled());
        window.setBusy(false);
        window.applyStatus(state(QStringLiteral("connected")));
        QVERIFY(!window.homeLocation_->isEnabled());
        QCOMPARE(window.powerButton_->text(), QStringLiteral("Disconnect"));
    }
    void protocolChangeInvalidatesOldLocationChoices()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("disconnected")));
        window.locations_ = locations();
        window.updateLocationTable();
        window.homeLocation_->setCurrentIndex(1);
        auto changed = state(QStringLiteral("disconnected"));
        changed.insert(QStringLiteral("technology"), QStringLiteral("openvpn"));
        window.applyStatus(changed);
        QVERIFY(window.selectedLocation_.isEmpty());
        QVERIFY(window.locations_.isEmpty());
        QCOMPARE(window.homeLocation_->currentIndex(), 0);
    }
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
#ifdef Q_OS_WIN
        // Qt's offscreen platform has no native Windows font discovery.
        const auto fonts = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts"));
        for (const auto &file : {QStringLiteral("segoeui.ttf"), QStringLiteral("segoeuib.ttf"), QStringLiteral("seguisb.ttf")})
            QFontDatabase::addApplicationFont(QDir(fonts).filePath(file));
#endif
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("login.png"))));
        window.applyStatus(state(QStringLiteral("disconnected")));
        window.locations_ = locations();
        window.updateLocationTable();
        window.homeLocation_->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("home.png"))));
        window.resize(960, 640);
        QTest::qWait(30);
        QVERIFY(window.powerButton_->mapTo(&window, QPoint()).y() + window.powerButton_->height()
            <= window.homeSearch_->mapTo(&window, QPoint()).y());
        QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("home-small.png"))));
        window.resize(1280, 820);
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
