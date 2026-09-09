#include "TransferWidget.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace coredesk::ui {
namespace {

QString format_port(std::uint16_t port)
{
    return port == 0 ? QStringLiteral("-") : QString::number(port);
}

} // namespace

TransferWidget::TransferWidget(QWidget* parent)
    : QWidget(parent)
    , status_value_(new QLabel(this))
    , port_value_(new QLabel(this))
    , receive_directory_value_(new QLabel(this))
    , active_transfers_value_(new QLabel(this))
    , error_label_(new QLabel(this))
    , enable_button_(new QPushButton(QStringLiteral("Enable LAN Transfer"), this))
    , disable_button_(new QPushButton(QStringLiteral("Disable LAN Transfer"), this))
    , choose_folder_button_(new QPushButton(QStringLiteral("Choose Folder"), this))
    , send_file_edit_(new QLineEdit(this))
    , send_host_edit_(new QLineEdit(this))
    , send_port_edit_(new QLineEdit(this))
    , browse_send_file_button_(new QPushButton(QStringLiteral("Browse..."), this))
    , send_file_button_(new QPushButton(QStringLiteral("Send File"), this))
    , send_status_value_(new QLabel(this))
{
    status_value_->setObjectName(QStringLiteral("transferStatusValue"));
    port_value_->setObjectName(QStringLiteral("transferPortValue"));
    receive_directory_value_->setObjectName(QStringLiteral("transferReceiveDirectoryValue"));
    active_transfers_value_->setObjectName(QStringLiteral("transferActiveTransfersValue"));
    error_label_->setObjectName(QStringLiteral("transferErrorLabel"));
    enable_button_->setObjectName(QStringLiteral("enableLanTransferButton"));
    disable_button_->setObjectName(QStringLiteral("disableLanTransferButton"));
    choose_folder_button_->setObjectName(QStringLiteral("chooseReceiveDirectoryButton"));
    send_file_edit_->setObjectName(QStringLiteral("sendFilePathEdit"));
    send_host_edit_->setObjectName(QStringLiteral("sendHostEdit"));
    send_port_edit_->setObjectName(QStringLiteral("sendPortEdit"));
    browse_send_file_button_->setObjectName(QStringLiteral("browseSendFileButton"));
    send_file_button_->setObjectName(QStringLiteral("sendFileButton"));
    send_status_value_->setObjectName(QStringLiteral("sendStatusValue"));
    send_host_edit_->setPlaceholderText(QStringLiteral("127.0.0.1"));
    send_port_edit_->setPlaceholderText(QStringLiteral("45827"));

    receive_directory_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_label_->setWordWrap(true);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Status:"), status_value_);
    form->addRow(QStringLiteral("Port:"), port_value_);
    form->addRow(QStringLiteral("Receive directory:"), receive_directory_value_);
    form->addRow(QStringLiteral("Active transfers:"), active_transfers_value_);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(enable_button_);
    buttons->addWidget(disable_button_);
    buttons->addWidget(choose_folder_button_);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);
    auto* send_file_row = new QHBoxLayout();
    send_file_row->addWidget(send_file_edit_, 1);
    send_file_row->addWidget(browse_send_file_button_);
    layout->addWidget(new QLabel(QStringLiteral("Send a file:"), this));
    layout->addLayout(send_file_row);
    auto* target_row = new QHBoxLayout();
    target_row->addWidget(new QLabel(QStringLiteral("Host:"), this));
    target_row->addWidget(send_host_edit_, 1);
    target_row->addWidget(new QLabel(QStringLiteral("Port:"), this));
    target_row->addWidget(send_port_edit_);
    target_row->addWidget(send_file_button_);
    layout->addLayout(target_row);
    layout->addWidget(send_status_value_);
    layout->addWidget(error_label_);
    layout->addStretch(1);
    setLayout(layout);

    QObject::connect(enable_button_, &QPushButton::clicked, this, [this]() {
        if (enable_requested_callback_) {
            enable_requested_callback_();
        }
    });
    QObject::connect(disable_button_, &QPushButton::clicked, this, [this]() {
        if (disable_requested_callback_) {
            disable_requested_callback_();
        }
    });
    QObject::connect(choose_folder_button_, &QPushButton::clicked, this, [this]() {
        choose_receive_directory();
    });
    QObject::connect(browse_send_file_button_, &QPushButton::clicked, this, [this]() {
        choose_send_file();
    });
    QObject::connect(send_file_button_, &QPushButton::clicked, this, [this]() {
        request_send();
    });

    set_offline_state();
}

void TransferWidget::set_enable_requested_callback(SimpleCallback callback)
{
    enable_requested_callback_ = std::move(callback);
}

void TransferWidget::set_disable_requested_callback(SimpleCallback callback)
{
    disable_requested_callback_ = std::move(callback);
}

void TransferWidget::set_receive_directory_selected_callback(DirectorySelectedCallback callback)
{
    receive_directory_selected_callback_ = std::move(callback);
}

void TransferWidget::set_send_file_requested_callback(SendFileCallback callback)
{
    send_file_requested_callback_ = std::move(callback);
}

void TransferWidget::set_offline_state()
{
    sending_ = false;
    set_display_state(DisplayState::Offline);
    status_value_->setText(QStringLiteral("Offline"));
    port_value_->setText(QStringLiteral("-"));
    active_transfers_value_->setText(QStringLiteral("-"));
    clear_error();
}

void TransferWidget::set_unavailable_state(const QString& message)
{
    sending_ = false;
    set_display_state(DisplayState::Unavailable);
    status_value_->setText(QStringLiteral("Unavailable"));
    port_value_->setText(QStringLiteral("-"));
    active_transfers_value_->setText(QStringLiteral("-"));
    show_error(message);
}

void TransferWidget::set_send_inputs(const QString& file_path, const QString& host, const QString& port)
{
    send_file_edit_->setText(file_path);
    send_host_edit_->setText(host);
    send_port_edit_->setText(port);
}

void TransferWidget::request_send_for_testing()
{
    request_send();
}

void TransferWidget::set_sending(bool sending)
{
    sending_ = sending;
    send_status_value_->setText(sending ? QStringLiteral("Sending...") : QStringLiteral("Ready"));
    apply_controls();
}

void TransferWidget::show_send_success()
{
    sending_ = false;
    send_status_value_->setText(QStringLiteral("Sent"));
    clear_error();
    apply_controls();
}

void TransferWidget::show_send_failure(const QString& message)
{
    sending_ = false;
    send_status_value_->setText(QStringLiteral("Error: %1").arg(message));
    show_error(message);
    apply_controls();
}

void TransferWidget::set_transfer_status(const protocol::GetTransferStatusResponsePayload& status)
{
    set_display_state(status.enabled ? DisplayState::Enabled : DisplayState::Disabled);
    status_value_->setText(status.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
    port_value_->setText(status.enabled ? format_port(status.port) : QStringLiteral("-"));
    receive_directory_value_->setText(QString::fromUtf8(status.receive_directory));
    active_transfers_value_->setText(QString::number(status.active_transfers));
    clear_error();
}

void TransferWidget::set_pending(bool pending)
{
    pending_ = pending;
    apply_controls();
}

void TransferWidget::show_error(const QString& message)
{
    error_label_->setText(message);
}

void TransferWidget::clear_error()
{
    error_label_->clear();
}

QString TransferWidget::status_text() const
{
    return status_value_->text();
}

QString TransferWidget::port_text() const
{
    return port_value_->text();
}

QString TransferWidget::receive_directory_text() const
{
    return receive_directory_value_->text();
}

QString TransferWidget::active_transfers_text() const
{
    return active_transfers_value_->text();
}

QString TransferWidget::error_text() const
{
    return error_label_->text();
}

bool TransferWidget::enable_button_enabled() const
{
    return enable_button_->isEnabled();
}

bool TransferWidget::disable_button_enabled() const
{
    return disable_button_->isEnabled();
}

bool TransferWidget::choose_folder_button_enabled() const
{
    return choose_folder_button_->isEnabled();
}

bool TransferWidget::send_button_enabled() const
{
    return send_file_button_->isEnabled();
}

QString TransferWidget::send_status_text() const
{
    return send_status_value_->text();
}

void TransferWidget::choose_receive_directory()
{
    if (state_ != DisplayState::Disabled || pending_) {
        return;
    }
    const auto selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select receive directory"), receive_directory_value_->text());
    if (!selected.isEmpty() && receive_directory_selected_callback_) {
        receive_directory_selected_callback_(selected);
    }
}

void TransferWidget::choose_send_file()
{
    if (sending_ || state_ == DisplayState::Offline || state_ == DisplayState::Unavailable) {
        return;
    }
    const auto selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select file to send"), send_file_edit_->text());
    if (!selected.isEmpty()) {
        send_file_edit_->setText(selected);
    }
}

void TransferWidget::request_send()
{
    if (sending_ || state_ == DisplayState::Offline || state_ == DisplayState::Unavailable) {
        return;
    }
    const auto file_path = send_file_edit_->text().trimmed();
    const auto host = send_host_edit_->text().trimmed();
    bool port_ok = false;
    const auto port_value = send_port_edit_->text().trimmed().toUInt(&port_ok);
    if (file_path.isEmpty()) {
        show_send_failure(QStringLiteral("Choose a file to send."));
        return;
    }
    const QFileInfo file_info(file_path);
    if (!file_info.exists() || !file_info.isFile() || !file_info.isReadable()) {
        show_send_failure(QStringLiteral("The selected path is not a readable regular file."));
        return;
    }
    if (host.isEmpty()) {
        show_send_failure(QStringLiteral("Enter a target host."));
        return;
    }
    if (!port_ok || port_value == 0 || port_value > 65535) {
        show_send_failure(QStringLiteral("Port must be in range 1..65535."));
        return;
    }
    clear_error();
    set_sending(true);
    if (send_file_requested_callback_) {
        send_file_requested_callback_(file_path, host, static_cast<std::uint16_t>(port_value));
    } else {
        show_send_failure(QStringLiteral("Send service is unavailable."));
    }
}

void TransferWidget::apply_controls()
{
    const auto ready = !pending_;
    enable_button_->setEnabled(ready && state_ == DisplayState::Disabled);
    disable_button_->setEnabled(ready && state_ == DisplayState::Enabled);
    choose_folder_button_->setEnabled(ready && state_ == DisplayState::Disabled);
    const auto send_available = state_ == DisplayState::Disabled || state_ == DisplayState::Enabled;
    browse_send_file_button_->setEnabled(send_available && !sending_);
    send_file_button_->setEnabled(send_available && !sending_);
    send_file_edit_->setEnabled(send_available && !sending_);
    send_host_edit_->setEnabled(send_available && !sending_);
    send_port_edit_->setEnabled(send_available && !sending_);
}

void TransferWidget::set_display_state(DisplayState state)
{
    state_ = state;
    pending_ = false;
    if (send_status_value_->text().isEmpty()) {
        send_status_value_->setText(QStringLiteral("Ready"));
    }
    apply_controls();
}

} // namespace coredesk::ui
