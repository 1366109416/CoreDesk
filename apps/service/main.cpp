#include "LocalIpcServer.h"
#include "coredesk/service/ServiceController.h"

#ifdef COREDESK_BUILD_NETWORK
#include "TcpTransferServer.h"
#endif

#include <QCoreApplication>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    coredesk::service::ServiceController controller;
    coredesk::qt_ipc::LocalIpcServer server(controller);
    auto listen_result = server.listen();
    if (!listen_result.ok()) {
        std::cerr << "coredesk_service failed to listen: "
                  << coredesk::to_string(listen_result.error().code) << " "
                  << listen_result.error().message << '\n';
        return 2;
    }

#ifdef COREDESK_BUILD_NETWORK
    const auto receive_directory = std::filesystem::temp_directory_path() / "CoreDeskReceived";
    std::error_code receive_ec;
    std::filesystem::create_directories(receive_directory, receive_ec);
    if (receive_ec) {
        std::cerr << "coredesk_service failed to create receive directory: "
                  << receive_ec.message() << '\n';
        return 3;
    }

    coredesk::qt_network::TcpTransferServer tcp_server;
    tcp_server.set_receive_directory(receive_directory);
    auto tcp_listen_result = tcp_server.listen();
    if (!tcp_listen_result.ok()) {
        std::cerr << "coredesk_service failed to listen for TCP transfer: "
                  << coredesk::to_string(tcp_listen_result.error().code) << " "
                  << tcp_listen_result.error().message << '\n';
        return 4;
    }
#endif

    return QCoreApplication::exec();
}
