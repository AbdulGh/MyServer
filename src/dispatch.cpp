#include "dispatch.h"
#include "logger.h"
#include <string>
#include <unistd.h>

namespace MyServer {

void Server::Dispatch::work() {
  for(;;) {
    parent.incomingClientQueue.wait();
    if (int client = parent.incomingClientQueue.take() >= 0) {
      dispatch(client);
    }
  }
}

void Server::Dispatch::dispatch(int client) {
  Logger::log<Logger::LogLevel::INFO>("dispatching client " + std::to_string(client));
  close(client);
}



}
