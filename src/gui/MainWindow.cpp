#include "gui/MainWindow.h"
#include "gui/GraphiteMap.h"
#include "gui/FlagIcons.h"

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
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSet>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>
#include <shellapi.h>

#include <utility>

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

QWidget *scrollable(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

void setTone(QWidget *widget, const QString &tone)
{
    if (widget->property("tone").toString() == tone) return;
    widget->setProperty("tone", tone);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QIcon brandIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#b6ec62")));
    painter.drawRoundedRect(QRectF(8, 9, 15, 46), 5, 5);
    painter.drawRoundedRect(QRectF(41, 9, 15, 46), 5, 5);
    QPainterPath ribbon;
    ribbon.moveTo(11, 10); ribbon.lineTo(23, 10); ribbon.lineTo(53, 45);
    ribbon.lineTo(53, 54); ribbon.lineTo(41, 54); ribbon.lineTo(11, 19);
    ribbon.closeSubpath();
    QLinearGradient shade(10, 10, 53, 54);
    shade.setColorAt(0, QColor(QStringLiteral("#daf99f")));
    shade.setColorAt(1, QColor(QStringLiteral("#89bb3f")));
    painter.setBrush(shade);
    painter.drawPath(ribbon);
    return QIcon(pixmap);
}

QFrame *rule()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("divider"));
    line->setFixedHeight(1);
    return line;
}

QPixmap chevron()
{
    QPixmap image(14, 14);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#aeb8b0")), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(3, 5), QPointF(7, 9));
    painter.drawLine(QPointF(7, 9), QPointF(11, 5));
    return image;
}

class GraphiteCombo final : public QComboBox
{
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        painter.drawPixmap(width() - 25, (height() - 14) / 2, chevron());
    }
};

QIcon powerIcon(bool active)
{
    QPixmap image(24, 24);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(active ? QStringLiteral("#d9edc4") : QStringLiteral("#14200b")), 1.8, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(4, 4, 16, 16), 125 * 16, 290 * 16);
    painter.drawLine(QPointF(12, 2), QPointF(12, 11));
    return QIcon(image);
}


}

MainWindow::MainWindow(QWidget *parent, ServiceMode serviceMode)
    : QMainWindow(parent), rpc_(this), serviceMode_(serviceMode)
{
    setWindowTitle(QStringLiteral("OpenNord"));
    resize(1280, 820);
    setMinimumSize(960, 640);
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setWindowIcon(brandIcon());
    layout->addWidget(createHeader());
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
    updateHomeLocations();
    if (serviceMode_ == ServiceMode::Live) setupTrayIcon();

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
    updateConnectionControls();
    if (serviceMode_ == ServiceMode::Live) {
        statusTimer_->start();
        refreshStatus();
    }
}

void MainWindow::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    const auto icon = brandIcon();
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
    if (serviceMode_ == ServiceMode::Preview) return;
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

QWidget *MainWindow::createHeader()
{
    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("header"));
    header->setFixedHeight(84);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(28, 0, 28, 0);
    layout->setSpacing(12);
    auto *logo = new QLabel;
    logo->setPixmap(brandIcon().pixmap(38, 38));
    logo->setFixedSize(38, 38);
    layout->addWidget(logo);
    auto *brand = new QLabel(QStringLiteral("OpenNord"));
    brand->setObjectName(QStringLiteral("brand"));
    layout->addWidget(brand);
    layout->addSpacing(36);
    navigation_ = new QListWidget;
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->setAccessibleName(QStringLiteral("Main navigation"));
    navigation_->setFlow(QListView::LeftToRight);
    navigation_->setWrapping(false);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setFixedSize(440, 84);
    navigation_->addItems({QStringLiteral("Overview"), QStringLiteral("Locations"), QStringLiteral("Settings"), QStringLiteral("Account")});
    for (int i = 0; i < navigation_->count(); ++i) {
        navigation_->item(i)->setSizeHint(QSize(108, 84));
        navigation_->item(i)->setTextAlignment(Qt::AlignCenter);
    }
    navigation_->setCurrentRow(0);
    navigation_->setVisible(false);
    layout->addWidget(navigation_);
    layout->addStretch();
    homeStatus_ = new QLabel(QStringLiteral("CHECKING STATUS"));
    homeStatus_->setObjectName(QStringLiteral("statusBadge"));
    layout->addWidget(homeStatus_);
    return header;
}

QWidget *MainWindow::createLoginPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *artPanel = new QWidget;
    auto *artLayout = new QVBoxLayout(artPanel);
    artLayout->setContentsMargins(32, 56, 12, 36);
    artLayout->addWidget(eyebrow(QStringLiteral("PRIVATE ROUTES. PUBLIC CODE.")));
    artLayout->addWidget(new GraphiteMap, 1);
    artLayout->addWidget(body(QStringLiteral("An open-source connection. Built around you.")));
    layout->addWidget(artPanel, 5);
    auto *panel = new QWidget;
    auto *form = new QVBoxLayout(panel);
    form->setContentsMargins(36, 36, 36, 36);
    form->setSpacing(12);
    form->addStretch();
    form->addWidget(eyebrow(QStringLiteral("UNOFFICIAL OPEN-SOURCE CLIENT")));
    form->addSpacing(14);
    form->addWidget(title(QStringLiteral("Your privacy.\nYour connection.")));
    form->addWidget(body(QStringLiteral("Connect your NordVPN subscription with an access token from Nord Account. Your saved credentials are encrypted on this PC.")));
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
    tokenInput_->setPlaceholderText(QStringLiteral("Paste your access token"));
    tokenInput_->setAccessibleName(QStringLiteral("Nord access token"));
    label->setBuddy(tokenInput_);
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
    layout->addWidget(panel, 4);
    connect(tokenLink, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://my.nordaccount.com/dashboard/nordvpn/manual-configuration/")));
    });
    connect(loginButton_, &QPushButton::clicked, this, [this] {
        if (busy_ || !serviceAvailable_) return;
        const auto token = tokenInput_->text().trimmed();
        if (token.isEmpty()) return showError(QStringLiteral("Enter an access token."));
        setBusy(true, QStringLiteral("Verifying…"));
        callService(QStringLiteral("login"), {{QStringLiteral("token"), token}}, [this](QJsonValue, QString error) {
            tokenInput_->clear();
            setBusy(false);
            if (!error.isEmpty()) return showError(error);
            refreshStatus();
        });
    });
    connect(tokenInput_, &QLineEdit::returnPressed, loginButton_, &QPushButton::click);
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
    page->setObjectName(QStringLiteral("overview"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 20);
    outer->setSpacing(10);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 24, 26, 12);
    layout->setSpacing(16);
    auto *hero = new QHBoxLayout;
    hero->setSpacing(24);
    auto *left = new QWidget;
    left->setMinimumWidth(280);
    left->setMaximumWidth(316);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("connectionCard"));
    homeForm_ = new QVBoxLayout(card);
    auto *form = homeForm_;
    form->setContentsMargins(22, 20, 22, 20);
    form->setSpacing(10);
    homeEyebrow_ = eyebrow(QStringLiteral("VPN"));
    form->addWidget(homeEyebrow_);
    form->addWidget(rule());
    homeTitle_ = title(QStringLiteral("Make your\nconnection."));
    homeTitle_->setObjectName(QStringLiteral("homeTitle"));
    form->addWidget(homeTitle_);
    auto *locationLabel = body(QStringLiteral("Location"));
    form->addWidget(locationLabel);
    homeLocation_ = new GraphiteCombo;
    homeLocation_->setObjectName(QStringLiteral("homeLocation"));
    homeLocation_->setAccessibleName(QStringLiteral("Connection location"));
    homeLocation_->setIconSize(QSize(26, 26));
    homeLocation_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    homeLocation_->setMinimumContentsLength(12);
    homeLocation_->addItem(QStringLiteral("Recommended location"), QJsonObject{});
    locationLabel->setBuddy(homeLocation_);
    form->addWidget(homeLocation_);
    homeLocationHint_ = body(QStringLiteral("Recommended automatically"));
    homeLocationHint_->setObjectName(QStringLiteral("caption"));
    form->addWidget(homeLocationHint_);
    form->addWidget(body(QStringLiteral("Protocol")));
    auto *protocolButton = new QPushButton;
    protocolButton->setObjectName(QStringLiteral("protocolButton"));
    protocolButton->setAccessibleName(QStringLiteral("Change VPN protocol in settings"));
    auto *protocolLayout = new QHBoxLayout(protocolButton);
    protocolLayout->setContentsMargins(14, 0, 14, 0);
    homeProtocol_ = new QLabel(QStringLiteral("NordLynx"));
    homeProtocol_->setAttribute(Qt::WA_TransparentForMouseEvents);
    protocolLayout->addWidget(homeProtocol_, 1);
    auto *arrow = new QLabel;
    arrow->setPixmap(chevron());
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    protocolLayout->addWidget(arrow);
    form->addWidget(protocolButton);
    connect(protocolButton, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(2); });
    powerButton_ = new QPushButton(QStringLiteral("Quick connect"));
    powerButton_->setObjectName(QStringLiteral("connectButton"));
    powerButton_->setMinimumHeight(52);
    powerButton_->setIconSize(QSize(22, 22));
    form->addWidget(powerButton_);
    homeService_ = body(QStringLiteral("○  Checking service"));
    homeService_->setObjectName(QStringLiteral("serviceState"));
    form->addWidget(homeService_);
    leftLayout->addWidget(card);
    leftLayout->addWidget(rule());
    homeDescription_ = body(QStringLiteral("Choose a location and make your connection."));
    leftLayout->addWidget(homeDescription_);
    homeRoute_ = body(QStringLiteral("Not verified"));
    homeRoute_->setObjectName(QStringLiteral("caption"));
    leftLayout->addWidget(homeRoute_);
    homeServer_ = body(QString{});
    homeServer_->setObjectName(QStringLiteral("caption"));
    homeServer_->setTextFormat(Qt::PlainText);
    leftLayout->addWidget(homeServer_);
    leftLayout->addStretch();
    hero->addWidget(left);
    auto *mapColumn = new QVBoxLayout;
    auto *mapHeading = eyebrow(QStringLiteral("PRIVACY  /  FREEDOM  /  OPEN SOURCE"));
    mapHeading->setAlignment(Qt::AlignRight);
    mapColumn->addWidget(mapHeading);
    connectionArt_ = new GraphiteMap;
    mapColumn->addWidget(connectionArt_, 1);
    hero->addLayout(mapColumn, 1);
    layout->addLayout(hero, 1);
    homeError_ = new QLabel;
    homeError_->setObjectName(QStringLiteral("errorBanner"));
    homeError_->setWordWrap(true);
    homeError_->hide();
    layout->addWidget(homeError_);
    auto *locationBar = new QFrame;
    locationBar->setObjectName(QStringLiteral("locationBar"));
    auto *bar = new QHBoxLayout(locationBar);
    bar->setContentsMargins(16, 16, 16, 16);
    bar->setSpacing(12);
    homeSearch_ = new QLineEdit;
    homeSearch_->setObjectName(QStringLiteral("homeSearch"));
    homeSearch_->setPlaceholderText(QStringLiteral("Search countries…"));
    homeSearch_->setAccessibleName(QStringLiteral("Search quick locations"));
    homeSearch_->setClearButtonEnabled(true);
    homeSearch_->setFixedWidth(182);
    homeSearch_->setMinimumHeight(64);
    bar->addWidget(homeSearch_);
    quickLocations_ = new QHBoxLayout;
    quickLocations_->setSpacing(10);
    bar->addLayout(quickLocations_, 1);
    auto *browse = new QPushButton(QStringLiteral("•••"));
    browse->setObjectName(QStringLiteral("browseLocations"));
    browse->setFixedSize(46, 64);
    browse->setToolTip(QStringLiteral("Explore all locations"));
    browse->setAccessibleName(QStringLiteral("Explore all locations"));
    bar->addWidget(browse);
    outer->addWidget(scrollable(content), 1);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(26, 0, 26, 0);
    bottom->addWidget(locationBar);
    outer->addLayout(bottom);
    connect(browse, &QPushButton::clicked, this, [this] {
        serverSearch_->setText(homeSearch_->text());
        navigation_->setCurrentRow(1);
    });
    connect(homeSearch_, &QLineEdit::textChanged, this, &MainWindow::updateHomeLocations);
    connect(homeSearch_, &QLineEdit::returnPressed, browse, &QPushButton::click);
    connect(homeLocation_, &QComboBox::currentIndexChanged, this, [this] {
        selectedLocation_ = homeLocation_->currentData().toJsonObject();
        updateHomeDestination();
        updateHomeLocations();
    });
    connect(powerButton_, &QPushButton::clicked, this, [this] {
        if (!powerButton_->isEnabled()) return;
        const auto disconnecting = connectionStatus_ == QStringLiteral("connected") || connectionStatus_ == QStringLiteral("reconnecting");
        QString method = disconnecting ? QStringLiteral("disconnect") : QStringLiteral("quickConnect");
        QJsonObject params;
        if (!disconnecting && !selectedLocation_.isEmpty()) {
            method = QStringLiteral("connectLocation");
            params = {{QStringLiteral("locationCountryId"), selectedLocation_.value(QStringLiteral("countryId"))},
                {QStringLiteral("locationCityId"), selectedLocation_.value(QStringLiteral("cityId"))}};
        }
        setBusy(true, disconnecting ? QStringLiteral("Disconnecting…") : QStringLiteral("Connecting…"));
        callService(method, params, [this](QJsonValue, QString error) {
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
    layout->setContentsMargins(40, 36, 40, 32);
    layout->setSpacing(14);
    layout->addWidget(eyebrow(QStringLiteral("GLOBAL NETWORK")));
    layout->addWidget(title(QStringLiteral("Choose a location")));
    serverSearch_ = new QLineEdit;
    serverSearch_->setPlaceholderText(QStringLiteral("Search every country, country code, or city"));
    serverSearch_->setAccessibleName(QStringLiteral("Search locations"));
    serverSearch_->setClearButtonEnabled(true);
    serverSearch_->setMinimumHeight(42);
    layout->addWidget(serverSearch_);
    locationCount_ = body(QStringLiteral("Loading locations…"));
    layout->addWidget(locationCount_);
    serverTable_ = new QTableWidget;
    serverTable_->setAccessibleName(QStringLiteral("Available VPN locations"));
    serverTable_->setColumnCount(3);
    serverTable_->setIconSize(QSize(28, 28));
    serverTable_->setHorizontalHeaderLabels({QStringLiteral("Country"), QStringLiteral("City"), QStringLiteral("Available servers")});
    serverTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    serverTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    serverTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    serverTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    serverTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    serverTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    serverTable_->verticalHeader()->hide();
    serverTable_->verticalHeader()->setDefaultSectionSize(48);
    serverTable_->setShowGrid(false);
    serverTable_->setAlternatingRowColors(true);
    layout->addWidget(serverTable_, 1);
    locationConnectButton_ = new QPushButton(QStringLiteral("Connect to selected location"));
    locationConnectButton_->setObjectName(QStringLiteral("primary"));
    layout->addWidget(locationConnectButton_, 0, Qt::AlignRight);
    connect(serverSearch_, &QLineEdit::textChanged, this, &MainWindow::updateLocationTable);
    const auto connectSelected = [this] {
        if (!locationConnectButton_->isEnabled()) return;
        const auto row = serverTable_->currentRow();
        if (row < 0 || !serverTable_->item(row, 0)) return;
        const auto countryId = serverTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
        const auto cityId = serverTable_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
        selectedLocation_ = serverTable_->item(row, 0)->data(Qt::UserRole + 2).toJsonObject();
        updateHomeLocations();
        navigation_->setCurrentRow(0);
        setBusy(true, QStringLiteral("Connecting…"));
        callService(QStringLiteral("connectLocation"), {
            {QStringLiteral("locationCountryId"), countryId},
            {QStringLiteral("locationCityId"), cityId},
        }, [this](QJsonValue, QString error) {
            setBusy(false);
            if (!error.isEmpty()) showError(error);
            refreshStatus();
        });
    };
    connect(locationConnectButton_, &QPushButton::clicked, this, connectSelected);
    connect(serverTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateConnectionControls);
    connect(serverTable_, &QTableWidget::cellDoubleClicked, this, [connectSelected](int, int) { connectSelected(); });
    return page;
}

QWidget *MainWindow::createSettingsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 36, 40, 32);
    layout->setSpacing(14);
    layout->addWidget(eyebrow(QStringLiteral("LOCAL POLICY")));
    layout->addWidget(title(QStringLiteral("Connection settings")));
    layout->addWidget(body(QStringLiteral("Choose how OpenNord connects. Tunnel changes take effect on your next connection.")));
    autoConnect_ = new QCheckBox(QStringLiteral("Auto-connect when OpenNord starts"));
    launchAtStartup_ = new QCheckBox(QStringLiteral("Launch OpenNord with Windows"));
    killSwitch_ = new QCheckBox(QStringLiteral("Strict kill switch (blocks traffic outside the tunnel)"));
    allowLan_ = new QCheckBox(QStringLiteral("Allow local network access in flexible mode"));
    for (auto *box : {autoConnect_, launchAtStartup_, killSwitch_, allowLan_}) {
        box->setMinimumHeight(40);
        layout->addWidget(box);
    }
    connect(killSwitch_, &QCheckBox::toggled, this, [this](bool strict) {
        allowLan_->setEnabled(technology_->currentData().toString() != QStringLiteral("openvpn") && !strict);
        if (strict) allowLan_->setChecked(false);
    });
    auto *form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    technology_ = new GraphiteCombo;
    technology_->addItem(QStringLiteral("NordLynx (WireGuard)"), QStringLiteral("nordlynx"));
    technology_->addItem(QStringLiteral("OpenVPN"), QStringLiteral("openvpn"));
    openVpnProtocol_ = new GraphiteCombo;
    openVpnProtocol_->addItem(QStringLiteral("UDP — faster"), QStringLiteral("udp"));
    openVpnProtocol_->addItem(QStringLiteral("TCP — restrictive networks"), QStringLiteral("tcp"));
    preferredCountry_ = new QLineEdit;
    preferredCountry_->setMaxLength(2);
    preferredCountry_->setPlaceholderText(QStringLiteral("SE"));
    preferredCountry_->setToolTip(QStringLiteral("Two-letter country code for quick connect. Leave empty for the recommended country."));
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
    saveSettingsButton_ = new QPushButton(QStringLiteral("Save changes"));
    saveSettingsButton_->setObjectName(QStringLiteral("primary"));
    layout->addWidget(saveSettingsButton_, 0, Qt::AlignRight);
    connect(saveSettingsButton_, &QPushButton::clicked, this, [this, page] {
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
        page->setEnabled(false);
        callService(QStringLiteral("saveSettings"), values, [this, page, values](QJsonValue, QString error) {
            page->setEnabled(true);
            if (!error.isEmpty()) return showError(error);
            selectedTechnology_ = values.value(QStringLiteral("technology")).toString();
            selectedOpenVpnProtocol_ = values.value(QStringLiteral("openVpnProtocol")).toString();
            locations_ = {};
            updateLocationTable();
            loadLocations(true);
            updateAutoStart(values.value(QStringLiteral("launchAtStartup")).toBool());
            refreshStatus();
            statusBar()->showMessage(QStringLiteral("Settings saved. Tunnel changes apply on your next connection."), 6000);
        });
    });
    return scrollable(page);
}

QWidget *MainWindow::createAccountPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 36, 40, 32);
    layout->setSpacing(14);
    layout->addWidget(eyebrow(QStringLiteral("ACCOUNT & BUILD")));
    layout->addWidget(title(QStringLiteral("Your OpenNord")));
    accountName_ = new QLabel;
    accountName_->setObjectName(QStringLiteral("accountName"));
    accountName_->setTextFormat(Qt::PlainText);
    accountName_->setWordWrap(true);
    layout->addWidget(accountName_);
    layout->addWidget(body(QStringLiteral("Account credentials are encrypted by the LocalSystem service and isolated by your Windows SID.")));
    diagnostics_ = new QLabel;
    diagnostics_->setObjectName(QStringLiteral("diagnostics"));
    diagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnostics_->setTextFormat(Qt::PlainText);
    diagnostics_->setWordWrap(true);
    layout->addWidget(diagnostics_);
    layout->addStretch();
    auto *logout = new QPushButton(QStringLiteral("Sign out"));
    logout->setObjectName(QStringLiteral("danger"));
    layout->addWidget(logout, 0, Qt::AlignLeft);
    connect(logout, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Sign out"), QStringLiteral("Disconnect and remove this Windows user's saved Nord session?")) != QMessageBox::Yes) return;
        callService(QStringLiteral("logout"), {}, [this](QJsonValue, QString error) {
            if (!error.isEmpty()) return showError(error);
            refreshStatus();
        });
    });
    return scrollable(page);
}

void MainWindow::callService(QString method, QJsonObject params, RpcClient::Callback callback)
{
    // Preview windows render and test the real UI without contacting SCM, the service, or the network.
    if (serviceMode_ == ServiceMode::Preview) {
        callback({}, QStringLiteral("Service calls are disabled in preview mode."));
        return;
    }
    rpc_.call(std::move(method), std::move(params), std::move(callback));
}

void MainWindow::refreshStatus()
{
    if (serviceMode_ == ServiceMode::Preview) return;
    if (statusInFlight_) return;
    statusInFlight_ = true;
    callService(QStringLiteral("status"), {}, [this](QJsonValue value, QString error) {
        statusInFlight_ = false;
        if (!error.isEmpty()) {
            showServiceUnavailable(error);
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
    const auto oldTechnology = selectedTechnology_;
    const auto oldProtocol = selectedOpenVpnProtocol_;
    if (!serviceAvailable_ && !busy_) statusBar()->clearMessage();
    serviceAvailable_ = true;
    loginServiceError_->hide();
    authenticated_ = state.value(QStringLiteral("authenticated")).toBool();
    wireGuardReady_ = state.value(QStringLiteral("wireGuardReady")).toBool();
    openVpnReady_ = state.value(QStringLiteral("openVpnReady")).toBool();
    selectedTechnology_ = state.value(QStringLiteral("technology")).toString(QStringLiteral("nordlynx"));
    selectedOpenVpnProtocol_ = state.value(QStringLiteral("openVpnProtocol")).toString(QStringLiteral("udp"));
    connectionStatus_ = state.value(QStringLiteral("status")).toString(QStringLiteral("unknown"));
    if (oldTechnology != selectedTechnology_ || oldProtocol != selectedOpenVpnProtocol_) {
        locations_ = {};
        selectedLocation_ = {};
        updateLocationTable();
    }
    navigation_->setVisible(authenticated_);
    updateConnectionControls();
    if (!authenticated_) {
        autoConnectAttempted_ = false;
        activeServer_ = {};
        selectedLocation_ = {};
        locations_ = {};
        updateHomeLocations();
        connectionArt_->setConnected(false);
        homeStatus_->setText(QStringLiteral("SIGN IN"));
        setTone(homeStatus_, QStringLiteral("idle"));
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
    activeServer_ = server;
    const auto connected = connectionStatus_ == QStringLiteral("connected");
    const auto reconnecting = connectionStatus_ == QStringLiteral("reconnecting");
    const auto changing = connectionStatus_ == QStringLiteral("connecting") || connectionStatus_ == QStringLiteral("disconnecting");
    const auto unknown = connectionStatus_ != QStringLiteral("connected") && connectionStatus_ != QStringLiteral("disconnected")
        && connectionStatus_ != QStringLiteral("error") && !reconnecting && !changing;
    homeTitle_->setText(connected ? QStringLiteral("Connected.\nStay private.")
        : reconnecting ? QStringLiteral("Reconnecting\nyour route…")
        : connectionStatus_ == QStringLiteral("connecting") ? QStringLiteral("Finding your\nprivate route…")
        : connectionStatus_ == QStringLiteral("disconnecting") ? QStringLiteral("Closing your\nconnection…")
        : unknown ? QStringLiteral("Checking your\nconnection…") : QStringLiteral("Make your\nconnection."));
    const auto openVpn = selectedTechnology_ == QStringLiteral("openvpn");
    homeEyebrow_->setText(QStringLiteral("VPN"));
    homeProtocol_->setText(openVpn ? QStringLiteral("OpenVPN · %1").arg(selectedOpenVpnProtocol_.toUpper())
        : QStringLiteral("NordLynx"));
    homeStatus_->setText(connected ? QStringLiteral("CONNECTED") : reconnecting ? QStringLiteral("RECONNECTING")
        : changing ? connectionStatus_.toUpper() : unknown ? QStringLiteral("STATUS UNKNOWN") : QStringLiteral("NOT CONNECTED"));
    const auto tone = connected ? QStringLiteral("connected") : reconnecting || changing ? QStringLiteral("pending") : QStringLiteral("idle");
    setTone(homeStatus_, tone);
    setTone(powerButton_, tone);
    connectionArt_->setConnected(connected);
    homeService_->setText(QStringLiteral("●  Service ready"));
    setTone(homeService_, QStringLiteral("connected"));
    homeDescription_->setText(connected
        ? QStringLiteral("Traffic is routed through %1 with %2.")
            .arg(server.value(QStringLiteral("city")).toString(server.value(QStringLiteral("country")).toString()),
                 openVpn ? QStringLiteral("OpenVPN %1").arg(selectedOpenVpnProtocol_.toUpper()) : QStringLiteral("the native WireGuardNT tunnel"))
        : reconnecting ? QStringLiteral("The tunnel is retrying the selected route. You can disconnect at any time.")
        : changing ? QStringLiteral("Please wait while the VPN service updates your connection.")
        : unknown ? QStringLiteral("The service has not reported a known tunnel state yet.")
        : QStringLiteral("Your privacy matters. Choose a location and make your connection."));
    updateHomeDestination();
    homeRoute_->setText(connected ? QStringLiteral("VPN tunnel established")
        : reconnecting ? QStringLiteral("Waiting for the tunnel")
        : changing ? QStringLiteral("Connection in progress")
        : unknown ? QStringLiteral("Not verified") : QStringLiteral("No active VPN tunnel"));
    const auto error = state.value(QStringLiteral("error")).toString();
    homeError_->setVisible(!error.isEmpty());
    homeError_->setText(error);
    if (!wasAuthenticated) {
        loadSettings();
        loadAccount();
    } else if (!engineWasReady && selectedEngineReady()) {
        loadSettings();
    }
    if (serviceMode_ == ServiceMode::Live && locations_.isEmpty() && !locationsInFlight_
        && (!wasAuthenticated || oldTechnology != selectedTechnology_ || oldProtocol != selectedOpenVpnProtocol_)) loadLocations();
}

void MainWindow::loadLocations(bool force)
{
    if (locationsInFlight_) return;
    if (!force && !locations_.isEmpty()) return updateLocationTable();
    locationsInFlight_ = true;
    updateConnectionControls();
    updateHomeLocations();
    locationCount_->setText(QStringLiteral("Loading locations…"));
    const auto requestedTechnology = selectedTechnology_;
    const auto requestedProtocol = selectedOpenVpnProtocol_;
    callService(QStringLiteral("locations"), {}, [this, requestedTechnology, requestedProtocol](QJsonValue value, QString error) {
        locationsInFlight_ = false;
        updateConnectionControls();
        if (requestedTechnology != selectedTechnology_ || requestedProtocol != selectedOpenVpnProtocol_) {
            if (authenticated_) loadLocations(true);
            return;
        }
        if (!error.isEmpty()) {
            locationCount_->setText(QStringLiteral("Locations unavailable. Open Locations again to retry."));
            updateHomeLocations();
            if (pages_->currentIndex() == LocationsPage) showError(error);
            return;
        }
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
        auto *country = new QTableWidgetItem(countryFlagIcon(location.value(QStringLiteral("countryCode")).toString()),
            location.value(QStringLiteral("country")).toString());
        country->setData(Qt::UserRole, location.value(QStringLiteral("countryId")).toInteger());
        country->setData(Qt::UserRole + 1, location.value(QStringLiteral("cityId")).toInteger());
        country->setData(Qt::UserRole + 2, location);
        serverTable_->setItem(row, 0, country);
        serverTable_->setItem(row, 1, new QTableWidgetItem(location.value(QStringLiteral("city")).toString()));
        auto *count = new QTableWidgetItem(QString::number(location.value(QStringLiteral("serverCount")).toInt()));
        count->setTextAlignment(Qt::AlignCenter);
        serverTable_->setItem(row, 2, count);
    }
    locationCount_->setText(serverTable_->rowCount() == 0 && !needle.isEmpty()
        ? QStringLiteral("No matching locations. Try another country or city.")
        : QStringLiteral("%1 of %2 locations").arg(serverTable_->rowCount()).arg(locations_.size()));
    if (serverTable_->rowCount() > 0) serverTable_->selectRow(0);
    updateHomeLocations();
    updateConnectionControls();
}

void MainWindow::updateHomeLocations()
{
    const QSignalBlocker blocker(homeLocation_);
    homeLocation_->clear();
    homeLocation_->addItem(QStringLiteral("Recommended location"), QVariant::fromValue(QJsonObject{}));
    int selectedIndex = 0;
    for (const auto &value : locations_) {
        const auto location = value.toObject();
        const auto text = QStringLiteral("%1 · %2").arg(location.value(QStringLiteral("country")).toString(),
            location.value(QStringLiteral("city")).toString());
        homeLocation_->addItem(countryFlagIcon(location.value(QStringLiteral("countryCode")).toString()), text, QVariant::fromValue(location));
        if (!selectedLocation_.isEmpty()
            && location.value(QStringLiteral("countryId")) == selectedLocation_.value(QStringLiteral("countryId"))
            && location.value(QStringLiteral("cityId")) == selectedLocation_.value(QStringLiteral("cityId"))) selectedIndex = homeLocation_->count() - 1;
    }
    if (!selectedIndex) selectedLocation_ = {};
    homeLocation_->setCurrentIndex(selectedIndex);
    while (auto *item = quickLocations_->takeAt(0)) {
        if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); }
        delete item;
    }
    locationTiles_.clear();
    QList<QJsonObject> choices;
    QSet<QString> seen;
    const auto needle = homeSearch_->text().trimmed();
    const auto add = [&](const QJsonObject &location) {
        const auto code = location.value(QStringLiteral("countryCode")).toString();
        const auto searchable = location.value(QStringLiteral("country")).toString() + u' '
            + location.value(QStringLiteral("city")).toString() + u' ' + code;
        if (choices.size() == 4 || seen.contains(code) || !searchable.contains(needle, Qt::CaseInsensitive)) return;
        seen.insert(code); choices.append(location);
    };
    for (const auto &code : {QStringLiteral("SE"), QStringLiteral("NO"), QStringLiteral("DE"), QStringLiteral("NL")})
        for (const auto &value : locations_) if (value.toObject().value(QStringLiteral("countryCode")).toString() == code) add(value.toObject());
    for (const auto &value : locations_) add(value.toObject());
    for (const auto &location : choices) {
        auto *tile = new QPushButton;
        tile->setObjectName(QStringLiteral("locationTile"));
        tile->setMinimumSize(112, 64);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const auto country = location.value(QStringLiteral("country")).toString();
        const auto city = location.value(QStringLiteral("city")).toString();
        tile->setAccessibleName(QStringLiteral("Select %1, %2").arg(country, city));
        tile->setToolTip(QStringLiteral("%1 · %2").arg(country, city));
        tile->setProperty("selected", location.value(QStringLiteral("countryId")) == selectedLocation_.value(QStringLiteral("countryId"))
            && location.value(QStringLiteral("cityId")) == selectedLocation_.value(QStringLiteral("cityId")));
        auto *row = new QHBoxLayout(tile);
        row->setContentsMargins(10, 8, 10, 8);
        row->setSpacing(10);
        auto *flag = new QLabel;
        flag->setPixmap(countryFlagIcon(location.value(QStringLiteral("countryCode")).toString()).pixmap(28, 28));
        flag->setFixedSize(28, 28);
        flag->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(flag);
        auto *labels = new QVBoxLayout;
        labels->setSpacing(2);
        for (const auto &text : {country, city}) {
            auto *label = new QLabel(text);
            label->setObjectName(text == country ? QStringLiteral("tileCountry") : QStringLiteral("caption"));
            label->setTextFormat(Qt::PlainText);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            labels->addWidget(label);
        }
        row->addLayout(labels, 1);
        quickLocations_->addWidget(tile, 1);
        locationTiles_.append(tile);
        connect(tile, &QPushButton::clicked, this, [this, location] {
            selectedLocation_ = location;
            updateHomeLocations();
            updateHomeDestination();
            for (auto *replacement : locationTiles_) if (replacement->property("selected").toBool()) replacement->setFocus();
        });
    }
    if (choices.isEmpty()) {
        auto *empty = body(locationsInFlight_ ? QStringLiteral("Loading locations…")
            : !needle.isEmpty() ? QStringLiteral("No matching locations") : QStringLiteral("Explore locations to load the server list  →"));
        quickLocations_->addWidget(empty, 1);
    }
    updateHomeDestination();
    updateConnectionControls();
}

void MainWindow::updateHomeDestination()
{
    const auto active = connectionStatus_ == QStringLiteral("connected") || connectionStatus_ == QStringLiteral("reconnecting");
    const auto location = active ? activeServer_ : selectedLocation_;
    connectionArt_->setLocation(location.value(QStringLiteral("countryCode")).toString(), location.value(QStringLiteral("country")).toString());
    homeServer_->setText(active ? activeServer_.value(QStringLiteral("hostname")).toString() : QString{});
    homeServer_->setVisible(active && height() >= 760);
    homeLocationHint_->setText(active ? QStringLiteral("Active: %1 · %2").arg(activeServer_.value(QStringLiteral("country")).toString(), activeServer_.value(QStringLiteral("city")).toString())
        : selectedLocation_.isEmpty() ? QStringLiteral("Recommended automatically") : QStringLiteral("Selected for your next connection"));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (homeForm_) updateResponsiveLayout();
}

void MainWindow::updateResponsiveLayout()
{
    const bool compact = height() < 760;
    homeEyebrow_->setVisible(!compact);
    homeLocationHint_->setVisible(!compact);
    homeDescription_->setVisible(!compact);
    homeRoute_->setVisible(!compact);
    homeServer_->setVisible(!compact && !homeServer_->text().isEmpty());
    homeForm_->setSpacing(compact ? 6 : 10);
    homeForm_->setContentsMargins(22, compact ? 14 : 20, 22, compact ? 14 : 20);
    if (homeTitle_->property("compact").toBool() != compact) {
        homeTitle_->setProperty("compact", compact);
        homeTitle_->style()->unpolish(homeTitle_);
        homeTitle_->style()->polish(homeTitle_);
    }
}

void MainWindow::loadSettings()
{
    callService(QStringLiteral("settings"), {}, [this](QJsonValue value, QString error) {
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
        if (!autoConnectAttempted_ && selectedEngineReady()) {
            autoConnectAttempted_ = true;
            if (autoConnect_->isChecked() && connectionStatus_ == QStringLiteral("disconnected") && !busy_) {
                setBusy(true, QStringLiteral("Connecting…"));
                callService(QStringLiteral("quickConnect"), {}, [this](QJsonValue, QString connectError) {
                    setBusy(false);
                    if (!connectError.isEmpty()) showError(connectError);
                    refreshStatus();
                });
            }
        }
    });
}

void MainWindow::loadAccount()
{
    callService(QStringLiteral("account"), {}, [this](QJsonValue value, QString error) {
        if (error.isEmpty()) {
            const auto user = value.toObject();
            accountName_->setText(user.value(QStringLiteral("email")).toString(user.value(QStringLiteral("username")).toString(QStringLiteral("Nord Account"))));
        }
    });
    callService(QStringLiteral("diagnostics"), {}, [this](QJsonValue value, QString error) {
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
    busy_ = busy;
    busyText_ = std::move(text);
    updateConnectionControls();
    if (!busyText_.isEmpty()) statusBar()->showMessage(busyText_);
    else statusBar()->clearMessage();
}

void MainWindow::updateConnectionControls()
{
    const auto active = connectionStatus_ == QStringLiteral("connected") || connectionStatus_ == QStringLiteral("reconnecting");
    const auto idle = connectionStatus_ == QStringLiteral("disconnected") || connectionStatus_ == QStringLiteral("error");
    const auto canChange = serviceAvailable_ && authenticated_ && !busy_ && (active || idle);
    loginButton_->setEnabled(serviceAvailable_ && !busy_);
    tokenInput_->setEnabled(!busy_);
    loginButton_->setText(busy_ && !authenticated_ ? QStringLiteral("Verifying…") : QStringLiteral("Continue securely"));
    powerButton_->setEnabled(canChange && (active || selectedEngineReady()));
    powerButton_->setText(busy_ ? (busyText_.isEmpty() ? QStringLiteral("Please wait…") : busyText_)
        : connectionStatus_ == QStringLiteral("connecting") ? QStringLiteral("Connecting…")
        : connectionStatus_ == QStringLiteral("disconnecting") ? QStringLiteral("Disconnecting…")
        : active ? QStringLiteral("Disconnect") : QStringLiteral("Quick connect"));
    powerButton_->setAccessibleName(powerButton_->text());
    powerButton_->setIcon(powerIcon(active));
    locationConnectButton_->setEnabled(canChange && selectedEngineReady() && !locationsInFlight_ && serverTable_->currentRow() >= 0);
    saveSettingsButton_->setEnabled(serviceAvailable_ && authenticated_ && !busy_);
    homeLocation_->setEnabled(canChange && idle && !locationsInFlight_);
    for (auto *tile : locationTiles_) tile->setEnabled(canChange && idle && !locationsInFlight_);
}

void MainWindow::showServiceUnavailable(const QString &error)
{
    serviceAvailable_ = false;
    connectionStatus_ = QStringLiteral("unknown");
    if (!authenticated_) pages_->setCurrentIndex(LoginPage);
    loginServiceError_->setText(error);
    loginServiceError_->show();
    homeError_->setText(error);
    homeError_->show();
    homeStatus_->setText(QStringLiteral("STATUS UNAVAILABLE"));
    homeTitle_->setText(QStringLiteral("Connection\nstatus unavailable"));
    homeDescription_->setText(QStringLiteral("OpenNord cannot reach its VPN service. Your current protection could not be verified."));
    homeRoute_->setText(QStringLiteral("Not verified"));
    homeServer_->setText(QStringLiteral("Waiting for the VPN service"));
    homeLocationHint_->setText(QStringLiteral("Status updates resume automatically"));
    setTone(homeStatus_, QStringLiteral("pending"));
    setTone(powerButton_, QStringLiteral("idle"));
    connectionArt_->setConnected(false);
    connectionArt_->setLocation({}, {});
    homeService_->setText(QStringLiteral("○  Service unavailable"));
    setTone(homeService_, QStringLiteral("pending"));
    statusBar()->showMessage(QStringLiteral("VPN service unavailable · Connection status could not be verified"));
    updateConnectionControls();
}

void MainWindow::updateAutoStart(bool enabled)
{
    if (serviceMode_ == ServiceMode::Preview) return;
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (enabled) registry.setValue(QStringLiteral("OpenNord"), QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else registry.remove(QStringLiteral("OpenNord"));
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        * { font-family: "Segoe UI"; color: #eceeeb; font-size: 13px; }
        QMainWindow, QWidget { background: #15191a; }
        QLabel { background: transparent; }
        QScrollArea { border: 0; }
        #header { background: #191d1e; border-bottom: 1px solid #2c3233; }
        #brand { font-size: 24px; font-weight: 600; color: #f4f5f1; }
        #navigation { border: 0; outline: 0; background: transparent; }
        #navigation::item { color: #a9afad; border-bottom: 2px solid transparent; }
        #navigation::item:hover { color: #f5f6f2; background: #202627; }
        #navigation::item:selected { color: #f5f6f2; border-bottom: 2px solid #b6ec62; background: transparent; }
        #eyebrow { color: #8c9692; font-size: 10px; letter-spacing: 2px; }
        #pageTitle { font-size: 36px; font-weight: 600; color: #f2f3ef; }
        #homeTitle { font-size: 32px; font-weight: 600; color: #f2f3ef; }
        #homeTitle[compact="true"] { font-size: 24px; }
        #bodyText { color: #a9b1ad; font-size: 13px; }
        #caption { color: #89938d; font-size: 11px; }
        #divider { background: #303738; border: 0; }
        #connectionCard { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #202526, stop:1 #1b2021); border: 1px solid #363e3f; border-radius: 16px; }
        #connectionCard QLabel { background: transparent; }
        #locationBar { background: #191e1f; border: 1px solid #343c3d; border-radius: 16px; }
        #statusBadge { color: #a5aeaa; background: transparent; padding: 8px 0; font-size: 10px; letter-spacing: 1px; }
        #statusBadge[tone="connected"] { color: #b6ec62; }
        #statusBadge[tone="pending"] { color: #e6c881; }
        #serviceState { color: #949e98; font-size: 12px; }
        #serviceState[tone="connected"] { color: #bfd99a; }
        #serviceState[tone="pending"] { color: #e6c881; }
        #fieldLabel { font-weight: 600; margin-top: 6px; }
        QLineEdit, QComboBox { min-height: 44px; padding: 0 12px; color: #e7ece6; background: #1e2425; border: 1px solid #3b4444; border-radius: 10px; selection-background-color: #435835; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #b6ec62; }
        QLineEdit:disabled, QComboBox:disabled { color: #758079; background: #1b2021; border-color: #303838; }
        QComboBox { padding-right: 28px; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border: 0; }
        QComboBox::down-arrow { image: none; }
        QComboBox QAbstractItemView { color: #e7ece6; background: #202728; selection-background-color: #3c4d32; border: 1px solid #424e45; padding: 4px; outline: 0; }
        QComboBox QAbstractItemView::item { min-height: 32px; }
        QPushButton { min-height: 44px; padding: 0 16px; border-radius: 10px; border: 1px solid #3b4444; background: #202627; font-weight: 500; }
        QPushButton:hover { background: #29312c; border-color: #8ba55c; }
        QPushButton:focus { border: 2px solid #c6f28b; }
        QPushButton:pressed { background: #333e2f; }
        #primary, #connectButton { color: #14200b; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #c2ef79, stop:1 #a8df57); border: 1px solid #b6ec62; font-weight: 600; }
        #connectButton { font-size: 16px; min-height: 52px; }
        #primary:hover, #connectButton:hover { background: #c9f28e; }
        #connectButton[tone="connected"] { color: #d9edc4; background: #2b3a26; border-color: #6f8e4f; }
        #connectButton[tone="connected"]:hover { background: #3b4d30; }
        #protocolButton { padding: 0; text-align: left; min-height: 48px; }
        #homeLocation { min-height: 48px; }
        #homeSearch { min-height: 64px; }
        #protocolButton QLabel { background: transparent; font-size: 14px; }
        #secondary { background: transparent; }
        #linkButton { color: #b6dd8b; background: transparent; border: 1px solid transparent; padding-left: 0; text-align: left; }
        #linkButton:hover { color: #d7f8b5; }
        #linkButton:focus { border-color: #b6ec62; }
        #danger { color: #edb5aa; background: #352726; border-color: #704c46; }
        QPushButton:disabled, #primary:disabled, #connectButton:disabled { color: #859088; background: #2a302d; border-color: #3a423c; }
        #locationTile { padding: 0; min-height: 64px; background: #202627; border: 1px solid #353e3e; border-radius: 12px; }
        #locationTile[selected="true"] { border-color: #93b85a; background: #283022; }
        #locationTile:hover { border-color: #b6ec62; background: #2b3429; }
        #locationTile:focus { border: 2px solid #c6f28b; }
        #locationTile QLabel { background: transparent; font-weight: 400; }
        #tileCountry { font-size: 12px; color: #eceeeb; }
        #browseLocations { padding: 0; min-height: 64px; font-size: 17px; color: #9ca59f; }
        #accountName { font-size: 20px; font-weight: 600; }
        #errorBanner { color: #f2c1b8; background: #352727; border: 1px solid #684541; border-radius: 8px; padding: 12px; }
        QTableWidget { background: #1b2122; alternate-background-color: #202627; border: 1px solid #35403b; border-radius: 10px; selection-background-color: #3a4a2e; selection-color: #f1f9e7; }
        QTableWidget::item { padding: 0 12px; border-bottom: 1px solid #2b3430; }
        QHeaderView::section { min-height: 35px; color: #a9b7a9; background: #222a26; border: 0; border-bottom: 1px solid #3b473e; padding: 8px 12px; font-weight: 600; }
        QCheckBox { spacing: 12px; padding: 6px 0; border-bottom: 1px solid #2c3530; }
        QCheckBox:disabled { color: #758078; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #798874; border-radius: 4px; background: #1b2220; }
        QCheckBox::indicator:checked { background: #b6ec62; border: 3px solid #5f7d3d; }
        QCheckBox::indicator:disabled { border-color: #3a463e; background: #252c27; }
        #diagnostics { color: #b7c4b5; background: #1b2220; border: 1px solid #364239; border-radius: 10px; padding: 20px; font-family: "Consolas"; font-size: 12px; }
        QStatusBar { color: #b7c9a7; background: #15191a; font-size: 11px; }
        QStatusBar::item { border: 0; }
        QMenu { background: #202728; border: 1px solid #3e4a40; padding: 6px; }
        QMenu::item { padding: 8px 24px; }
        QMenu::item:selected { background: #3a4a2e; }
        QScrollBar:vertical { background: #15191a; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: #3d4941; border-radius: 4px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )"));
}

}
