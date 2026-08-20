#include "MainWindow.h"
#include "SearchWidget.h"
#include "coredesk/protocol/JsonPayload.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#include <functional>
#include <vector>

namespace {

using coredesk::protocol::Frame;
using coredesk::protocol::MessageType;
using coredesk::protocol::ScanCompletedPayload;
using coredesk::protocol::ScanProgressPayload;
using coredesk::protocol::SearchResponsePayload;
using coredesk::protocol::SearchResultPayload;
using coredesk::ui::MainWindow;
using coredesk::ui::SearchWidget;

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
