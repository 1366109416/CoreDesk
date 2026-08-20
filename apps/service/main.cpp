#include "LocalIpcServer.h"
#include "coredesk/service/ServiceController.h"

#include <QCoreApplication>

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

    return QCoreApplication::exec();
}
