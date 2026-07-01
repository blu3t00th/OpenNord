#include "gui/MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>
#include <shellapi.h>

namespace opennord {
namespace {

enum Page { LoginPage, DependencyPage, HomePage, LocationsPage, SettingsPage, AccountPage };

QLabel *eyebrow(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("eyebrow"));
    return label;
}

QLabel *title(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageTitle"));
    label->setWordWrap(true);
    return label;
}

QLabel *body(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("bodyText"));
    label->setWordWrap(true);
    return label;
}

class NetworkArt final : public QWidget
{
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(8, 25, 36));
        painter.setPen(QPen(QColor(99, 230, 207, 25), 1));
        constexpr int spacing = 34;
        for (int x = 0; x < width(); x += spacing) painter.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += spacing) painter.drawLine(0, y, width(), y);
        const auto diameter = qMin(width(), height()) * 0.58;
        const QRectF globe((width() - diameter) / 2.0, height() * 0.12, diameter, diameter);
        painter.setPen(QPen(QColor(99, 230, 207, 95), 1.4));
        painter.drawEllipse(globe);
        painter.drawEllipse(QRectF(globe.center().x() - diameter * .18, globe.top(), diameter * .36, diameter));
        painter.drawEllipse(QRectF(globe.left(), globe.center().y() - diameter * .18, diameter, diameter * .36));
        painter.setBrush(QColor(255, 180, 92));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(globe.center().x() + diameter * .2, globe.center().y() - diameter * .21), 5, 5);
        painter.setPen(QColor(235, 244, 244));
        QFont font(QStringLiteral("Bahnschrift SemiCondensed"), 27, QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(QRectF(42, height() - 115, width() - 84, 100), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Private routes.\nPublic code."));
    }
};

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), rpc_(this)
{
    setWindowTitle(QStringLiteral("OpenNord"));
    resize(1180, 760);
    setMinimumSize(960, 640);
    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createSidebar());
    pages_ = new QStackedWidget;
    pages_->addWidget(createLoginPage());
    pages_->addWidget(createDependencyPage());
    pages_->addWidget(createHomePage());
    pages_->addWidget(createLocationsPage());
    pages_->addWidget(createSettingsPage());
    pages_->addWidget(createAccountPage());
    layout->addWidget(pages_, 1);
    setCentralWidget(central);
    applyTheme();
    setupTrayIcon();

    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !authenticated_) return;
        if (row < 2 && !selectedEngineReady()) {
            updateDependencyPage();
            pages_->setCurrentIndex(DependencyPage);
            return;
        }
        pages_->setCurrentIndex(HomePage + row);
        if (row == 1) loadLocations();
        if (row == 2) loadSettings();
        if (row == 3) loadAccount();
    });
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(1000);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    statusTimer_->start();
    refreshStatus();
}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#63e6cf")));
    painter.drawEllipse(3, 3, 58, 58);
    painter.setPen(QColor(QStringLiteral("#05201d")));
    auto font = painter.font();
    font.setBold(true);
    font.setPixelSize(31);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("N"));
    const QIcon icon(pixmap);
    setWindowIcon(icon);

    trayIcon_ = new QSystemTrayIcon(icon, this);
    trayIcon_->setToolTip(QStringLiteral("OpenNord VPN service"));
    auto *menu = new QMenu(this);
    auto *openAction = menu->addAction(QStringLiteral("Open OpenNord"));
    menu->addSeparator();
    auto *startAction = menu->addAction(QStringLiteral("Start service"));
    auto *restartAction = menu->addAction(QStringLiteral("Restart service"));
    auto *stopAction = menu->addAction(QStringLiteral("Stop service"));
    menu->addSeparator();
    auto *exitAction = menu->addAction(QStringLiteral("Exit OpenNord"));
    trayIcon_->setContextMenu(menu);

    const auto showWindow = [this] {
        showNormal();
        raise();
        activateWindow();
    };
    connect(openAction, &QAction::triggered, this, showWindow);
    connect(startAction, &QAction::triggered, this, [this] { runElevatedServiceCommand(QStringLiteral("start"), true); });
    connect(restartAction, &QAction::triggered, this, [this] { runElevatedServiceCommand(QStringLiteral("restart"), true); });
    connect(stopAction, &QAction::triggered, this, [this] { runElevatedServiceCommand(QStringLiteral("stop"), false); });
    connect(exitAction, &QAction::triggered, this, [this] {
        exiting_ = true;
        trayIcon_->hide();
        qApp->quit();
    });
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [showWindow](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    trayIcon_->show();
}

void MainWindow::runElevatedServiceCommand(const QString &command, bool enableAutoStart)
{
    const auto servicePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("OpenNordService.exe"));
    if (!QFileInfo::exists(servicePath)) {
        showError(QStringLiteral("OpenNordService.exe is missing. Please reinstall OpenNord as administrator."));
        return;
    }
    rpc_.setServiceAutoStartEnabled(enableAutoStart);
    const auto nativePath = QDir::toNativeSeparators(servicePath);
    const auto arguments = QStringLiteral("--%1").arg(command);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"runas", reinterpret_cast<LPCWSTR>(nativePath.utf16()),
        reinterpret_cast<LPCWSTR>(arguments.utf16()), nullptr, SW_HIDE));
    if (result <= 32) {
        rpc_.setServiceAutoStartEnabled(true);
        showError(QStringLiteral("Could not run the service command with administrator permission. Windows error %1.").arg(result));
        return;
    }
    if (trayIcon_) trayIcon_->showMessage(QStringLiteral("OpenNord"),
        QStringLiteral("Service %1 requested.").arg(command), QSystemTrayIcon::Information, 2500);
    QTimer::singleShot(1500, this, &MainWindow::refreshStatus);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (exiting_ || !trayIcon_ || !trayIcon_->isVisible()) {
        event->accept();
        return;
    }
    hide();
    trayIcon_->showMessage(QStringLiteral("OpenNord"),
        QStringLiteral("OpenNord is still running in the notification area."),
        QSystemTrayIcon::Information, 2500);
    event->ignore();
}

QWidget *MainWindow::createSidebar()
{
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(205);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(18, 25, 18, 20);
    auto *brand = new QLabel(QStringLiteral("◯  OpenNord"));
    brand->setObjectName(QStringLiteral("brand"));
    layout->addWidget(brand);
    navigation_ = new QListWidget;
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("Overview"), QStringLiteral("Locations"), QStringLiteral("Settings"), QStringLiteral("Account")});
    navigation_->setCurrentRow(0);
    navigation_->setVisible(false);
    layout->addWidget(navigation_, 1);
    auto *privacy = new QLabel(QStringLiteral("◇  OPEN BY DESIGN\n    GPLv3 · No telemetry"));
    privacy->setObjectName(QStringLiteral("privacy"));
    layout->addWidget(privacy);
    return sidebar;
}

QWidget *MainWindow::createLoginPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *art = new NetworkArt;
    art->setMinimumWidth(370);
    layout->addWidget(art, 5);
    auto *panel = new QWidget;
    auto *form = new QVBoxLayout(panel);
    form->setContentsMargins(65, 70, 65, 60);
    form->addStretch();
    form->addWidget(eyebrow(QStringLiteral("UNOFFICIAL OPEN-SOURCE CLIENT")));
    form->addSpacing(14);
    form->addWidget(title(QStringLiteral("Connect your\nNord account.")));
    form->addWidget(body(QStringLiteral("Generate an access token in Nord Account. The service validates it with Nord's signed API and encrypts it locally with Windows DPAPI.")));
    loginServiceError_ = new QLabel;
    loginServiceError_->setObjectName(QStringLiteral("errorBanner"));
    loginServiceError_->setWordWrap(true);
    loginServiceError_->hide();
    form->addWidget(loginServiceError_);
    form->addSpacing(20);
    auto *label = new QLabel(QStringLiteral("Access token"));
    label->setObjectName(QStringLiteral("fieldLabel"));
    form->addWidget(label);
    tokenInput_ = new QLineEdit;
    tokenInput_->setEchoMode(QLineEdit::Password);
    tokenInput_->setPlaceholderText(QStringLiteral("Paste lowercase hexadecimal token"));
    tokenInput_->setMinimumHeight(48);
    form->addWidget(tokenInput_);
    loginButton_ = new QPushButton(QStringLiteral("Continue securely"));
    loginButton_->setObjectName(QStringLiteral("primary"));
    loginButton_->setMinimumHeight(46);
    form->addWidget(loginButton_);
    auto *tokenLink = new QPushButton(QStringLiteral("Generate a token in Nord Account ↗"));
    tokenLink->setObjectName(QStringLiteral("linkButton"));
    form->addWidget(tokenLink);
    form->addWidget(body(QStringLiteral("OpenNord is community software and is not endorsed by Nord Security.")));
    form->addStretch();
    layout->addWidget(panel, 6);
    connect(tokenLink, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://my.nordaccount.com/dashboard/nordvpn/manual-configuration/")));
    });
    connect(loginButton_, &QPushButton::clicked, this, [this] {
        const auto token = tokenInput_->text().trimmed();
        if (token.isEmpty()) return showError(QStringLiteral("Enter an access token."));
        setBusy(true, QStringLiteral("Verifying…"));
        rpc_.call(QStringLiteral("login"), {{QStringLiteral("token"), token}}, [this](QJsonValue, QString error) {
            tokenInput_->clear();
            setBusy(false);
            if (!error.isEmpty()) return showError(error);
            refreshStatus();
        });
    });
    return page;
}

QWidget *MainWindow::createDependencyPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(100, 80, 100, 80);
    layout->addStretch();
    layout->addWidget(eyebrow(QStringLiteral("REQUIRED OPEN-SOURCE DRIVER")), 0, Qt::AlignHCenter);
    dependencyHeading_ = title(QStringLiteral("VPN engine required"));
    dependencyHeading_->setAlignment(Qt::AlignCenter);
    layout->addWidget(dependencyHeading_);
    dependencyDescription_ = body(QString{});
    dependencyDescription_->setAlignment(Qt::AlignCenter);
    layout->addWidget(dependencyDescription_);
    dependencyPath_ = body(QString{});
    dependencyPath_->setAlignment(Qt::AlignCenter);
    layout->addWidget(dependencyPath_);
    dependencyInstall_ = new QPushButton;
    dependencyInstall_->setObjectName(QStringLiteral("primary"));
    dependencyInstall_->setFixedWidth(240);
    layout->addWidget(dependencyInstall_, 0, Qt::AlignHCenter);
    auto *retry = new QPushButton(QStringLiteral("Check again"));
    retry->setObjectName(QStringLiteral("secondary"));
    retry->setFixedWidth(220);
    layout->addWidget(retry, 0, Qt::AlignHCenter);
    layout->addStretch();
    connect(dependencyInstall_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(selectedTechnology_ == QStringLiteral("openvpn")
            ? QStringLiteral("https://openvpn.net/community-downloads/")
            : QStringLiteral("https://www.wireguard.com/install/")));
    });
    connect(retry, &QPushButton::clicked, this, &MainWindow::refreshStatus);
    return page;
}

QWidget *MainWindow::createHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(70, 60, 70, 45);
    homeEyebrow_ = eyebrow(QStringLiteral("NORDLYNX · WIREGUARDNT"));
    layout->addWidget(homeEyebrow_);
    layout->addSpacing(18);
    homeTitle_ = title(QStringLiteral("Ready when you are"));
    layout->addWidget(homeTitle_);
    homeDescription_ = body(QStringLiteral("One action selects a recommended low-load server and establishes a native Windows tunnel."));
    homeDescription_->setMaximumWidth(590);
    layout->addWidget(homeDescription_);
    layout->addStretch();
    auto *actionRow = new QHBoxLayout;
    powerButton_ = new QPushButton(QStringLiteral("⏻"));
    powerButton_->setObjectName(QStringLiteral("powerButton"));
    powerButton_->setFixedSize(76, 76);
    actionRow->addWidget(powerButton_);
    auto *serverColumn = new QVBoxLayout;
    homeServer_ = new QLabel(QStringLiteral("Best available location"));
    homeServer_->setObjectName(QStringLiteral("connectionServer"));
    serverColumn->addWidget(homeServer_);
    serverColumn->addWidget(body(QStringLiteral("Full tunnel · DNS protected")));
    actionRow->addLayout(serverColumn);
    actionRow->addStretch();
    layout->addLayout(actionRow);
    homeError_ = new QLabel;
    homeError_->setObjectName(QStringLiteral("errorBanner"));
    homeError_->setWordWrap(true);
    homeError_->hide();
    layout->addWidget(homeError_);
    layout->addStretch();
    connect(powerButton_, &QPushButton::clicked, this, [this] {
        const auto disconnecting = connectionStatus_ == QStringLiteral("connected") || connectionStatus_ == QStringLiteral("reconnecting");
        setBusy(true, disconnecting ? QStringLiteral("Disconnecting…") : QStringLiteral("Connecting…"));
        rpc_.call(disconnecting ? QStringLiteral("disconnect") : QStringLiteral("quickConnect"), {}, [this](QJsonValue, QString error) {
            setBusy(false);
            if (!error.isEmpty()) showError(error);
            refreshStatus();
        });
    });
    return page;
}

QWidget *MainWindow::createLocationsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(55, 45, 55, 40);
    layout->addWidget(eyebrow(QStringLiteral("GLOBAL NETWORK")));
    layout->addWidget(title(QStringLiteral("Choose a location")));
    serverSearch_ = new QLineEdit;
    serverSearch_->setPlaceholderText(QStringLiteral("Search every country, country code, or city"));
    serverSearch_->setMinimumHeight(42);
    layout->addWidget(serverSearch_);
    locationCount_ = body(QStringLiteral("Loading locations…"));
    layout->addWidget(locationCount_);
    serverTable_ = new QTableWidget;
    serverTable_->setColumnCount(3);
    serverTable_->setHorizontalHeaderLabels({QStringLiteral("Country"), QStringLiteral("City"), QStringLiteral("Available servers")});
    serverTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    serverTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    serverTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    serverTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    serverTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    serverTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    serverTable_->verticalHeader()->hide();
    layout->addWidget(serverTable_, 1);
    auto *connectButton = new QPushButton(QStringLiteral("Connect to selected location"));
    connectButton->setObjectName(QStringLiteral("primary"));
    layout->addWidget(connectButton, 0, Qt::AlignRight);
    connect(serverSearch_, &QLineEdit::textChanged, this, &MainWindow::updateLocationTable);
    const auto connectSelected = [this] {
        const auto row = serverTable_->currentRow();
        if (row < 0) return;
        const auto countryId = serverTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
        const auto cityId = serverTable_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
        navigation_->setCurrentRow(0);
        setBusy(true, QStringLiteral("Connecting…"));
        rpc_.call(QStringLiteral("connectLocation"), {
            {QStringLiteral("locationCountryId"), countryId},
            {QStringLiteral("locationCityId"), cityId},
        }, [this](QJsonValue, QString error) {
            setBusy(false);
            if (!error.isEmpty()) showError(error);
            refreshStatus();
        });
    };
    connect(connectButton, &QPushButton::clicked, this, connectSelected);
    connect(serverTable_, &QTableWidget::cellDoubleClicked, this, [connectSelected](int, int) { connectSelected(); });
    return page;
}

QWidget *MainWindow::createSettingsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(65, 50, 65, 45);
    layout->addWidget(eyebrow(QStringLiteral("LOCAL POLICY")));
    layout->addWidget(title(QStringLiteral("Connection settings")));
    autoConnect_ = new QCheckBox(QStringLiteral("Auto-connect when OpenNord starts"));
    launchAtStartup_ = new QCheckBox(QStringLiteral("Launch OpenNord with Windows"));
    killSwitch_ = new QCheckBox(QStringLiteral("Strict kill switch (blocks traffic outside the tunnel)"));
    allowLan_ = new QCheckBox(QStringLiteral("Allow local network access in flexible mode"));
    for (auto *box : {autoConnect_, launchAtStartup_, killSwitch_, allowLan_}) {
        box->setMinimumHeight(48);
        layout->addWidget(box);
    }
    connect(killSwitch_, &QCheckBox::toggled, this, [this](bool strict) {
        allowLan_->setEnabled(technology_->currentData().toString() != QStringLiteral("openvpn") && !strict);
        if (strict) allowLan_->setChecked(false);
    });
    auto *form = new QFormLayout;
    technology_ = new QComboBox;
    technology_->addItem(QStringLiteral("NordLynx (WireGuard)"), QStringLiteral("nordlynx"));
    technology_->addItem(QStringLiteral("OpenVPN"), QStringLiteral("openvpn"));
    openVpnProtocol_ = new QComboBox;
    openVpnProtocol_->addItem(QStringLiteral("UDP — faster"), QStringLiteral("udp"));
    openVpnProtocol_->addItem(QStringLiteral("TCP — restrictive networks"), QStringLiteral("tcp"));
    preferredCountry_ = new QLineEdit;
    preferredCountry_->setMaxLength(2);
    preferredCountry_->setPlaceholderText(QStringLiteral("SE"));
    dnsServers_ = new QLineEdit;
    dnsServers_->setPlaceholderText(QStringLiteral("103.86.96.100, 103.86.99.100"));
    form->addRow(QStringLiteral("VPN technology"), technology_);
    form->addRow(QStringLiteral("OpenVPN protocol"), openVpnProtocol_);
    form->addRow(QStringLiteral("Preferred country"), preferredCountry_);
    form->addRow(QStringLiteral("DNS servers"), dnsServers_);
    layout->addLayout(form);
    policyNote_ = body(QString{});
    layout->addWidget(policyNote_);
    connect(technology_, &QComboBox::currentIndexChanged, this, [this] { updateTechnologyControls(); });
    layout->addStretch();
    auto *save = new QPushButton(QStringLiteral("Save changes"));
    save->setObjectName(QStringLiteral("primary"));
    layout->addWidget(save, 0, Qt::AlignRight);
    connect(save, &QPushButton::clicked, this, [this] {
        QJsonArray dns;
        for (const auto &part : dnsServers_->text().split(u',', Qt::SkipEmptyParts)) dns.append(part.trimmed());
        QJsonObject values{
            {QStringLiteral("autoConnect"), autoConnect_->isChecked()},
            {QStringLiteral("launchAtStartup"), launchAtStartup_->isChecked()},
            {QStringLiteral("killSwitch"), killSwitch_->isChecked()},
            {QStringLiteral("allowLan"), allowLan_->isChecked()},
            {QStringLiteral("technology"), technology_->currentData().toString()},
            {QStringLiteral("openVpnProtocol"), openVpnProtocol_->currentData().toString()},
            {QStringLiteral("preferredCountry"), preferredCountry_->text().trimmed().toUpper()},
            {QStringLiteral("customDns"), dns},
        };
        rpc_.call(QStringLiteral("saveSettings"), values, [this](QJsonValue, QString error) {
            if (!error.isEmpty()) return showError(error);
            selectedTechnology_ = technology_->currentData().toString();
            selectedOpenVpnProtocol_ = openVpnProtocol_->currentData().toString();
            locations_ = {};
            updateAutoStart(launchAtStartup_->isChecked());
            refreshStatus();
            QMessageBox::information(this, QStringLiteral("OpenNord"), QStringLiteral("Settings saved."));
        });
    });
    return page;
}

QWidget *MainWindow::createAccountPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(65, 50, 65, 45);
    layout->addWidget(eyebrow(QStringLiteral("ACCOUNT & BUILD")));
    layout->addWidget(title(QStringLiteral("Your OpenNord")));
    accountName_ = new QLabel;
    accountName_->setObjectName(QStringLiteral("accountName"));
    layout->addWidget(accountName_);
    layout->addWidget(body(QStringLiteral("Account credentials are encrypted by the LocalSystem service and isolated by your Windows SID.")));
    diagnostics_ = new QLabel;
    diagnostics_->setObjectName(QStringLiteral("diagnostics"));
    diagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(diagnostics_);
    layout->addStretch();
    auto *logout = new QPushButton(QStringLiteral("Sign out"));
    logout->setObjectName(QStringLiteral("danger"));
    layout->addWidget(logout, 0, Qt::AlignLeft);
    connect(logout, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Sign out"), QStringLiteral("Disconnect and remove this Windows user's saved Nord session?")) != QMessageBox::Yes) return;
        rpc_.call(QStringLiteral("logout"), {}, [this](QJsonValue, QString error) {
            if (!error.isEmpty()) return showError(error);
            refreshStatus();
        });
    });
    return page;
}

void MainWindow::refreshStatus()
{
    if (statusInFlight_) return;
    statusInFlight_ = true;
    rpc_.call(QStringLiteral("status"), {}, [this](QJsonValue value, QString error) {
        statusInFlight_ = false;
        if (!error.isEmpty()) {
            if (!authenticated_) pages_->setCurrentIndex(LoginPage);
            loginServiceError_->setText(error);
            loginServiceError_->show();
            homeError_->setText(error);
            homeError_->show();
            return;
        }
        loginServiceError_->hide();
        applyStatus(value.toObject());
    });
}

void MainWindow::applyStatus(const QJsonObject &state)
{
    const auto wasAuthenticated = authenticated_;
    const auto engineWasReady = selectedEngineReady();
    authenticated_ = state.value(QStringLiteral("authenticated")).toBool();
    wireGuardReady_ = state.value(QStringLiteral("wireGuardReady")).toBool();
    openVpnReady_ = state.value(QStringLiteral("openVpnReady")).toBool();
    selectedTechnology_ = state.value(QStringLiteral("technology")).toString(QStringLiteral("nordlynx"));
    selectedOpenVpnProtocol_ = state.value(QStringLiteral("openVpnProtocol")).toString(QStringLiteral("udp"));
    connectionStatus_ = state.value(QStringLiteral("status")).toString();
    navigation_->setVisible(authenticated_);
    if (!authenticated_) {
        autoConnectAttempted_ = false;
        pages_->setCurrentIndex(LoginPage);
        if (wasAuthenticated) navigation_->setCurrentRow(0);
        return;
    }
    const auto settingsOrAccount = pages_->currentIndex() == SettingsPage || pages_->currentIndex() == AccountPage;
    if (!selectedEngineReady() && !settingsOrAccount) {
        updateDependencyPage();
        pages_->setCurrentIndex(DependencyPage);
    } else if (selectedEngineReady() && pages_->currentIndex() == DependencyPage) {
        pages_->setCurrentIndex(HomePage);
    }
    if (pages_->currentIndex() == LoginPage) pages_->setCurrentIndex(HomePage);
    const auto server = state.value(QStringLiteral("server")).toObject();
    const auto connected = connectionStatus_ == QStringLiteral("connected");
    const auto reconnecting = connectionStatus_ == QStringLiteral("reconnecting");
    const auto changing = connectionStatus_ == QStringLiteral("connecting") || connectionStatus_ == QStringLiteral("disconnecting");
    homeTitle_->setText(connected ? QStringLiteral("Your connection is protected")
        : reconnecting ? QStringLiteral("Re-establishing encrypted route")
        : changing ? QStringLiteral("Changing encrypted route") : QStringLiteral("Ready when you are"));
    const auto openVpn = selectedTechnology_ == QStringLiteral("openvpn");
    homeEyebrow_->setText(openVpn
        ? QStringLiteral("OPENVPN · %1").arg(selectedOpenVpnProtocol_.toUpper())
        : QStringLiteral("NORDLYNX · WIREGUARDNT"));
    homeDescription_->setText(connected
        ? QStringLiteral("Traffic is routed through %1 with %2.")
            .arg(server.value(QStringLiteral("city")).toString(server.value(QStringLiteral("country")).toString()),
                 openVpn ? QStringLiteral("OpenVPN %1").arg(selectedOpenVpnProtocol_.toUpper()) : QStringLiteral("the native WireGuardNT tunnel"))
        : reconnecting ? QStringLiteral("OpenVPN is retrying the selected route. Use the stop control to disconnect immediately.")
        : QStringLiteral("One action selects a recommended low-load server and establishes the selected Windows tunnel."));
    homeServer_->setText((connected || reconnecting) ? server.value(QStringLiteral("hostname")).toString() : QStringLiteral("Best available location"));
    powerButton_->setText((connected || reconnecting) ? QStringLiteral("■") : QStringLiteral("⏻"));
    powerButton_->setEnabled(!changing);
    const auto error = state.value(QStringLiteral("error")).toString();
    homeError_->setVisible(!error.isEmpty());
    homeError_->setText(error);
    if (!wasAuthenticated) {
        loadSettings();
        loadAccount();
    } else if (!engineWasReady && selectedEngineReady()) {
        loadSettings();
    }
}

void MainWindow::loadLocations(bool force)
{
    if (!force && !locations_.isEmpty()) return updateLocationTable();
    locationCount_->setText(QStringLiteral("Loading every NordVPN location…"));
    rpc_.call(QStringLiteral("locations"), {}, [this](QJsonValue value, QString error) {
        if (!error.isEmpty()) return showError(error);
        locations_ = value.toArray();
        updateLocationTable();
    });
}

void MainWindow::updateLocationTable()
{
    const auto needle = serverSearch_->text().trimmed();
    serverTable_->setRowCount(0);
    for (const auto &value : locations_) {
        const auto location = value.toObject();
        const auto haystack = location.value(QStringLiteral("country")).toString() + u' '
            + location.value(QStringLiteral("countryCode")).toString() + u' '
            + location.value(QStringLiteral("city")).toString();
        if (!needle.isEmpty() && !haystack.contains(needle, Qt::CaseInsensitive)) continue;
        const auto row = serverTable_->rowCount();
        serverTable_->insertRow(row);
        auto *country = new QTableWidgetItem(QStringLiteral("%1  %2").arg(
            location.value(QStringLiteral("countryCode")).toString(), location.value(QStringLiteral("country")).toString()));
        country->setData(Qt::UserRole, location.value(QStringLiteral("countryId")).toInteger());
        country->setData(Qt::UserRole + 1, location.value(QStringLiteral("cityId")).toInteger());
        serverTable_->setItem(row, 0, country);
        serverTable_->setItem(row, 1, new QTableWidgetItem(location.value(QStringLiteral("city")).toString()));
        auto *count = new QTableWidgetItem(QString::number(location.value(QStringLiteral("serverCount")).toInt()));
        count->setTextAlignment(Qt::AlignCenter);
        serverTable_->setItem(row, 2, count);
    }
    locationCount_->setText(QStringLiteral("%1 of %2 locations").arg(serverTable_->rowCount()).arg(locations_.size()));
    if (serverTable_->rowCount() > 0) serverTable_->selectRow(0);
}

void MainWindow::loadSettings()
{
    rpc_.call(QStringLiteral("settings"), {}, [this](QJsonValue value, QString error) {
        if (!error.isEmpty()) return;
        const auto settings = value.toObject();
        autoConnect_->setChecked(settings.value(QStringLiteral("autoConnect")).toBool());
        launchAtStartup_->setChecked(settings.value(QStringLiteral("launchAtStartup")).toBool());
        killSwitch_->setChecked(settings.value(QStringLiteral("killSwitch")).toBool(true));
        allowLan_->setChecked(settings.value(QStringLiteral("allowLan")).toBool());
        const auto technologyIndex = technology_->findData(settings.value(QStringLiteral("technology")).toString(QStringLiteral("nordlynx")));
        technology_->setCurrentIndex(qMax(0, technologyIndex));
        const auto protocolIndex = openVpnProtocol_->findData(settings.value(QStringLiteral("openVpnProtocol")).toString(QStringLiteral("udp")));
        openVpnProtocol_->setCurrentIndex(qMax(0, protocolIndex));
        updateTechnologyControls();
        preferredCountry_->setText(settings.value(QStringLiteral("preferredCountry")).toString());
        QStringList dns;
        for (const auto &address : settings.value(QStringLiteral("customDns")).toArray()) dns.append(address.toString());
        dnsServers_->setText(dns.join(QStringLiteral(", ")));
        if (!autoConnectAttempted_ && autoConnect_->isChecked() && selectedEngineReady()
            && connectionStatus_ == QStringLiteral("disconnected")) {
            autoConnectAttempted_ = true;
            rpc_.call(QStringLiteral("quickConnect"), {}, [this](QJsonValue, QString connectError) {
                if (!connectError.isEmpty()) showError(connectError);
            });
        }
    });
}

void MainWindow::loadAccount()
{
    rpc_.call(QStringLiteral("account"), {}, [this](QJsonValue value, QString error) {
        if (error.isEmpty()) {
            const auto user = value.toObject();
            accountName_->setText(user.value(QStringLiteral("email")).toString(user.value(QStringLiteral("username")).toString(QStringLiteral("Nord Account"))));
        }
    });
    rpc_.call(QStringLiteral("diagnostics"), {}, [this](QJsonValue value, QString error) {
        if (!error.isEmpty()) return;
        const auto data = value.toObject();
        diagnostics_->setText(QStringLiteral("Version\t%1\nWireGuard\t%2\nOpenVPN\t%3\nTunnel config\t%4\nSession store\t%5\n\nGPLv3 · No telemetry")
            .arg(data.value(QStringLiteral("version")).toString(), data.value(QStringLiteral("wireGuardPath")).toString(),
                 data.value(QStringLiteral("openVpnPath")).toString(), data.value(QStringLiteral("wireGuardConfigPath")).toString(),
                 data.value(QStringLiteral("sessionDirectory")).toString()));
        dependencyPath_->setText(selectedTechnology_ == QStringLiteral("openvpn")
            ? data.value(QStringLiteral("openVpnPath")).toString()
            : data.value(QStringLiteral("wireGuardPath")).toString());
    });
}

bool MainWindow::selectedEngineReady() const
{
    return selectedTechnology_ == QStringLiteral("openvpn") ? openVpnReady_ : wireGuardReady_;
}

void MainWindow::updateTechnologyControls()
{
    const auto openVpn = technology_->currentData().toString() == QStringLiteral("openvpn");
    openVpnProtocol_->setEnabled(openVpn);
    killSwitch_->setEnabled(!openVpn);
    allowLan_->setEnabled(!openVpn && !killSwitch_->isChecked());
    policyNote_->setText(openVpn
        ? QStringLiteral("OpenVPN uses its profile routes plus block-outside-dns. The strict WireGuard kill switch and LAN exception do not apply to this engine.")
        : QStringLiteral("Strict mode uses WireGuard AllowedIPs for a full IPv4 and IPv6 tunnel. Flexible mode can preserve LAN access."));
}

void MainWindow::updateDependencyPage()
{
    const auto openVpn = selectedTechnology_ == QStringLiteral("openvpn");
    dependencyHeading_->setText(openVpn ? QStringLiteral("OpenVPN Community") : QStringLiteral("WireGuard for Windows"));
    dependencyDescription_->setText(openVpn
        ? QStringLiteral("OpenNord runs the open-source OpenVPN engine inside its LocalSystem service and supplies Nord service credentials over a localhost-only management channel.")
        : QStringLiteral("OpenNord delegates the tunnel and kernel driver to the official WireGuardNT client. Install it once, then check again."));
    dependencyInstall_->setText(openVpn ? QStringLiteral("Install OpenVPN Community ↗") : QStringLiteral("Install WireGuard ↗"));
    dependencyPath_->setText(QDir(qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files"))).filePath(
        openVpn ? QStringLiteral("OpenVPN/bin/openvpn.exe") : QStringLiteral("WireGuard/wireguard.exe")));
}

void MainWindow::showError(const QString &message)
{
    QMessageBox::critical(this, QStringLiteral("OpenNord"), message);
}

void MainWindow::setBusy(bool busy, QString text)
{
    loginButton_->setEnabled(!busy);
    powerButton_->setEnabled(!busy);
    if (!text.isEmpty()) statusBar()->showMessage(text);
    else statusBar()->clearMessage();
}

void MainWindow::updateAutoStart(bool enabled)
{
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (enabled) registry.setValue(QStringLiteral("OpenNord"), QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else registry.remove(QStringLiteral("OpenNord"));
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        * { font-family: "Aptos", "Segoe UI Variable"; color: #eaf2f3; font-size: 13px; }
        QMainWindow, QWidget { background: #07111c; }
        #sidebar { background: #061019; border-right: 1px solid rgba(164,201,206,35); }
        #brand { font-family: "Bahnschrift SemiCondensed"; font-size: 19px; font-weight: 600; color: #eaf2f3; margin-bottom: 26px; }
        #navigation { border: 0; outline: 0; background: transparent; }
        #navigation::item { min-height: 46px; padding-left: 14px; border-radius: 6px; color: #82929f; }
        #navigation::item:selected { color: #63e6cf; background: rgba(99,230,207,28); border-left: 2px solid #63e6cf; }
        #privacy { color: #70818e; font-size: 10px; line-height: 1.5; }
        #eyebrow { color: #63e6cf; font-size: 10px; font-weight: 600; letter-spacing: 2px; }
        #pageTitle { font-family: "Bahnschrift SemiCondensed"; font-size: 48px; font-weight: 600; line-height: .95; }
        #bodyText { color: #82929f; font-size: 13px; line-height: 1.5; }
        #fieldLabel { font-weight: 600; margin-top: 6px; }
        QLineEdit, QComboBox { min-height: 40px; padding: 0 12px; color: #eaf2f3; background: #091a27; border: 1px solid #233844; border-radius: 5px; selection-background-color: #28796f; }
        QLineEdit:focus, QComboBox:focus { border-color: #4da99b; }
        QComboBox QAbstractItemView { color: #eaf2f3; background: #091a27; selection-background-color: #19463f; border: 1px solid #233844; }
        QPushButton { min-height: 40px; padding: 0 18px; border-radius: 5px; border: 1px solid #29414d; background: #0b1d29; }
        QPushButton:hover { border-color: #63e6cf; }
        #primary { color: #05201d; background: #63e6cf; border: 0; font-weight: 600; }
        #primary:hover { background: #86f3df; }
        #secondary { background: transparent; }
        #linkButton { color: #63e6cf; background: transparent; border: 0; }
        #danger { color: #ffbfc1; background: rgba(255,111,114,22); border-color: rgba(255,111,114,70); }
        #powerButton { border-radius: 38px; color: #63e6cf; border: 1px solid #4da99b; background: rgba(99,230,207,20); font-size: 27px; }
        #powerButton:hover { background: rgba(99,230,207,38); }
        #connectionServer, #accountName { font-family: "Bahnschrift SemiCondensed"; font-size: 20px; font-weight: 600; }
        #errorBanner { color: #ffbfc1; background: rgba(255,111,114,18); border-left: 2px solid #ff6f72; padding: 10px; }
        QTableWidget { background: #081721; border: 1px solid #1e333e; gridline-color: #172b35; selection-background-color: rgba(99,230,207,28); }
        QHeaderView::section { min-height: 35px; color: #82929f; background: #091a27; border: 0; border-bottom: 1px solid #233844; padding: 6px; }
        QCheckBox { padding: 8px; border-bottom: 1px solid #172b35; }
        #diagnostics { color: #aab8be; background: #081721; border: 1px solid #1e333e; padding: 20px; font-family: "Cascadia Mono"; font-size: 11px; }
        QStatusBar { color: #63e6cf; background: #061019; }
    )"));
}

}
