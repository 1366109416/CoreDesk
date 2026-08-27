#include "TransferWidget.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
{
    status_value_->setObjectName(QStringLiteral("transferStatusValue"));
    port_value_->setObjectName(QStringLiteral("transferPortValue"));
    receive_directory_value_->setObjectName(QStringLiteral("transferReceiveDirectoryValue"));
    active_transfers_value_->setObjectName(QStringLiteral("transferActiveTransfersValue"));
    error_label_->setObjectName(QStringLiteral("transferErrorLabel"));
    enable_button_->setObjectName(QStringLiteral("enableLanTransferButton"));
    disable_button_->setObjectName(QStringLiteral("disableLanTransferButton"));
    choose_folder_button_->setObjectName(QStringLiteral("chooseReceiveDirectoryButton"));

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

void TransferWidget::set_offline_state()
{
    set_display_state(DisplayState::Offline);
    status_value_->setText(QStringLiteral("Offline"));
    port_value_->setText(QStringLiteral("-"));
    active_transfers_value_->setText(QStringLiteral("-"));
    clear_error();
}

void TransferWidget::set_unavailable_state(const QString& message)
{
    set_display_state(DisplayState::Unavailable);
    status_value_->setText(QStringLiteral("Unavailable"));
    port_value_->setText(QStringLiteral("-"));
    active_transfers_value_->setText(QStringLiteral("-"));
    show_error(message);
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

void TransferWidget::apply_controls()
{
    const auto ready = !pending_;
    enable_button_->setEnabled(ready && state_ == DisplayState::Disabled);
    disable_button_->setEnabled(ready && state_ == DisplayState::Enabled);
    choose_folder_button_->setEnabled(ready && state_ == DisplayState::Disabled);
}

void TransferWidget::set_display_state(DisplayState state)
{
    state_ = state;
    pending_ = false;
    apply_controls();
}

} // namespace coredesk::ui
