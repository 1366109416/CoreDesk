#include "MainWindow.h"

#include "SearchWidget.h"
#include "coredesk/protocol/JsonPayload.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace coredesk::ui {
namespace {

QString state_text(MainWindow::ConnectionState state)
{
    switch (state) {
    case MainWindow::ConnectionState::Disconnected:
        return QStringLiteral("Disconnected");
    case MainWindow::ConnectionState::Connecting:
        return QStringLiteral("Connecting");
    case MainWindow::ConnectionState::StartingService:
        return QStringLiteral("Starting service");
    case MainWindow::ConnectionState::Connected:
        return QStringLiteral("Ready");
    case MainWindow::ConnectionState::Offline:
        return QStringLiteral("Offline");
    }
    return QStringLiteral("Unknown");
}

std::string utf8_string(const QString& text)
{
    const auto utf8 = text.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

} // namespace

MainWindow::MainWindow(bool auto_connect_on_start, QWidget* parent)
    : QMainWindow(parent)
    , retry_timer_(new QTimer(this))
{
    build_ui();
    wire_callbacks();
    retry_timer_->setInterval(100);
    QObject::connect(retry_timer_, &QTimer::timeout, this, [this]() {
        if (QTime::currentTime() >= retry_deadline_) {
            retry_timer_->stop();
            set_connection_state(ConnectionState::Offline);
            last_error_ = QStringLiteral("Unable to connect to CoreDesk service.");
            update_status_line();
            return;
        }
        attempt_connect();
    });

    if (auto_connect_on_start) {
        QTimer::singleShot(0, this, [this]() {
            retry_connection();
        });
    } else {
        set_connection_state(ConnectionState::Offline);
    }
}

void MainWindow::retry_connection()
{
    invalidate_pending_requests();
    last_error_.clear();
    auto_start_attempted_ = false;
    retry_deadline_ = QTime::currentTime().addMSecs(3000);
    retry_timer_->stop();
    set_connection_state(ConnectionState::Connecting);
    attempt_connect();
}

void MainWindow::handle_frame(const protocol::Frame& frame)
{
    switch (frame.type) {
    case protocol::MessageType::Pong:
        return;
    case protocol::MessageType::ScanAccepted:
        if (active_scan_request_id_ && frame.request_id == *active_scan_request_id_) {
            set_scan_active(true);
        }
        return;
    case protocol::MessageType::ScanProgress:
        apply_scan_progress(frame);
        return;
    case protocol::MessageType::ScanCompleted:
        apply_scan_completed(frame);
        return;
    case protocol::MessageType::ScanFailed:
        apply_scan_failed(frame);
        return;
    case protocol::MessageType::SearchResponse:
        apply_search_response(frame);
        return;
    default:
        return;
    }
}

void MainWindow::handle_connected()
{
    retry_timer_->stop();
    last_error_.clear();
    set_connection_state(ConnectionState::Connected);
}

void MainWindow::handle_disconnected()
{
    if (connection_state_ == ConnectionState::Connecting || connection_state_ == ConnectionState::StartingService) {
        update_status_line();
        return;
    }
    retry_timer_->stop();
    invalidate_pending_requests();
    set_scan_active(false);
    set_connection_state(ConnectionState::Offline);
}

void MainWindow::handle_connection_error(const Error& error)
{
    last_error_ = QString::fromStdString(error.message);
    if (connection_state_ == ConnectionState::Connecting || connection_state_ == ConnectionState::StartingService) {
        if (!auto_start_attempted_) {
            start_service_process();
            auto_start_attempted_ = true;
            set_connection_state(ConnectionState::StartingService);
        }
        if (QTime::currentTime() < retry_deadline_ && !retry_timer_->isActive()) {
            retry_timer_->start();
            return;
        }
    }
    if (connection_state_ != ConnectionState::Connected) {
        set_connection_state(ConnectionState::Offline);
    }
}

MainWindow::ConnectionState MainWindow::connection_state() const noexcept
{
    return connection_state_;
}

bool MainWindow::is_scanning() const noexcept
{
    return scanning_;
}

QString MainWindow::status_text() const
{
    return status_label_->text();
}

SearchWidget* MainWindow::search_widget() const noexcept
{
    return search_widget_;
}

void MainWindow::set_root_path(const QString& path)
{
    root_path_edit_->setText(path);
}

QString MainWindow::root_path() const
{
    return root_path_edit_->text();
}

void MainWindow::set_latest_search_request_id_for_testing(RequestId request_id)
{
    latest_search_request_id_ = request_id;
}

void MainWindow::set_active_scan_request_id_for_testing(RequestId request_id)
{
    active_scan_request_id_ = request_id;
}

void MainWindow::build_ui()
{
    setWindowTitle(QStringLiteral("CoreDesk"));
    resize(960, 640);

    auto* central = new QWidget(this);
    auto* main_layout = new QVBoxLayout(central);

    auto* root_layout = new QHBoxLayout();
    root_layout->addWidget(new QLabel(QStringLiteral("Root:"), central));
    root_path_edit_ = new QLineEdit(central);
    root_path_edit_->setObjectName(QStringLiteral("rootPathEdit"));
    browse_button_ = new QPushButton(QStringLiteral("Browse"), central);
    browse_button_->setObjectName(QStringLiteral("browseButton"));
    scan_button_ = new QPushButton(QStringLiteral("Scan"), central);
    scan_button_->setObjectName(QStringLiteral("scanButton"));
    retry_button_ = new QPushButton(QStringLiteral("Retry"), central);
    retry_button_->setObjectName(QStringLiteral("retryButton"));
    root_layout->addWidget(root_path_edit_, 1);
    root_layout->addWidget(browse_button_);
    root_layout->addWidget(scan_button_);
    root_layout->addWidget(retry_button_);
    main_layout->addLayout(root_layout);

    status_label_ = new QLabel(central);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    main_layout->addWidget(status_label_);

    tabs_ = new QTabWidget(central);
    search_widget_ = new SearchWidget(tabs_);
    tabs_->addTab(search_widget_, QStringLiteral("Search"));
    auto* transfer_placeholder = new QWidget(tabs_);
    auto* transfer_layout = new QVBoxLayout(transfer_placeholder);
    transfer_layout->addWidget(new QLabel(QStringLiteral("LAN Transfer will be implemented in M6"), transfer_placeholder));
    transfer_layout->addStretch(1);
    tabs_->addTab(transfer_placeholder, QStringLiteral("LAN Transfer"));
    main_layout->addWidget(tabs_, 1);

    setCentralWidget(central);
    retry_button_->setVisible(false);
    update_status_line();
}

void MainWindow::wire_callbacks()
{
    QObject::connect(browse_button_, &QPushButton::clicked, this, [this]() {
        browse_root();
    });
    QObject::connect(scan_button_, &QPushButton::clicked, this, [this]() {
        handle_scan_button();
    });
    QObject::connect(retry_button_, &QPushButton::clicked, this, [this]() {
        retry_connection();
    });

    search_widget_->set_search_requested_callback([this](const QString& query) {
        send_search(query);
    });
    search_widget_->set_query_changed_callback([this](const QString&) {
        latest_search_request_id_.reset();
    });
    client_.set_frame_callback([this](const protocol::Frame& frame) {
        handle_frame(frame);
    });
    client_.set_connected_callback([this]() {
        handle_connected();
    });
    client_.set_disconnected_callback([this]() {
        handle_disconnected();
    });
    client_.set_error_callback([this](const Error& error) {
        handle_connection_error(error);
    });
}

void MainWindow::browse_root()
{
    const auto selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select root directory"), root_path_edit_->text());
    if (!selected.isEmpty()) {
        root_path_edit_->setText(selected);
    }
}

void MainWindow::handle_scan_button()
{
    if (scanning_) {
        cancel_scan();
    } else {
        start_scan();
    }
}

void MainWindow::start_scan()
{
    if (!client_.is_connected()) {
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    const auto root = root_path_edit_->text().trimmed();
    if (root.isEmpty()) {
        last_error_ = QStringLiteral("Choose a root directory before scanning.");
        update_status_line();
        return;
    }
    last_error_.clear();
    protocol::ScanRequestPayload payload;
    payload.root = utf8_string(root);
    payload.include_dot_hidden = false;
    payload.follow_directory_symlinks = false;
    payload.worker_count = 0;
    active_scan_request_id_ = client_.send_scan_request(payload);
    set_scan_active(true);
}

void MainWindow::cancel_scan()
{
    if (!client_.is_connected()) {
        handle_disconnected();
        return;
    }
    client_.send_cancel_scan();
}

void MainWindow::send_search(const QString& query)
{
    if (!client_.is_connected()) {
        search_widget_->clear_results();
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    latest_search_request_id_ = client_.send_search_request(protocol::SearchRequestPayload{utf8_string(query), 100});
}

void MainWindow::attempt_connect()
{
    client_.connect_to_server_async(QStringLiteral("CoreDesk.Service.v1"));
}

void MainWindow::start_service_process()
{
    const auto executable = service_executable_path();
    const auto working_dir = QCoreApplication::applicationDirPath();
    if (!QProcess::startDetached(executable, {}, working_dir)) {
        last_error_ = QStringLiteral("Unable to start coredesk_service.");
    }
}

QString MainWindow::service_executable_path() const
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/coredesk_service.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/coredesk_service");
#endif
}

void MainWindow::set_connection_state(ConnectionState state)
{
    connection_state_ = state;
    retry_button_->setVisible(state == ConnectionState::Offline);
    update_status_line();
}

void MainWindow::set_scan_active(bool active)
{
    scanning_ = active;
    scan_button_->setText(active ? QStringLiteral("Cancel") : QStringLiteral("Scan"));
    browse_button_->setEnabled(!active);
    root_path_edit_->setEnabled(!active);
    update_status_line();
}

void MainWindow::invalidate_pending_requests()
{
    latest_search_request_id_.reset();
    active_scan_request_id_.reset();
}

void MainWindow::update_status_line()
{
    const auto scan_text = scanning_ ? QStringLiteral("Scanning") : QStringLiteral("Idle");
    const auto error_text = last_error_.isEmpty() ? QString{} : QStringLiteral(" | ") + last_error_;
    status_label_->setText(QStringLiteral("Status: %1 | %2 | %3 entries | search %4 us | generation %5%6")
                               .arg(state_text(connection_state_))
                               .arg(scan_text)
                               .arg(file_count_)
                               .arg(search_elapsed_us_)
                               .arg(generation_)
                               .arg(error_text));
}

void MainWindow::apply_search_response(const protocol::Frame& frame)
{
    if (!latest_search_request_id_ || frame.request_id != *latest_search_request_id_) {
        return;
    }

    auto decoded = protocol::decode_search_response_payload(frame.payload);
    if (!decoded.ok()) {
        auto error = protocol::decode_error_response_payload(frame.payload);
        if (error.ok()) {
            last_error_ = QString::fromStdString(error.value().message);
            search_widget_->clear_results();
            update_status_line();
        }
        return;
    }

    generation_ = decoded.value().generation;
    search_elapsed_us_ = decoded.value().elapsed_us;
    search_widget_->render_results(decoded.value());
    update_status_line();
}

void MainWindow::apply_scan_progress(const protocol::Frame& frame)
{
    if (!active_scan_request_id_ || frame.request_id != *active_scan_request_id_) {
        return;
    }
    auto decoded = protocol::decode_scan_progress_payload(frame.payload);
    if (decoded.ok()) {
        file_count_ = decoded.value().processed;
        set_scan_active(true);
    }
}

void MainWindow::apply_scan_completed(const protocol::Frame& frame)
{
    if (!active_scan_request_id_ || frame.request_id != *active_scan_request_id_) {
        return;
    }
    auto decoded = protocol::decode_scan_completed_payload(frame.payload);
    if (decoded.ok()) {
        last_error_.clear();
        generation_ = decoded.value().generation;
        file_count_ = decoded.value().file_count;
        active_scan_request_id_.reset();
        set_scan_active(false);
    }
}

void MainWindow::apply_scan_failed(const protocol::Frame& frame)
{
    if (!active_scan_request_id_ || frame.request_id != *active_scan_request_id_) {
        return;
    }
    apply_error_payload(frame);
    active_scan_request_id_.reset();
    set_scan_active(false);
}

void MainWindow::apply_error_payload(const protocol::Frame& frame)
{
    auto error = protocol::decode_error_response_payload(frame.payload);
    if (error.ok()) {
        last_error_ = QString::fromStdString(error.value().message);
        update_status_line();
    }
}

} // namespace coredesk::ui
