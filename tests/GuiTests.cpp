#include "gui/MainWindow.h"
#include "gui/GraphiteMap.h"
#include "gui/FlagIcons.h"
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>
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
    void completeCountryFlagCoverage()
    {
        // Full Nord countries response recorded 2026-09-23, not just visible rows.
        const auto catalog = QStringLiteral(
            "AD AE AF AG AL AM AO AR AT AU AZ BA BB BD BE BF BG BH BJ BM "
            "BN BO BR BS BT BW BZ CA CH CL CO CR CV CY CZ DE DK DO DZ EC "
            "EE EG ES ET FI FJ FR GB GE GH GL GM GR GT GU GY HK HN HR HU "
            "ID IE IL IM IN IQ IS IT JE JM JO JP KE KG KH KM KR KW KY KZ "
            "LA LB LC LI LK LR LT LU LV LY MA MC MD ME MG MK MM MN MR MT "
            "MU MV MW MX MY MZ NG NL NO NP NZ PA PE PG PH PK PL PR PT PY "
            "QA RO RS SC SE SG SI SK SL SN SO SR SV TD TH TJ TN TR TT TW "
            "TZ UA US UY UZ VE VN YE ZA ZM").split(u' ');
        QCOMPARE(catalog.size(), 150);
        for (const auto &code : catalog) QVERIFY2(hasCountryFlag(code), qPrintable(code));
        const auto files = QDir(QStringLiteral(":/flags")).entryList({QStringLiteral("*.png")}, QDir::Files);
        QCOMPARE(files.size(), 256);
        const auto fallback = countryFlagIcon(QStringLiteral("ZZ")).pixmap(32, 32).toImage();
        for (const auto &file : files) {
            const auto code = file.left(2);
            const QPixmap asset(QStringLiteral(":/flags/") + file);
            QVERIFY2(!asset.isNull(), qPrintable(file));
            QCOMPARE(asset.size(), QSize(128, 128));
            QVERIFY2(countryFlagIcon(code).pixmap(32, 32).toImage() != fallback, qPrintable(code));
        }
        QCOMPARE(countryFlagIcon(QStringLiteral(" uk ")).cacheKey(), countryFlagIcon(QStringLiteral("GB")).cacheKey());
        QCOMPARE(countryFlagIcon(QStringLiteral("EL")).cacheKey(), countryFlagIcon(QStringLiteral("GR")).cacheKey());
        QVERIFY(!hasCountryFlag(QStringLiteral("../SE")));
        QVERIFY(!hasCountryFlag(QString{}));
    }
    void flagsAppearInDropdownAndFilteredTable()
    {
        MainWindow window(nullptr, MainWindow::ServiceMode::Preview);
        window.applyStatus(state(QStringLiteral("disconnected")));
        window.locations_ = {QJsonObject{{QStringLiteral("country"), QStringLiteral("Gambia")},
            {QStringLiteral("countryCode"), QStringLiteral("GM")}, {QStringLiteral("city"), QStringLiteral("Banjul")},
            {QStringLiteral("countryId"), 1}, {QStringLiteral("cityId"), 2}, {QStringLiteral("serverCount"), 1}},
            QJsonObject{{QStringLiteral("country"), QStringLiteral("Guam")},
            {QStringLiteral("countryCode"), QStringLiteral("GU")}, {QStringLiteral("city"), QStringLiteral("Hagatna")},
            {QStringLiteral("countryId"), 3}, {QStringLiteral("cityId"), 4}, {QStringLiteral("serverCount"), 1}}};
        window.updateLocationTable();
        QCOMPARE(window.homeLocation_->itemIcon(1).cacheKey(), countryFlagIcon(QStringLiteral("GM")).cacheKey());
        QCOMPARE(window.serverTable_->item(0, 0)->icon().cacheKey(), countryFlagIcon(QStringLiteral("GM")).cacheKey());
        window.serverSearch_->setText(QStringLiteral("Guam"));
        QCOMPARE(window.serverTable_->rowCount(), 1);
        QCOMPARE(window.serverTable_->item(0, 0)->icon().cacheKey(), countryFlagIcon(QStringLiteral("GU")).cacheKey());
        QCOMPARE(window.homeLocation_->count(), 3);
    }
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
        const auto flags = QDir(QStringLiteral(":/flags")).entryList({QStringLiteral("*.png")}, QDir::Files);
        QImage sheet(960, ((flags.size() + 15) / 16) * 64, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(QColor(QStringLiteral("#15191a")));
        QPainter painter(&sheet);
        painter.setPen(QColor(QStringLiteral("#e6eddf")));
        QFont font(QStringLiteral("Segoe UI")); font.setPixelSize(10); painter.setFont(font);
        for (int i = 0; i < flags.size(); ++i) {
            const auto code = flags[i].left(2);
            const int x = (i % 16) * 60, y = (i / 16) * 64;
            countryFlagIcon(code).paint(&painter, QRect(x + 14, y + 4, 32, 32));
            painter.drawText(QRect(x, y + 39, 60, 18), Qt::AlignCenter, code.toUpper());
        }
        painter.end();
        QVERIFY(sheet.save(QDir(directory).filePath(QStringLiteral("all-country-flags.png"))));
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
