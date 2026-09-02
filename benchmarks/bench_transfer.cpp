#include "TcpTransferClient.h"
#include "TcpTransferServer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace coredesk::qt_network {

class TcpTransferClientTestPeer {
public:
    static qint64 socket_pending_bytes(const TcpTransferClient& client)
    {
        return client.socket_->bytesToWrite();
    }

    static qint64 remainder_bytes(const TcpTransferClient& client)
    {
        return client.write_remainder_.size();
    }

    static std::uint64_t send_offset(const TcpTransferClient& client)
    {
        return client.send_offset_;
    }

    static constexpr qint64 high_water_mark()
    {
        return TcpTransferClient::kPendingWriteHighWaterMark;
    }

    static constexpr qint64 chunk_size()
    {
        return TcpTransferClient::kFileChunkSize;
    }
};

} // namespace coredesk::qt_network

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
using TimePoint = Clock::time_point;
using coredesk::qt_network::TcpTransferClient;
using coredesk::qt_network::TcpTransferClientTestPeer;
using coredesk::qt_network::TcpTransferServer;

QByteArray sha256_file(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) {
            return {};
        }
        hash.addData(bytes);
    }
    return hash.result().toHex();
}

constexpr auto kTimerInterval = std::chrono::milliseconds(5);

struct Distribution {
    double average_us{};
    double median_us{};
    double p95_us{};
    double p99_us{};
    double max_us{};
};

Nanoseconds nearest_rank(std::vector<Nanoseconds> values, double percentile)
{
    if (values.empty()) {
        return Nanoseconds::zero();
    }
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
}

Distribution summarize(const std::vector<Nanoseconds>& samples)
{
    if (samples.empty()) {
        return {};
    }
    const auto total = std::accumulate(samples.begin(), samples.end(), Nanoseconds::zero());
    const auto to_us = [](Nanoseconds value) {
        return std::chrono::duration<double, std::micro>(value).count();
    };
    return Distribution{to_us(total) / static_cast<double>(samples.size()),
                        to_us(nearest_rank(samples, 0.50)),
                        to_us(nearest_rank(samples, 0.95)),
                        to_us(nearest_rank(samples, 0.99)),
                        to_us(*std::max_element(samples.begin(), samples.end()))};
}

class ResponsivenessSampler {
public:
    ResponsivenessSampler()
    {
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(static_cast<int>(kTimerInterval.count()));
        QObject::connect(&timer_, &QTimer::timeout, [&]() {
            record_callback();
        });
    }

    void start()
    {
        const auto now = Clock::now();
        previous_callback_ = now;
        expected_deadline_ = now + kTimerInterval;
        timer_.start();
    }

    void stop() { timer_.stop(); }
    const std::vector<Nanoseconds>& intervals() const noexcept { return intervals_; }
    const std::vector<Nanoseconds>& lateness() const noexcept { return lateness_; }

private:
    void record_callback()
    {
        const auto now = Clock::now();
        intervals_.push_back(std::chrono::duration_cast<Nanoseconds>(now - previous_callback_));
        previous_callback_ = now;
        lateness_.push_back(now > expected_deadline_
                                ? std::chrono::duration_cast<Nanoseconds>(now - expected_deadline_)
                                : Nanoseconds::zero());

        // Keep the original cadence grid, skip elapsed deadlines, and never
        // generate catch-up callbacks or reset the cadence to actual + 5 ms.
        if (now >= expected_deadline_) {
            const auto missed = (now - expected_deadline_) / kTimerInterval + 1;
            expected_deadline_ += kTimerInterval * missed;
        } else {
            expected_deadline_ += kTimerInterval;
        }
    }

    QTimer timer_;
    TimePoint previous_callback_{};
    TimePoint expected_deadline_{};
    std::vector<Nanoseconds> intervals_;
    std::vector<Nanoseconds> lateness_;
};

void print_responsiveness(const ResponsivenessSampler& sampler)
{
    const auto interval = summarize(sampler.intervals());
    const auto lateness = summarize(sampler.lateness());
    std::cout << "timer_type: PreciseTimer\n"
              << "target_interval_us: 5000\n"
              << "missed_cadence_policy: preserve_grid_skip_elapsed_deadlines\n"
              << "timer_samples: " << sampler.intervals().size() << '\n'
              << "callback_interval_average_us: " << interval.average_us << '\n'
              << "callback_interval_median_us: " << interval.median_us << '\n'
              << "callback_interval_p95_us: " << interval.p95_us << '\n'
              << "callback_interval_p99_us: " << interval.p99_us << '\n'
              << "callback_interval_max_us: " << interval.max_us << '\n'
              << "deadline_lateness_average_us: " << lateness.average_us << '\n'
              << "deadline_lateness_median_us: " << lateness.median_us << '\n'
              << "deadline_lateness_p95_us: " << lateness.p95_us << '\n'
              << "deadline_lateness_p99_us: " << lateness.p99_us << '\n'
              << "deadline_lateness_max_us: " << lateness.max_us << '\n';
}

bool parse_positive_seconds(std::string_view text, int& value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value > 0;
}

int run_baseline(QCoreApplication& app, int seconds)
{
    ResponsivenessSampler sampler;
    sampler.start();
    const auto started = Clock::now();
    QTimer::singleShot(std::chrono::seconds(seconds), Qt::PreciseTimer, &app, [&]() {
        sampler.stop();
        app.quit();
    });
    app.exec();
    std::cout << std::fixed << std::setprecision(3)
              << "mode: baseline\n"
              << "baseline_target_seconds: " << seconds << '\n'
              << "baseline_actual_seconds: " << std::chrono::duration<double>(Clock::now() - started).count() << '\n';
    print_responsiveness(sampler);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && std::string_view(argv[1]) == "--baseline") {
        int seconds = 0;
        if (!parse_positive_seconds(argv[2], seconds)) {
            std::cerr << "baseline seconds must be a positive integer\n";
            return 1;
        }
        return run_baseline(app, seconds);
    }
    if (argc != 3) {
        std::cerr << "Usage: coredesk_bench_transfer <source_file> <receive_directory>\n"
                  << "       coredesk_bench_transfer --baseline <seconds>\n";
        return 1;
    }

    const QString source_path = QString::fromLocal8Bit(argv[1]);
    const QFileInfo source_info(source_path);
    if (!source_info.isFile()) {
        std::cerr << "source file does not exist\n";
        return 1;
    }
    const auto receive_path = std::filesystem::path(QString::fromLocal8Bit(argv[2]).toStdWString());
    std::error_code directory_error;
    std::filesystem::create_directories(receive_path, directory_error);
    if (directory_error) {
        std::cerr << "receive directory creation failed: " << directory_error.message() << '\n';
        return 1;
    }

    TcpTransferServer server(QStringLiteral("BenchmarkServer"));
    server.set_receive_directory(receive_path);
    const auto listening = server.listen(0, QHostAddress::LocalHost);
    if (!listening.ok()) {
        std::cerr << "listen failed: " << listening.error().message << '\n';
        return 2;
    }

    TcpTransferClient client(QStringLiteral("BenchmarkClient"));
    bool completed = false;
    bool successful = false;
    std::string terminal_message;
    TimePoint send_started{};
    TimePoint accepted_at{};
    TimePoint completed_at{};
    client.set_file_accept_callback([&](coredesk::RequestId, const coredesk::protocol::FileAcceptPayload&) {
        if (accepted_at == TimePoint{}) {
            accepted_at = Clock::now();
        }
    });
    client.set_file_result_callback([&](coredesk::RequestId, const coredesk::protocol::FileResultPayload& result) {
        completed_at = Clock::now();
        completed = true;
        successful = result.ok;
        terminal_message = result.message;
        app.quit();
    });
    client.set_error_callback([&](const coredesk::Error& error) {
        completed_at = Clock::now();
        completed = true;
        successful = false;
        terminal_message = std::string(coredesk::to_string(error.code)) + ": " + error.message;
        app.quit();
    });

    qint64 max_qt_pending = 0;
    qint64 max_remainder = 0;
    qint64 max_combined = 0;
    std::uint64_t offset_at_max_pending = 0;
    ResponsivenessSampler sampler;
    QTimer metric_sampler;
    metric_sampler.setTimerType(Qt::PreciseTimer);
    metric_sampler.setInterval(static_cast<int>(kTimerInterval.count()));
    QObject::connect(&metric_sampler, &QTimer::timeout, &app, [&]() {
        const auto qt_pending = TcpTransferClientTestPeer::socket_pending_bytes(client);
        const auto remainder = TcpTransferClientTestPeer::remainder_bytes(client);
        const auto combined = qt_pending + remainder;
        if (combined > max_combined) {
            max_combined = combined;
            offset_at_max_pending = TcpTransferClientTestPeer::send_offset(client);
        }
        max_qt_pending = std::max(max_qt_pending, qt_pending);
        max_remainder = std::max(max_remainder, remainder);
    });
    sampler.start();
    metric_sampler.start();

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&]() {
        completed_at = Clock::now();
        terminal_message = "benchmark timeout";
        app.quit();
    });
    timeout.start(15 * 60 * 1000);

    client.connect_to_host(QStringLiteral("127.0.0.1"), server.server_port());
    QTimer start_poll;
    start_poll.setTimerType(Qt::PreciseTimer);
    start_poll.setInterval(1);
    QObject::connect(&start_poll, &QTimer::timeout, &app, [&]() {
        if (!client.handshake_complete()) {
            return;
        }
        start_poll.stop();
        send_started = Clock::now();
        const auto sent = client.send_file(source_path);
        if (!sent.ok()) {
            completed_at = Clock::now();
            completed = true;
            terminal_message = sent.error().message;
            app.quit();
        }
    });
    start_poll.start();
    app.exec();
    sampler.stop();
    metric_sampler.stop();

    const auto elapsed = completed_at > send_started ? completed_at - send_started : Clock::duration::zero();
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
    const auto prepare_elapsed_ms = accepted_at > send_started
        ? std::chrono::duration<double, std::milli>(accepted_at - send_started).count()
        : 0.0;
    const auto accepted_to_result_ms = completed_at > accepted_at
        ? std::chrono::duration<double, std::milli>(completed_at - accepted_at).count()
        : 0.0;
    const auto file_size = static_cast<std::uint64_t>(source_info.size());
    const auto mib = static_cast<double>(file_size) / (1024.0 * 1024.0);
    const auto throughput = elapsed_ms > 0.0 ? mib / (elapsed_ms / 1000.0) : 0.0;
    const auto received_file = QString::fromStdWString((receive_path / source_info.fileName().toStdWString()).wstring());
    const auto source_hash = sha256_file(source_path);
    const auto received_hash = sha256_file(received_file);
    const bool hash_match = !source_hash.isEmpty() && source_hash == received_hash;

    std::cout << std::fixed << std::setprecision(3)
              << "mode: transfer\n"
              << "file_size_bytes: " << file_size << '\n'
              << "file_size_mib: " << mib << '\n'
              << "total_send_file_elapsed_ms: " << elapsed_ms << '\n'
              << "prepare_to_accept_elapsed_ms: " << prepare_elapsed_ms << '\n'
              << "accept_to_result_elapsed_ms: " << accepted_to_result_ms << '\n'
              << "throughput_mib_s: " << throughput << '\n'
              << "chunk_size_bytes: " << TcpTransferClientTestPeer::chunk_size() << '\n'
              << "high_water_mark_bytes: " << TcpTransferClientTestPeer::high_water_mark() << '\n'
              << "max_qt_pending_bytes: " << max_qt_pending << '\n'
              << "max_application_remainder_bytes: " << max_remainder << '\n'
              << "max_combined_pending_bytes: " << max_combined << '\n'
              << "send_offset_at_max_pending: " << offset_at_max_pending << '\n';
    print_responsiveness(sampler);
    std::cout << "source_sha256: " << source_hash.constData() << '\n'
              << "received_sha256: " << received_hash.constData() << '\n'
              << "sha256_match: " << (hash_match ? "true" : "false") << '\n'
              << "transfer_result: " << (successful ? "success" : "failure") << '\n'
              << "terminal_message: " << terminal_message << '\n';
    return completed && successful && hash_match ? 0 : 3;
}
