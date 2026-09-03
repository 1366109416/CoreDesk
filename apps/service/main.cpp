#include "LocalIpcServer.h"
#include "coredesk/common/Logger.h"
#include "coredesk/service/ServiceController.h"

#ifdef COREDESK_BUILD_NETWORK
#include "transfer/TransferManager.h"
#endif

#include <QCoreApplication>
#include <QStandardPaths>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path path_from_qstring(const QString& text)
{
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    return std::filesystem::path(text.toStdString());
#endif
}

std::filesystem::path service_log_path()
{
    const auto override_path = qEnvironmentVariable("COREDESK_LOG_FILE");
    if (!override_path.isEmpty()) {
        return path_from_qstring(override_path);
    }
    return path_from_qstring(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) /
        "logs" / "coredesk_service.log";
}

} // namespace

#ifdef COREDESK_BUILD_NETWORK
namespace {

std::string path_to_utf8_string(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path path_from_utf8_string(const std::string& text)
{
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const unsigned char ch : text) {
        utf8.push_back(static_cast<char8_t>(ch));
    }
    return std::filesystem::path(utf8);
}

} // namespace
#endif

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    coredesk::Logger logger;
    const auto logger_opened = logger.open(service_log_path());
    if (!logger_opened.ok()) {
        std::cerr << "coredesk_service logger unavailable: " << coredesk::to_string(logger_opened.error().code)
                  << " " << logger_opened.error().message << '\n';
    }
    logger.log(coredesk::LogLevel::Info, "service", "Service starting");

    coredesk::service::ServiceController controller;
    controller.set_logger(&logger);

#ifdef COREDESK_BUILD_NETWORK
    coredesk::service::TransferManager transfer_manager;
    transfer_manager.set_logger(&logger);
#endif

    coredesk::qt_ipc::LocalIpcServer server(controller);
    server.set_logger(&logger);

#ifdef COREDESK_BUILD_NETWORK
    server.set_transfer_management_handlers(coredesk::qt_ipc::TransferManagementHandlers{
        [&transfer_manager]() -> coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload> {
            auto started = transfer_manager.start();
            if (!started.ok()) {
                return coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload>::failure(started.error());
            }
            return coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload>::success(
                {true, transfer_manager.listening_port()});
        },
        [&transfer_manager]() -> coredesk::Result<coredesk::protocol::DisableLanTransferResponsePayload> {
            transfer_manager.stop();
            return coredesk::Result<coredesk::protocol::DisableLanTransferResponsePayload>::success({true});
        },
        [&transfer_manager](
            const coredesk::protocol::SetReceiveDirectoryRequestPayload& payload)
            -> coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload> {
            auto updated = transfer_manager.set_receive_directory(path_from_utf8_string(payload.path));
            if (!updated.ok()) {
                return coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload>::failure(updated.error());
            }
            return coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload>::success(
                {true, path_to_utf8_string(transfer_manager.receive_directory())});
        },
        [&transfer_manager]() -> coredesk::Result<coredesk::protocol::GetTransferStatusResponsePayload> {
            const auto status = transfer_manager.status();
            return coredesk::Result<coredesk::protocol::GetTransferStatusResponsePayload>::success(
                {status.enabled,
                 status.port,
                 path_to_utf8_string(status.receive_directory),
                 status.active_transfers});
        },
        [&transfer_manager](const coredesk::protocol::SendFileRequestPayload& payload,
                            coredesk::qt_ipc::TransferManagementHandlers::SendFileCompletion completion)
            -> coredesk::Result<void> {
            return transfer_manager.send_file(path_from_utf8_string(payload.file_path),
                                              payload.host,
                                              payload.port,
                                              std::move(completion));
        }});
#endif

    auto listen_result = server.listen();
    if (!listen_result.ok()) {
        logger.log(coredesk::LogLevel::Error,
                   "service",
                   "Service start failed error_code=" + std::string(coredesk::to_string(listen_result.error().code)) +
                       " message=" + listen_result.error().message);
        std::cerr << "coredesk_service failed to listen: "
                  << coredesk::to_string(listen_result.error().code) << " "
                  << listen_result.error().message << '\n';
        return 2;
    }

    logger.log(coredesk::LogLevel::Info, "service", "Service started");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&logger]() {
        logger.log(coredesk::LogLevel::Info, "service", "Service stopping");
    });
    const auto exit_code = QCoreApplication::exec();
    server.close();
#ifdef COREDESK_BUILD_NETWORK
    transfer_manager.stop();
#endif
    controller.shutdown();
    logger.log(coredesk::LogLevel::Info, "service", "Service stopped");
    return exit_code;
}
