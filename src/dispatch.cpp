#include "dispatch.h"
#include "logger.h"
#include <string>
#include <sys/epoll.h>
#include <unistd.h>

namespace MyServer {

Server::Dispatch::Dispatch(Server& parent): parent{parent} {
  int epollfd = insist(epoll_create1(0), "Couldn't create epoll");
}

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

Server::Dispatch::~Dispatch() {
  close(epollfd);
}

}
