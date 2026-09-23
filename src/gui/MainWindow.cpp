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
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
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

class ConnectionArt final : public QWidget
{
public:
    explicit ConnectionArt(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(160, 180);
        setMaximumWidth(280);
        setAccessibleName(QStringLiteral("Connection status illustration"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto side = qMin(width(), height());
        painter.translate(width() / 2.0, height() / 2.0);
        painter.scale(side / 240.0, side / 240.0);
        const auto connected = property("tone").toString() == QStringLiteral("connected");
        const QColor accent(connected ? QStringLiteral("#75ead1") : QStringLiteral("#7192a7"));
        QRadialGradient glow(0, 0, 114);
        glow.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), 24));
        glow.setColorAt(1, QColor(accent.red(), accent.green(), accent.blue(), 0));
        painter.setBrush(glow);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(0, 0), 114, 114);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 28), 1));
        painter.drawEllipse(QPointF(0, 0), 104, 104);
        painter.drawEllipse(QPointF(0, 0), 82, 82);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 65), 1));
        for (int angle = 0; angle < 360; angle += 30) {
            painter.save();
            painter.rotate(angle);
            painter.drawLine(QPointF(0, -99), QPointF(0, -104));
            painter.restore();
        }
        QPainterPath shield;
        shield.moveTo(0, -49);
        shield.lineTo(43, -32);
        shield.lineTo(39, 18);
        shield.quadTo(31, 45, 0, 59);
        shield.quadTo(-31, 45, -39, 18);
        shield.lineTo(-43, -32);
        shield.closeSubpath();
        painter.setBrush(QColor(QStringLiteral("#132c38")));
        painter.setPen(QPen(accent, 1.8));
        painter.drawPath(shield);
        painter.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (connected) {
            painter.drawLine(QPointF(-17, 3), QPointF(-4, 16));
            painter.drawLine(QPointF(-4, 16), QPointF(21, -12));
        } else {
            painter.drawLine(QPointF(-22, 17), QPointF(-4, -12));
            painter.drawLine(QPointF(-4, -12), QPointF(8, 7));
            painter.drawLine(QPointF(8, 7), QPointF(15, -3));
            painter.drawLine(QPointF(15, -3), QPointF(26, 17));
        }
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(73, -73), 4, 4);
    }
};

class NetworkArt final : public QWidget
{
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient backdrop(0, 0, width(), height());
        backdrop.setColorAt(0, QColor(QStringLiteral("#142f3c")));
        backdrop.setColorAt(1, QColor(QStringLiteral("#0b1926")));
        painter.fillRect(rect(), backdrop);
        painter.setPen(QPen(QColor(99, 230, 207, 12), 1));
        constexpr int spacing = 42;
        for (int x = 0; x < width(); x += spacing) painter.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += spacing) painter.drawLine(0, y, width(), y);
        const auto diameter = qMin(width(), height()) * 0.7;
        const QRectF globe((width() - diameter) / 2.0, height() * 0.20, diameter, diameter);
        painter.setPen(QPen(QColor(99, 230, 207, 95), 1.4));
        painter.drawEllipse(globe);
        painter.drawEllipse(QRectF(globe.center().x() - diameter * .18, globe.top(), diameter * .36, diameter));
        painter.drawEllipse(QRectF(globe.left(), globe.center().y() - diameter * .18, diameter, diameter * .36));
        painter.setBrush(QColor(255, 180, 92));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(globe.center().x() + diameter * .2, globe.center().y() - diameter * .21), 5, 5);
        painter.setPen(QColor(235, 244, 244));
        QFont font(QStringLiteral("Segoe UI"), 22, QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(QRectF(32, height() - 145, width() - 64, 110), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Private routes.\nPublic code."));
    }
};

}

MainWindow::MainWindow(QWidget *parent, ServiceMode serviceMode)
    : QMainWindow(parent), rpc_(this), serviceMode_(serviceMode)
{
    setWindowTitle(QStringLiteral("OpenNord"));
    resize(1180, 800);
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

QWidget *MainWindow::createSidebar()
{
    auto *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(200);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(20, 32, 20, 24);
    auto *brand = new QLabel(QStringLiteral("◈  OpenNord"));
    brand->setObjectName(QStringLiteral("brand"));
    layout->addWidget(brand);
    auto *edition = eyebrow(QStringLiteral("WINDOWS CLIENT"));
    edition->setObjectName(QStringLiteral("sidebarCaption"));
    layout->addWidget(edition);
    layout->addSpacing(32);
    navigation_ = new QListWidget;
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->setAccessibleName(QStringLiteral("Main navigation"));
    navigation_->addItems({QStringLiteral("Overview"), QStringLiteral("Locations"), QStringLiteral("Settings"), QStringLiteral("Account")});
    navigation_->setCurrentRow(0);
    navigation_->setVisible(false);
    layout->addWidget(navigation_, 1);
    auto *privacy = new QLabel(QStringLiteral("OPEN BY DESIGN\nGPLv3 · No telemetry\n\nCommunity-built. Independent."));
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
    art->setMinimumWidth(250);
    layout->addWidget(art, 4);
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
    layout->addWidget(panel, 6);
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
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 36, 40, 32);
    layout->setSpacing(20);
    auto *heading = new QHBoxLayout;
    heading->addWidget(eyebrow(QStringLiteral("YOUR CONNECTION")));
    heading->addStretch();
    homeStatus_ = new QLabel(QStringLiteral("CHECKING STATUS"));
    homeStatus_->setObjectName(QStringLiteral("statusBadge"));
    heading->addWidget(homeStatus_);
    layout->addLayout(heading);

    auto *hero = new QFrame;
    hero->setObjectName(QStringLiteral("heroCard"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(28, 30, 22, 30);
    heroLayout->setSpacing(12);
    auto *copy = new QVBoxLayout;
    copy->setSpacing(14);
    copy->addStretch();
    homeEyebrow_ = eyebrow(QStringLiteral("A CLEARER WAY TO CONNECT"));
    copy->addWidget(homeEyebrow_);
    homeTitle_ = title(QStringLiteral("Ready when you are"));
    copy->addWidget(homeTitle_);
    homeDescription_ = body(QStringLiteral("Connect to a recommended server, or choose a location that suits you."));
    copy->addWidget(homeDescription_);
    copy->addStretch();
    heroLayout->addLayout(copy, 3);
    connectionArt_ = new ConnectionArt;
    heroLayout->addWidget(connectionArt_, 2);
    layout->addWidget(hero, 1);

    auto *connectionCard = new QFrame;
    connectionCard->setObjectName(QStringLiteral("surfaceCard"));
    auto *actionRow = new QHBoxLayout(connectionCard);
    actionRow->setContentsMargins(24, 20, 24, 20);
    actionRow->setSpacing(20);
    auto *serverColumn = new QVBoxLayout;
    serverColumn->addWidget(eyebrow(QStringLiteral("DESTINATION")));
    homeServer_ = new QLabel(QStringLiteral("Best available location"));
    homeServer_->setObjectName(QStringLiteral("connectionServer"));
    homeServer_->setWordWrap(true);
    homeServer_->setTextFormat(Qt::PlainText);
    serverColumn->addWidget(homeServer_);
    homeLocationHint_ = body(QStringLiteral("Recommended automatically"));
    serverColumn->addWidget(homeLocationHint_);
    actionRow->addLayout(serverColumn, 1);
    powerButton_ = new QPushButton(QStringLiteral("Quick connect"));
    powerButton_->setObjectName(QStringLiteral("connectButton"));
    powerButton_->setMinimumSize(160, 48);
    actionRow->addWidget(powerButton_);
    layout->addWidget(connectionCard);

    auto *details = new QHBoxLayout;
    const auto addDetail = [details](const QString &label, QLabel *&value) {
        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("detailCard"));
        auto *column = new QVBoxLayout(card);
        column->setContentsMargins(20, 16, 20, 16);
        column->addWidget(eyebrow(label));
        value = body(QString{});
        column->addWidget(value);
        details->addWidget(card, 1);
    };
    addDetail(QStringLiteral("PROTOCOL"), homeProtocol_);
    addDetail(QStringLiteral("TUNNEL STATUS"), homeRoute_);
    layout->addLayout(details);
    homeError_ = new QLabel;
    homeError_->setObjectName(QStringLiteral("errorBanner"));
    homeError_->setWordWrap(true);
    homeError_->hide();
    layout->addWidget(homeError_);
    auto *browse = new QPushButton(QStringLiteral("Explore all locations  →"));
    browse->setObjectName(QStringLiteral("linkButton"));
    layout->addWidget(browse, 0, Qt::AlignLeft);
    connect(browse, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    connect(powerButton_, &QPushButton::clicked, this, [this] {
        if (!powerButton_->isEnabled()) return;
        const auto disconnecting = connectionStatus_ == QStringLiteral("connected") || connectionStatus_ == QStringLiteral("reconnecting");
        setBusy(true, disconnecting ? QStringLiteral("Disconnecting…") : QStringLiteral("Connecting…"));
        callService(disconnecting ? QStringLiteral("disconnect") : QStringLiteral("quickConnect"), {}, [this](QJsonValue, QString error) {
            setBusy(false);
            if (!error.isEmpty()) showError(error);
            refreshStatus();
        });
    });
    return scrollable(page);
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
    technology_ = new QComboBox;
    technology_->addItem(QStringLiteral("NordLynx (WireGuard)"), QStringLiteral("nordlynx"));
    technology_->addItem(QStringLiteral("OpenVPN"), QStringLiteral("openvpn"));
    openVpnProtocol_ = new QComboBox;
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
    if (!serviceAvailable_ && !busy_) statusBar()->clearMessage();
    serviceAvailable_ = true;
    loginServiceError_->hide();
    authenticated_ = state.value(QStringLiteral("authenticated")).toBool();
    wireGuardReady_ = state.value(QStringLiteral("wireGuardReady")).toBool();
    openVpnReady_ = state.value(QStringLiteral("openVpnReady")).toBool();
    selectedTechnology_ = state.value(QStringLiteral("technology")).toString(QStringLiteral("nordlynx"));
    selectedOpenVpnProtocol_ = state.value(QStringLiteral("openVpnProtocol")).toString(QStringLiteral("udp"));
    connectionStatus_ = state.value(QStringLiteral("status")).toString(QStringLiteral("unknown"));
    navigation_->setVisible(authenticated_);
    updateConnectionControls();
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
    const auto unknown = connectionStatus_ != QStringLiteral("connected") && connectionStatus_ != QStringLiteral("disconnected")
        && connectionStatus_ != QStringLiteral("error") && !reconnecting && !changing;
    homeTitle_->setText(connected ? QStringLiteral("Connected.\nStay private.")
        : reconnecting ? QStringLiteral("Reconnecting\nyour route…")
        : connectionStatus_ == QStringLiteral("connecting") ? QStringLiteral("Finding your\nprivate route…")
        : connectionStatus_ == QStringLiteral("disconnecting") ? QStringLiteral("Closing your\nconnection…")
        : unknown ? QStringLiteral("Checking your\nconnection…") : QStringLiteral("Ready when\nyou are."));
    const auto openVpn = selectedTechnology_ == QStringLiteral("openvpn");
    homeEyebrow_->setText(openVpn
        ? QStringLiteral("OPENVPN CONNECTION") : QStringLiteral("NORDLYNX CONNECTION"));
    homeProtocol_->setText(openVpn ? QStringLiteral("OpenVPN · %1").arg(selectedOpenVpnProtocol_.toUpper())
        : QStringLiteral("NordLynx · WireGuard"));
    homeStatus_->setText(connected ? QStringLiteral("CONNECTED") : reconnecting ? QStringLiteral("RECONNECTING")
        : changing ? connectionStatus_.toUpper() : unknown ? QStringLiteral("STATUS UNKNOWN") : QStringLiteral("NOT CONNECTED"));
    const auto tone = connected ? QStringLiteral("connected") : reconnecting || changing ? QStringLiteral("pending") : QStringLiteral("idle");
    setTone(homeStatus_, tone);
    setTone(powerButton_, tone);
    setTone(connectionArt_, tone);
    homeDescription_->setText(connected
        ? QStringLiteral("Traffic is routed through %1 with %2.")
            .arg(server.value(QStringLiteral("city")).toString(server.value(QStringLiteral("country")).toString()),
                 openVpn ? QStringLiteral("OpenVPN %1").arg(selectedOpenVpnProtocol_.toUpper()) : QStringLiteral("the native WireGuardNT tunnel"))
        : reconnecting ? QStringLiteral("The tunnel is retrying the selected route. You can disconnect at any time.")
        : changing ? QStringLiteral("Please wait while the VPN service updates your connection.")
        : unknown ? QStringLiteral("The service has not reported a known tunnel state yet.")
        : QStringLiteral("Connect to a recommended server, or explore the locations to find your next destination."));
    homeServer_->setText((connected || reconnecting) ? server.value(QStringLiteral("hostname")).toString() : QStringLiteral("Best available location"));
    homeLocationHint_->setText((connected || reconnecting) ? QStringLiteral("%1 · %2")
        .arg(server.value(QStringLiteral("city")).toString(), server.value(QStringLiteral("country")).toString())
        : QStringLiteral("Recommended automatically"));
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
}

void MainWindow::loadLocations(bool force)
{
    if (locationsInFlight_) return;
    if (!force && !locations_.isEmpty()) return updateLocationTable();
    locationsInFlight_ = true;
    updateConnectionControls();
    locationCount_->setText(QStringLiteral("Loading locations…"));
    const auto requestedTechnology = selectedTechnology_;
    const auto requestedProtocol = selectedOpenVpnProtocol_;
    callService(QStringLiteral("locations"), {}, [this, requestedTechnology, requestedProtocol](QJsonValue value, QString error) {
        locationsInFlight_ = false;
        updateConnectionControls();
        if (requestedTechnology != selectedTechnology_ || requestedProtocol != selectedOpenVpnProtocol_) {
            if (pages_->currentIndex() == LocationsPage) loadLocations(true);
            return;
        }
        if (!error.isEmpty()) {
            locationCount_->setText(QStringLiteral("Locations unavailable. Open Locations again to retry."));
            return showError(error);
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
    locationCount_->setText(serverTable_->rowCount() == 0 && !needle.isEmpty()
        ? QStringLiteral("No matching locations. Try another country or city.")
        : QStringLiteral("%1 of %2 locations").arg(serverTable_->rowCount()).arg(locations_.size()));
    if (serverTable_->rowCount() > 0) serverTable_->selectRow(0);
    updateConnectionControls();
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
    locationConnectButton_->setEnabled(canChange && selectedEngineReady() && !locationsInFlight_ && serverTable_->currentRow() >= 0);
    saveSettingsButton_->setEnabled(serviceAvailable_ && authenticated_ && !busy_);
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
    setTone(connectionArt_, QStringLiteral("idle"));
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
        * { font-family: "Segoe UI"; color: #e8eff4; font-size: 13px; }
        QMainWindow, QWidget { background: #0b1521; }
        QLabel { background: transparent; }
        QScrollArea { border: 0; }
        #sidebar { background: #08111b; border-right: 1px solid #1a2b39; }
        #brand { font-size: 22px; font-weight: 700; color: #f3f8fa; }
        #sidebarCaption { color: #748b9e; font-size: 9px; letter-spacing: 2px; padding-left: 3px; }
        #navigation { border: 0; outline: 0; background: transparent; }
        #navigation::item { min-height: 46px; padding-left: 16px; margin-bottom: 6px; border-radius: 8px; color: #91a7b8; }
        #navigation::item:hover { color: #edf5f7; background: #112331; }
        #navigation::item:selected { color: #8bf1d9; background: #143631; font-weight: 600; }
        #privacy { color: #7e94a5; font-size: 10px; }
        #eyebrow { color: #80bfb6; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
        #pageTitle { font-size: 36px; font-weight: 600; color: #f4f8fa; }
        #bodyText { color: #a0b3c2; font-size: 13px; }
        #heroCard { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #142b38, stop:1 #10212f); border: 1px solid #264250; border-radius: 16px; }
        #heroCard QWidget { background: transparent; }
        #surfaceCard { background: #112330; border: 1px solid #253d4c; border-radius: 12px; }
        #detailCard { background: #0e1e2b; border: 1px solid #203442; border-radius: 10px; }
        #statusBadge { color: #acbfcc; background: #182c3b; border: 1px solid #2b4354; border-radius: 12px; padding: 6px 12px; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
        #statusBadge[tone="connected"] { color: #8bf1d9; background: #143b33; border-color: #28604e; }
        #statusBadge[tone="pending"] { color: #f4ca87; background: #352c20; border-color: #665036; }
        #fieldLabel { font-weight: 600; margin-top: 6px; }
        QLineEdit, QComboBox { min-height: 42px; padding: 0 12px; color: #eaf2f3; background: #101f2d; border: 1px solid #304656; border-radius: 8px; selection-background-color: #28796f; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #75ead1; }
        QLineEdit:disabled, QComboBox:disabled { color: #6d8293; background: #101b27; border-color: #223441; }
        QComboBox QAbstractItemView { color: #eaf2f3; background: #112532; selection-background-color: #215044; border: 1px solid #304656; }
        QPushButton { min-height: 42px; padding: 0 18px; border-radius: 8px; border: 1px solid #365362; background: #172c3a; font-weight: 600; }
        QPushButton:hover { background: #1e3a49; border-color: #75ead1; }
        QPushButton:focus { border: 2px solid #b2ffee; }
        QPushButton:pressed { background: #234a55; }
        #primary, #connectButton { color: #082a25; background: #75ead1; border: 1px solid #75ead1; font-weight: 600; }
        #primary:hover, #connectButton:hover { background: #9af3de; }
        #connectButton[tone="connected"] { color: #c1e5df; background: #193a38; border-color: #48756c; }
        #connectButton[tone="connected"]:hover { background: #23504a; }
        #secondary { background: transparent; }
        #linkButton { color: #83dcca; background: transparent; border: 1px solid transparent; padding-left: 0; text-align: left; }
        #linkButton:hover { color: #b6ffed; }
        #linkButton:focus { border-color: #75ead1; }
        #danger { color: #ffbfc1; background: rgba(255,111,114,22); border-color: rgba(255,111,114,70); }
        QPushButton:disabled, #primary:disabled, #connectButton:disabled { color: #8098a8; background: #1a2d3a; border-color: #2a4050; }
        #connectionServer, #accountName { font-size: 19px; font-weight: 600; }
        #errorBanner { color: #ffc4bc; background: #352427; border: 1px solid #674247; border-radius: 8px; padding: 12px; }
        QTableWidget { background: #10212e; alternate-background-color: #122633; border: 1px solid #2a4050; border-radius: 8px; selection-background-color: #205044; selection-color: #e7fff6; }
        QTableWidget::item { padding: 0 12px; border-bottom: 1px solid #1c3442; }
        QHeaderView::section { min-height: 35px; color: #9db5c5; background: #142a38; border: 0; border-bottom: 1px solid #304656; padding: 8px 12px; font-weight: 600; }
        QCheckBox { spacing: 12px; padding: 6px 0; border-bottom: 1px solid #1d3242; }
        QCheckBox:disabled { color: #788d9d; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #668392; border-radius: 4px; background: #10212e; }
        QCheckBox::indicator:checked { background: #75ead1; border: 3px solid #28796f; }
        QCheckBox::indicator:disabled { border-color: #304656; background: #172733; }
        #diagnostics { color: #b6c8d4; background: #10212e; border: 1px solid #2a4050; border-radius: 10px; padding: 20px; font-family: "Consolas"; font-size: 12px; }
        QStatusBar { color: #a4cfc5; background: #08111b; }
        QMenu { background: #112532; border: 1px solid #304656; padding: 6px; }
        QMenu::item { padding: 8px 24px; }
        QMenu::item:selected { background: #215044; }
        QScrollBar:vertical { background: #0b1521; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: #304858; border-radius: 5px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )"));
}

}
