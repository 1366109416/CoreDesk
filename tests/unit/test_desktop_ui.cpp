#include "MainWindow.h"
#include "SearchWidget.h"
#include "TransferWidget.h"
#include "coredesk/protocol/JsonPayload.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>

#include <functional>
#include <vector>

namespace {

using coredesk::protocol::Frame;
using coredesk::protocol::MessageType;
using coredesk::protocol::ScanCompletedPayload;
using coredesk::protocol::ScanProgressPayload;
using coredesk::protocol::SearchResponsePayload;
using coredesk::protocol::SearchResultPayload;
using coredesk::protocol::GetTransferStatusResponsePayload;
using coredesk::ui::MainWindow;
using coredesk::ui::SearchWidget;
using coredesk::ui::TransferWidget;

QApplication& app()
{
    static int argc = 1;
    static char app_name[] = "coredesk_desktop_ui_tests";
    static char* argv[] = {app_name, nullptr};
    static QApplication application(argc, argv);
    return application;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 1000)
{
    if (predicate()) {
        return true;
    }
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poller;
    QObject::connect(&poller, &QTimer::timeout, &loop, [&]() {
        if (predicate()) {
            loop.quit();
        }
    });
    poller.start(5);
    deadline.start(timeout_ms);
    loop.exec();
    return predicate();
}

Frame search_frame(coredesk::RequestId request_id, std::string name)
{
    SearchResponsePayload payload;
    payload.generation = request_id;
    payload.elapsed_us = 7;
    payload.results.push_back(SearchResultPayload{std::move(name), "C:/x", "x", 10, 1, "file", 60});
    auto encoded = coredesk::protocol::encode_search_response_payload(payload);
    EXPECT_TRUE(encoded.ok());
    return Frame{MessageType::SearchResponse, 0, request_id, std::move(encoded).value()};
}

Frame transfer_status_frame(coredesk::RequestId request_id, GetTransferStatusResponsePayload payload)
{
    auto encoded = coredesk::protocol::encode_get_transfer_status_response_payload(payload);
    EXPECT_TRUE(encoded.ok());
    return Frame{MessageType::GetTransferStatusResponse, 0, request_id, std::move(encoded).value()};
}

Frame transfer_error_frame(MessageType type, coredesk::RequestId request_id, coredesk::ErrorCode code, std::string message)
{
    auto encoded = coredesk::protocol::encode_error_response_payload(
        coredesk::protocol::ErrorResponsePayload{false, code, std::move(message)});
    EXPECT_TRUE(encoded.ok());
    return Frame{type, 0, request_id, std::move(encoded).value()};
}

Frame send_accepted_frame(coredesk::RequestId request_id)
{
    auto encoded = coredesk::protocol::encode_send_file_accepted_payload({true});
    EXPECT_TRUE(encoded.ok());
    return Frame{MessageType::SendFileAccepted, 0, request_id, std::move(encoded).value()};
}

Frame send_result_frame(coredesk::RequestId request_id, bool success, coredesk::ErrorCode code, std::string message)
{
    auto encoded = coredesk::protocol::encode_send_file_result_payload({success, code, std::move(message)});
    EXPECT_TRUE(encoded.ok());
    return Frame{MessageType::SendFileResult, 0, request_id, std::move(encoded).value()};
}

} // namespace

TEST(SearchWidgetTest, DebouncesSearchRequests)
{
    app();
    SearchWidget widget;
    std::vector<QString> queries;
    widget.set_search_requested_callback([&](const QString& query) {
        queries.push_back(query);
    });

    widget.set_query_text(QStringLiteral("a"));
    widget.set_query_text(QStringLiteral("ab"));
    widget.set_query_text(QStringLiteral("abc"));

    QCoreApplication::processEvents();
    EXPECT_TRUE(queries.empty());
    ASSERT_TRUE(wait_until([&] {
        return !queries.empty();
    }, 1000));
    ASSERT_EQ(queries.size(), 1U);
    EXPECT_EQ(queries[0], QStringLiteral("abc"));
}

TEST(SearchWidgetTest, EmptyDebouncedQueryClearsResultsAndDoesNotRequestSearch)
{
    app();
    SearchWidget widget;
    SearchResponsePayload response;
    response.results.push_back(SearchResultPayload{"old.txt", "C:/old.txt", "old.txt", 1, 1, "file", 60});
    widget.render_results(response);
    ASSERT_EQ(widget.result_row_count(), 1);

    bool requested = false;
    widget.set_search_requested_callback([&](const QString&) {
        requested = true;
    });
    widget.set_query_text(QStringLiteral("   "));
    ASSERT_TRUE(wait_until([&] {
        return widget.result_row_count() == 0;
    }, 1000));
    EXPECT_FALSE(requested);
}

TEST(SearchWidgetTest, RendersAtMostOneHundredRows)
{
    app();
    SearchWidget widget;
    SearchResponsePayload response;
    for (int i = 0; i < 125; ++i) {
        response.results.push_back(SearchResultPayload{"file_" + std::to_string(i), "C:/x", "x", 1, 1, "file", 60});
    }
    widget.render_results(response);
    EXPECT_EQ(widget.result_row_count(), 100);
}

TEST(TransferWidgetTest, InitialOfflineStateDisablesActions)
{
    app();
    TransferWidget widget;
    EXPECT_EQ(widget.status_text(), QStringLiteral("Offline"));
    EXPECT_FALSE(widget.enable_button_enabled());
    EXPECT_FALSE(widget.disable_button_enabled());
    EXPECT_FALSE(widget.choose_folder_button_enabled());
}

TEST(TransferWidgetTest, DisabledStatusEnablesEnableAndChooseFolder)
{
    app();
    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{false, 0, "C:/receive", 0});

    EXPECT_EQ(widget.status_text(), QStringLiteral("Disabled"));
    EXPECT_EQ(widget.port_text(), QStringLiteral("-"));
    EXPECT_EQ(widget.receive_directory_text(), QStringLiteral("C:/receive"));
    EXPECT_EQ(widget.active_transfers_text(), QStringLiteral("0"));
    EXPECT_TRUE(widget.enable_button_enabled());
    EXPECT_FALSE(widget.disable_button_enabled());
    EXPECT_TRUE(widget.choose_folder_button_enabled());
}

TEST(TransferWidgetTest, EnabledStatusEnablesOnlyDisable)
{
    app();
    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{true, 45827, "C:/receive", 1});

    EXPECT_EQ(widget.status_text(), QStringLiteral("Enabled"));
    EXPECT_EQ(widget.port_text(), QStringLiteral("45827"));
    EXPECT_EQ(widget.active_transfers_text(), QStringLiteral("1"));
    EXPECT_FALSE(widget.enable_button_enabled());
    EXPECT_TRUE(widget.disable_button_enabled());
    EXPECT_FALSE(widget.choose_folder_button_enabled());
}

TEST(TransferWidgetTest, UnavailableStateDisablesActionsAndShowsError)
{
    app();
    TransferWidget widget;
    widget.set_unavailable_state(QStringLiteral("LAN transfer feature is unavailable in this build"));

    EXPECT_EQ(widget.status_text(), QStringLiteral("Unavailable"));
    EXPECT_FALSE(widget.enable_button_enabled());
    EXPECT_FALSE(widget.disable_button_enabled());
    EXPECT_FALSE(widget.choose_folder_button_enabled());
    EXPECT_TRUE(widget.error_text().contains(QStringLiteral("unavailable")));
}

TEST(TransferWidgetTest, PendingStateDisablesActionsTemporarily)
{
    app();
    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{false, 0, "C:/receive", 0});
    ASSERT_TRUE(widget.enable_button_enabled());
    ASSERT_TRUE(widget.choose_folder_button_enabled());

    widget.set_pending(true);
    EXPECT_FALSE(widget.enable_button_enabled());
    EXPECT_FALSE(widget.disable_button_enabled());
    EXPECT_FALSE(widget.choose_folder_button_enabled());
}

TEST(TransferWidgetTest, ErrorLabelDisplaysServiceError)
{
    app();
    TransferWidget widget;
    widget.show_error(QStringLiteral("permission denied"));
    EXPECT_EQ(widget.error_text(), QStringLiteral("permission denied"));
}

TEST(TransferWidgetTest, ValidSendInputsEmitTypedRequestAndDisableSend)
{
    app();
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto file_path = temp.filePath(QStringLiteral("send.bin"));
    QFile file(file_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("payload");
    file.close();

    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{false, 0, "C:/receive", 0});
    QString requested_file;
    QString requested_host;
    std::uint16_t requested_port = 0;
    widget.set_send_file_requested_callback(
        [&](const QString& path, const QString& host, std::uint16_t port) {
            requested_file = path;
            requested_host = host;
            requested_port = port;
        });
    widget.set_send_inputs(file_path, QStringLiteral("127.0.0.1"), QStringLiteral("45827"));
    widget.request_send_for_testing();

    EXPECT_EQ(requested_file, file_path);
    EXPECT_EQ(requested_host, QStringLiteral("127.0.0.1"));
    EXPECT_EQ(requested_port, 45827U);
    EXPECT_FALSE(widget.send_button_enabled());
    EXPECT_EQ(widget.send_status_text(), QStringLiteral("Sending..."));
}

TEST(TransferWidgetTest, InvalidSendInputsShowErrorWithoutRequest)
{
    app();
    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{false, 0, "C:/receive", 0});
    int requests = 0;
    widget.set_send_file_requested_callback([&](const QString&, const QString&, std::uint16_t) {
        ++requests;
    });
    widget.set_send_inputs(QStringLiteral("missing.file"), QString{}, QStringLiteral("70000"));
    widget.request_send_for_testing();

    EXPECT_EQ(requests, 0);
    EXPECT_TRUE(widget.send_status_text().startsWith(QStringLiteral("Error:")));
    EXPECT_TRUE(widget.send_button_enabled());
}

TEST(TransferWidgetTest, SendTerminalStatesRestoreButton)
{
    app();
    TransferWidget widget;
    widget.set_transfer_status(GetTransferStatusResponsePayload{true, 45827, "C:/receive", 0});
    widget.set_sending(true);
    EXPECT_FALSE(widget.send_button_enabled());
    widget.show_send_success();
    EXPECT_TRUE(widget.send_button_enabled());
    EXPECT_EQ(widget.send_status_text(), QStringLiteral("Sent"));
    widget.set_sending(true);
    widget.show_send_failure(QStringLiteral("connection refused"));
    EXPECT_TRUE(widget.send_button_enabled());
    EXPECT_TRUE(widget.send_status_text().contains(QStringLiteral("connection refused")));
}

TEST(MainWindowTest, StaleSearchResponseDoesNotOverwriteNewerQuery)
{
    app();
    MainWindow window(false);
    window.handle_connected();

    window.set_latest_search_request_id_for_testing(11);
    window.handle_frame(search_frame(11, "newer.txt"));
    ASSERT_EQ(window.search_widget()->result_row_count(), 1);
    EXPECT_EQ(window.search_widget()->result_name_at(0), QStringLiteral("newer.txt"));

    window.handle_frame(search_frame(10, "older.txt"));
    ASSERT_EQ(window.search_widget()->result_row_count(), 1);
    EXPECT_EQ(window.search_widget()->result_name_at(0), QStringLiteral("newer.txt"));
}

TEST(MainWindowTest, QueryChangeBeforeDebounceInvalidatesPreviousResponse)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.set_latest_search_request_id_for_testing(10);

    window.search_widget()->set_query_text(QStringLiteral("ab"));
    QCoreApplication::processEvents();
    window.handle_frame(search_frame(10, "old.txt"));

    EXPECT_EQ(window.search_widget()->result_row_count(), 0);
}

TEST(MainWindowTest, NonEmptyQueryChangeClearsVisibleStaleResults)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.set_latest_search_request_id_for_testing(10);
    window.handle_frame(search_frame(10, "old.txt"));
    ASSERT_EQ(window.search_widget()->result_row_count(), 1);

    window.search_widget()->set_query_text(QStringLiteral("new query"));
    QCoreApplication::processEvents();

    EXPECT_EQ(window.search_widget()->result_row_count(), 0);
}

TEST(MainWindowTest, ClearingQueryInvalidatesPreviousResponse)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.search_widget()->set_query_text(QStringLiteral("a"));
    QCoreApplication::processEvents();
    window.set_latest_search_request_id_for_testing(10);
    window.handle_frame(search_frame(10, "old.txt"));
    ASSERT_EQ(window.search_widget()->result_row_count(), 1);

    window.search_widget()->set_query_text(QString{});
    QCoreApplication::processEvents();
    ASSERT_EQ(window.search_widget()->result_row_count(), 0);

    window.handle_frame(search_frame(10, "old.txt"));
    EXPECT_EQ(window.search_widget()->result_row_count(), 0);
}

TEST(MainWindowTest, ScanProgressAndCompletionUpdateUiState)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.set_root_path(QStringLiteral("C:/temp"));
    window.set_active_scan_request_id_for_testing(1);

    window.handle_frame(Frame{MessageType::ScanAccepted, 0, 1, {}});
    EXPECT_TRUE(window.is_scanning());

    auto progress_bytes = coredesk::protocol::encode_scan_progress_payload(
        ScanProgressPayload{"1", 3, 2, 0, 0, 10});
    ASSERT_TRUE(progress_bytes.ok());
    window.handle_frame(Frame{MessageType::ScanProgress, 0, 1, std::move(progress_bytes).value()});
    EXPECT_TRUE(window.is_scanning());

    auto completed_bytes = coredesk::protocol::encode_scan_completed_payload(
        ScanCompletedPayload{"1", 7, 2, 20});
    ASSERT_TRUE(completed_bytes.ok());
    window.handle_frame(Frame{MessageType::ScanCompleted, 0, 1, std::move(completed_bytes).value()});
    EXPECT_FALSE(window.is_scanning());
    EXPECT_TRUE(window.status_text().contains(QStringLiteral("generation 7")));
}

TEST(MainWindowTest, SuccessfulScanClearsPreviousScanErrorMessage)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.set_active_scan_request_id_for_testing(1);

    auto error_bytes = coredesk::protocol::encode_error_response_payload(
        coredesk::protocol::ErrorResponsePayload{false, coredesk::ErrorCode::Cancelled, "scan cancelled"});
    ASSERT_TRUE(error_bytes.ok());
    window.handle_frame(Frame{MessageType::ScanFailed, 0, 1, std::move(error_bytes).value()});
    ASSERT_TRUE(window.status_text().contains(QStringLiteral("scan cancelled")));

    window.set_active_scan_request_id_for_testing(2);
    auto completed_bytes = coredesk::protocol::encode_scan_completed_payload(
        ScanCompletedPayload{"2", 8, 3, 20});
    ASSERT_TRUE(completed_bytes.ok());
    window.handle_frame(Frame{MessageType::ScanCompleted, 0, 2, std::move(completed_bytes).value()});

    EXPECT_FALSE(window.status_text().contains(QStringLiteral("scan cancelled")));
    EXPECT_TRUE(window.status_text().contains(QStringLiteral("generation 8")));
}

TEST(MainWindowTest, DisconnectInvalidatesPendingRequestsAndShowsOffline)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.handle_disconnected();
    EXPECT_EQ(window.connection_state(), MainWindow::ConnectionState::Offline);
    EXPECT_TRUE(window.status_text().contains(QStringLiteral("Offline")));
}

TEST(MainWindowTest, GetTransferStatusResponseUpdatesTransferWidget)
{
    app();
    MainWindow window(false);
    window.set_pending_transfer_request_id_for_testing(MessageType::GetTransferStatusResponse, 33);

    window.handle_frame(transfer_status_frame(33, GetTransferStatusResponsePayload{true, 45827, "C:/receive", 1}));

    EXPECT_EQ(window.transfer_widget()->status_text(), QStringLiteral("Enabled"));
    EXPECT_EQ(window.transfer_widget()->port_text(), QStringLiteral("45827"));
    EXPECT_EQ(window.transfer_widget()->receive_directory_text(), QStringLiteral("C:/receive"));
    EXPECT_EQ(window.transfer_widget()->active_transfers_text(), QStringLiteral("1"));
}

TEST(MainWindowTest, TransferStatusRequestIdMismatchDoesNotUpdateWidget)
{
    app();
    MainWindow window(false);
    window.set_pending_transfer_request_id_for_testing(MessageType::GetTransferStatusResponse, 33);

    window.handle_frame(transfer_status_frame(32, GetTransferStatusResponsePayload{true, 45827, "C:/receive", 1}));

    EXPECT_EQ(window.transfer_widget()->status_text(), QStringLiteral("Offline"));
}

TEST(MainWindowTest, TransferUnavailableErrorShowsUnavailableState)
{
    app();
    MainWindow window(false);
    window.set_pending_transfer_request_id_for_testing(MessageType::GetTransferStatusResponse, 44);

    window.handle_frame(transfer_error_frame(MessageType::GetTransferStatusResponse,
                                            44,
                                            coredesk::ErrorCode::ConnectionFailed,
                                            "LAN transfer feature is unavailable in this build"));

    EXPECT_EQ(window.transfer_widget()->status_text(), QStringLiteral("Unavailable"));
    EXPECT_TRUE(window.transfer_widget()->error_text().contains(QStringLiteral("unavailable")));
}

TEST(MainWindowTest, DisconnectInvalidatesPendingTransferStatusResponse)
{
    app();
    MainWindow window(false);
    window.set_pending_transfer_request_id_for_testing(MessageType::GetTransferStatusResponse, 55);

    window.handle_disconnected();
    window.handle_frame(transfer_status_frame(55, GetTransferStatusResponsePayload{true, 45827, "C:/receive", 1}));

    EXPECT_EQ(window.transfer_widget()->status_text(), QStringLiteral("Offline"));
}

TEST(MainWindowTest, SendFileResponsesRequireMatchingRequestId)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.set_pending_transfer_request_id_for_testing(MessageType::SendFileAccepted, 70);

    window.handle_frame(send_accepted_frame(69));
    EXPECT_NE(window.transfer_widget()->send_status_text(), QStringLiteral("Sending..."));
    window.handle_frame(send_accepted_frame(70));
    EXPECT_EQ(window.transfer_widget()->send_status_text(), QStringLiteral("Sending..."));
    window.handle_frame(send_result_frame(69, true, coredesk::ErrorCode::Ok, {}));
    EXPECT_EQ(window.transfer_widget()->send_status_text(), QStringLiteral("Sending..."));
    window.handle_frame(send_result_frame(70, true, coredesk::ErrorCode::Ok, {}));
    EXPECT_EQ(window.transfer_widget()->send_status_text(), QStringLiteral("Sent"));
}

TEST(MainWindowTest, SendFileFailureRestoresUiOnce)
{
    app();
    MainWindow window(false);
    window.handle_connected();
    window.transfer_widget()->set_transfer_status(GetTransferStatusResponsePayload{false, 0, "C:/receive", 0});
    window.set_pending_transfer_request_id_for_testing(MessageType::SendFileResult, 80);
    window.handle_frame(send_result_frame(80, false, coredesk::ErrorCode::TargetExists, "target exists"));
    EXPECT_TRUE(window.transfer_widget()->send_button_enabled());
    EXPECT_TRUE(window.transfer_widget()->send_status_text().contains(QStringLiteral("target exists")));

    window.handle_frame(send_result_frame(80, true, coredesk::ErrorCode::Ok, {}));
    EXPECT_TRUE(window.transfer_widget()->send_status_text().contains(QStringLiteral("target exists")));
}

TEST(MainWindowTest, DisconnectDuringSendShowsTerminalError)
{
    app();
    MainWindow window(false);
    window.set_pending_transfer_request_id_for_testing(MessageType::SendFileAccepted, 901);
    window.transfer_widget()->set_sending(true);

    window.handle_disconnected();

    EXPECT_EQ(window.connection_state(), MainWindow::ConnectionState::Offline);
    EXPECT_FALSE(window.transfer_widget()->send_button_enabled());
    EXPECT_TRUE(window.transfer_widget()->send_status_text().contains(QStringLiteral("disconnected"),
                                                                      Qt::CaseInsensitive));
}
