#include "server.hpp"
#include <arpa/inet.h>
#include <csignal>

int
main()
{
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    cServer Server(SERVER_ADDR, SERVER_PORT);

    Server.Start();
    Server.Run();

    return 0;
}
