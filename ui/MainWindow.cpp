#include "MainWindow.h"

#include "SearchWidget.h"
#include "TransferWidget.h"
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
    case protocol::MessageType::EnableLanTransferResponse:
        apply_enable_lan_transfer_response(frame);
        return;
    case protocol::MessageType::DisableLanTransferResponse:
        apply_disable_lan_transfer_response(frame);
        return;
    case protocol::MessageType::SetReceiveDirectoryResponse:
        apply_set_receive_directory_response(frame);
        return;
    case protocol::MessageType::GetTransferStatusResponse:
        apply_get_transfer_status_response(frame);
        return;
    case protocol::MessageType::SendFileAccepted:
        apply_send_file_accepted(frame);
        return;
    case protocol::MessageType::SendFileResult:
        apply_send_file_result(frame);
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
    request_transfer_status();
}

void MainWindow::handle_disconnected()
{
    if (connection_state_ == ConnectionState::Connecting || connection_state_ == ConnectionState::StartingService) {
        update_status_line();
        return;
    }
    retry_timer_->stop();
    const bool outgoing_was_pending = pending_send_file_request_id_.has_value();
    invalidate_pending_requests();
    set_scan_active(false);
    transfer_widget_->set_offline_state();
    if (outgoing_was_pending) {
        transfer_widget_->show_send_failure(QStringLiteral("Service disconnected during file transfer."));
    }
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

TransferWidget* MainWindow::transfer_widget() const noexcept
{
    return transfer_widget_;
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

void MainWindow::set_pending_transfer_request_id_for_testing(protocol::MessageType type, RequestId request_id)
{
    switch (type) {
    case protocol::MessageType::EnableLanTransferResponse:
        pending_enable_transfer_request_id_ = request_id;
        break;
    case protocol::MessageType::DisableLanTransferResponse:
        pending_disable_transfer_request_id_ = request_id;
        break;
    case protocol::MessageType::SetReceiveDirectoryResponse:
        pending_set_receive_directory_request_id_ = request_id;
        break;
    case protocol::MessageType::GetTransferStatusResponse:
        pending_transfer_status_request_id_ = request_id;
        break;
    case protocol::MessageType::SendFileAccepted:
    case protocol::MessageType::SendFileResult:
        pending_send_file_request_id_ = request_id;
        break;
    default:
        break;
    }
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
    transfer_widget_ = new TransferWidget(tabs_);
    tabs_->addTab(transfer_widget_, QStringLiteral("LAN Transfer"));
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
    transfer_widget_->set_enable_requested_callback([this]() {
        enable_lan_transfer();
    });
    transfer_widget_->set_disable_requested_callback([this]() {
        disable_lan_transfer();
    });
    transfer_widget_->set_receive_directory_selected_callback([this](const QString& path) {
        set_receive_directory(path);
    });
    transfer_widget_->set_send_file_requested_callback(
        [this](const QString& file_path, const QString& host, std::uint16_t port) {
            send_file(file_path, host, port);
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

void MainWindow::enable_lan_transfer()
{
    if (!client_.is_connected()) {
        transfer_widget_->set_offline_state();
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    transfer_widget_->set_pending(true);
    pending_enable_transfer_request_id_ = client_.send_enable_lan_transfer_request();
}

void MainWindow::disable_lan_transfer()
{
    if (!client_.is_connected()) {
        transfer_widget_->set_offline_state();
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    transfer_widget_->set_pending(true);
    pending_disable_transfer_request_id_ = client_.send_disable_lan_transfer_request();
}

void MainWindow::set_receive_directory(const QString& path)
{
    if (!client_.is_connected()) {
        transfer_widget_->set_offline_state();
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    transfer_widget_->set_pending(true);
    pending_set_receive_directory_request_id_ =
        client_.send_set_receive_directory_request(protocol::SetReceiveDirectoryRequestPayload{utf8_string(path)});
}

void MainWindow::request_transfer_status()
{
    if (!client_.is_connected()) {
        transfer_widget_->set_offline_state();
        return;
    }
    transfer_widget_->set_pending(true);
    pending_transfer_status_request_id_ = client_.send_get_transfer_status_request();
}

void MainWindow::send_file(const QString& file_path, const QString& host, std::uint16_t port)
{
    if (!client_.is_connected()) {
        transfer_widget_->set_offline_state();
        last_error_ = QStringLiteral("CoreDesk service is offline.");
        set_connection_state(ConnectionState::Offline);
        return;
    }
    pending_send_file_request_id_ = client_.send_file_request(
        protocol::SendFileRequestPayload{utf8_string(file_path), utf8_string(host), port});
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
    pending_enable_transfer_request_id_.reset();
    pending_disable_transfer_request_id_.reset();
    pending_set_receive_directory_request_id_.reset();
    pending_transfer_status_request_id_.reset();
    pending_send_file_request_id_.reset();
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

void MainWindow::apply_enable_lan_transfer_response(const protocol::Frame& frame)
{
    if (!pending_enable_transfer_request_id_ || frame.request_id != *pending_enable_transfer_request_id_) {
        return;
    }
    pending_enable_transfer_request_id_.reset();

    auto decoded = protocol::decode_enable_lan_transfer_response_payload(frame.payload);
    if (!decoded.ok() && !apply_transfer_error(frame)) {
        transfer_widget_->show_error(QString::fromStdString(decoded.error().message));
    }
    sync_transfer_status_after_operation();
}

void MainWindow::apply_disable_lan_transfer_response(const protocol::Frame& frame)
{
    if (!pending_disable_transfer_request_id_ || frame.request_id != *pending_disable_transfer_request_id_) {
        return;
    }
    pending_disable_transfer_request_id_.reset();

    auto decoded = protocol::decode_disable_lan_transfer_response_payload(frame.payload);
    if (!decoded.ok() && !apply_transfer_error(frame)) {
        transfer_widget_->show_error(QString::fromStdString(decoded.error().message));
    }
    sync_transfer_status_after_operation();
}

void MainWindow::apply_set_receive_directory_response(const protocol::Frame& frame)
{
    if (!pending_set_receive_directory_request_id_ ||
        frame.request_id != *pending_set_receive_directory_request_id_) {
        return;
    }
    pending_set_receive_directory_request_id_.reset();

    auto decoded = protocol::decode_set_receive_directory_response_payload(frame.payload);
    if (!decoded.ok() && !apply_transfer_error(frame)) {
        transfer_widget_->show_error(QString::fromStdString(decoded.error().message));
    }
    sync_transfer_status_after_operation();
}

void MainWindow::apply_get_transfer_status_response(const protocol::Frame& frame)
{
    if (!pending_transfer_status_request_id_ || frame.request_id != *pending_transfer_status_request_id_) {
        return;
    }
    pending_transfer_status_request_id_.reset();

    auto decoded = protocol::decode_get_transfer_status_response_payload(frame.payload);
    if (decoded.ok()) {
        transfer_widget_->set_transfer_status(decoded.value());
        transfer_widget_->set_pending(false);
        return;
    }
    if (!apply_transfer_error(frame)) {
        transfer_widget_->show_error(QString::fromStdString(decoded.error().message));
    }
    transfer_widget_->set_pending(false);
}

void MainWindow::apply_send_file_accepted(const protocol::Frame& frame)
{
    if (!pending_send_file_request_id_ || frame.request_id != *pending_send_file_request_id_) {
        return;
    }
    auto decoded = protocol::decode_send_file_accepted_payload(frame.payload);
    if (decoded.ok() && decoded.value().accepted) {
        transfer_widget_->set_sending(true);
        return;
    }
    pending_send_file_request_id_.reset();
    if (!apply_transfer_error(frame)) {
        const auto message = decoded.ok() ? QStringLiteral("Service rejected the send request.")
                                          : QString::fromStdString(decoded.error().message);
        transfer_widget_->show_send_failure(message);
    } else {
        transfer_widget_->show_send_failure(transfer_widget_->error_text());
    }
}

void MainWindow::apply_send_file_result(const protocol::Frame& frame)
{
    if (!pending_send_file_request_id_ || frame.request_id != *pending_send_file_request_id_) {
        return;
    }
    pending_send_file_request_id_.reset();
    auto decoded = protocol::decode_send_file_result_payload(frame.payload);
    if (!decoded.ok()) {
        if (!apply_transfer_error(frame)) {
            transfer_widget_->show_send_failure(QString::fromStdString(decoded.error().message));
        } else {
            transfer_widget_->show_send_failure(transfer_widget_->error_text());
        }
        return;
    }
    if (decoded.value().success) {
        transfer_widget_->show_send_success();
    } else {
        transfer_widget_->show_send_failure(QString::fromStdString(decoded.value().message));
    }
}

bool MainWindow::apply_transfer_error(const protocol::Frame& frame)
{
    auto error = protocol::decode_error_response_payload(frame.payload);
    if (!error.ok()) {
        return false;
    }

    const auto message = QString::fromStdString(error.value().message);
    if (error.value().code == ErrorCode::ConnectionFailed &&
        message.contains(QStringLiteral("LAN transfer"), Qt::CaseInsensitive) &&
        message.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)) {
        transfer_widget_->set_unavailable_state(message);
    } else {
        transfer_widget_->show_error(message);
    }
    last_error_ = message;
    update_status_line();
    return true;
}

void MainWindow::sync_transfer_status_after_operation()
{
    if (client_.is_connected()) {
        request_transfer_status();
    } else {
        transfer_widget_->set_offline_state();
    }
}

} // namespace coredesk::ui
