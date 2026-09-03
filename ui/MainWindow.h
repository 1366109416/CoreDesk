#pragma once

#include "LocalIpcClient.h"
#include "coredesk/protocol/Frame.h"

#include <QMainWindow>
#include <QProcess>
#include <QTime>

#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTimer;

namespace coredesk::ui {

class SearchWidget;
class TransferWidget;

class MainWindow : public QMainWindow {
public:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        StartingService,
        Connected,
        Offline
    };

    explicit MainWindow(bool auto_connect_on_start = true, QWidget* parent = nullptr);

    void retry_connection();
    void handle_frame(const protocol::Frame& frame);
    void handle_connected();
    void handle_disconnected();
    void handle_connection_error(const Error& error);

    ConnectionState connection_state() const noexcept;
    bool is_scanning() const noexcept;
    QString status_text() const;
    SearchWidget* search_widget() const noexcept;
    TransferWidget* transfer_widget() const noexcept;
    void set_root_path(const QString& path);
    QString root_path() const;
    void set_latest_search_request_id_for_testing(RequestId request_id);
    void set_active_scan_request_id_for_testing(RequestId request_id);
    void set_pending_transfer_request_id_for_testing(protocol::MessageType type, RequestId request_id);

private:
    void build_ui();
    void wire_callbacks();
    void browse_root();
    void handle_scan_button();
    void start_scan();
    void cancel_scan();
    void send_search(const QString& query);
    void enable_lan_transfer();
    void disable_lan_transfer();
    void set_receive_directory(const QString& path);
    void request_transfer_status();
    void send_file(const QString& file_path, const QString& host, std::uint16_t port);
    void attempt_connect();
    void start_service_process();
    QString service_executable_path() const;
    void set_connection_state(ConnectionState state);
    void set_scan_active(bool active);
    void invalidate_pending_requests();
    void update_status_line();
    void apply_search_response(const protocol::Frame& frame);
    void apply_scan_progress(const protocol::Frame& frame);
    void apply_scan_completed(const protocol::Frame& frame);
    void apply_scan_failed(const protocol::Frame& frame);
    void apply_error_payload(const protocol::Frame& frame);
    void apply_enable_lan_transfer_response(const protocol::Frame& frame);
    void apply_disable_lan_transfer_response(const protocol::Frame& frame);
    void apply_set_receive_directory_response(const protocol::Frame& frame);
    void apply_get_transfer_status_response(const protocol::Frame& frame);
    void apply_send_file_accepted(const protocol::Frame& frame);
    void apply_send_file_result(const protocol::Frame& frame);
    bool apply_transfer_error(const protocol::Frame& frame);
    void sync_transfer_status_after_operation();

    qt_ipc::LocalIpcClient client_;
    QLineEdit* root_path_edit_{};
    QPushButton* browse_button_{};
    QPushButton* scan_button_{};
    QPushButton* retry_button_{};
    QLabel* status_label_{};
    QTabWidget* tabs_{};
    SearchWidget* search_widget_{};
    TransferWidget* transfer_widget_{};
    QTimer* retry_timer_{};

    ConnectionState connection_state_{ConnectionState::Disconnected};
    bool auto_start_attempted_{false};
    bool scanning_{false};
    QTime retry_deadline_;
    std::optional<RequestId> latest_search_request_id_;
    std::optional<RequestId> active_scan_request_id_;
    std::optional<RequestId> pending_enable_transfer_request_id_;
    std::optional<RequestId> pending_disable_transfer_request_id_;
    std::optional<RequestId> pending_set_receive_directory_request_id_;
    std::optional<RequestId> pending_transfer_status_request_id_;
    std::optional<RequestId> pending_send_file_request_id_;
    QString last_error_;
    std::uint64_t file_count_{0};
    std::uint64_t generation_{0};
    std::uint64_t search_elapsed_us_{0};
};

} // namespace coredesk::ui
