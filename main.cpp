#include "server.hpp"
#include <arpa/inet.h>
#include <csignal>

cServer g_server(SERVER_ADDR, SERVER_PORT);

int
main()
{
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    g_server.Start();
    g_server.Run();

    return 0;
}
