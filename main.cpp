#include "server.hpp"
#include <arpa/inet.h>
#include <csignal>

cServer Server(SERVER_ADDR, SERVER_PORT);

int
main()
{
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    Server.Start();
    Server.Run();

    return 0;
}
