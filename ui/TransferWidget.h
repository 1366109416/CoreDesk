#pragma once

#include "coredesk/protocol/JsonPayload.h"

#include <QWidget>

#include <cstdint>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;

namespace coredesk::ui {

class TransferWidget : public QWidget {
public:
    using SimpleCallback = std::function<void()>;
    using DirectorySelectedCallback = std::function<void(const QString&)>;
    using SendFileCallback = std::function<void(const QString&, const QString&, std::uint16_t)>;

    explicit TransferWidget(QWidget* parent = nullptr);

    void set_enable_requested_callback(SimpleCallback callback);
    void set_disable_requested_callback(SimpleCallback callback);
    void set_receive_directory_selected_callback(DirectorySelectedCallback callback);
    void set_send_file_requested_callback(SendFileCallback callback);

    void set_offline_state();
    void set_unavailable_state(const QString& message);
    void set_transfer_status(const protocol::GetTransferStatusResponsePayload& status);
    void set_pending(bool pending);
    void show_error(const QString& message);
    void clear_error();
    void set_send_inputs(const QString& file_path, const QString& host, const QString& port);
    void request_send_for_testing();
    void set_sending(bool sending);
    void show_send_success();
    void show_send_failure(const QString& message);

    QString status_text() const;
    QString port_text() const;
    QString receive_directory_text() const;
    QString active_transfers_text() const;
    QString error_text() const;
    bool enable_button_enabled() const;
    bool disable_button_enabled() const;
    bool choose_folder_button_enabled() const;
    bool send_button_enabled() const;
    QString send_status_text() const;

private:
    enum class DisplayState {
        Offline,
        Disabled,
        Enabled,
        Unavailable
    };

    void choose_receive_directory();
    void choose_send_file();
    void request_send();
    void apply_controls();
    void set_display_state(DisplayState state);

    QLabel* status_value_{};
    QLabel* port_value_{};
    QLabel* receive_directory_value_{};
    QLabel* active_transfers_value_{};
    QLabel* error_label_{};
    QPushButton* enable_button_{};
    QPushButton* disable_button_{};
    QPushButton* choose_folder_button_{};
    QLineEdit* send_file_edit_{};
    QLineEdit* send_host_edit_{};
    QLineEdit* send_port_edit_{};
    QPushButton* browse_send_file_button_{};
    QPushButton* send_file_button_{};
    QLabel* send_status_value_{};

    DisplayState state_{DisplayState::Offline};
    bool pending_{false};
    bool sending_{false};
    SimpleCallback enable_requested_callback_;
    SimpleCallback disable_requested_callback_;
    DirectorySelectedCallback receive_directory_selected_callback_;
    SendFileCallback send_file_requested_callback_;
};

} // namespace coredesk::ui
