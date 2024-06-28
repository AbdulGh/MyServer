#include <csignal>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "server/server.h"
#include "server/common.h"
#include "utils/logger.h"
#include "utils/concurrentQueue.h"

namespace MyServer {

void Server::handover(int client) {
  Logger::log<Logger::LogLevel::DEBUG>("Handing over client " + std::to_string(client));
  incomingClientQueue.add(client);
}

//todo check copies etc for std::function
void Server::registerHandler(std::string endpoint, Request::Method method, Handler handler) {
  handlers[std::to_underlying(method)][std::move(endpoint)] = std::move(handler);
}

[[noreturn]]
void Server::go() {
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(Server::port);
  sockaddr* const addressbp = reinterpret_cast<sockaddr*>(&address);

  // TCP socket
  serverfd = insist(socket(AF_INET, SOCK_STREAM, 0), "Couldn't make server socket");
  insist(setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)), "Todo");
  insist(bind(serverfd, addressbp, sizeof(address)), "Couldn't bind to server socket");
  insist(listen(serverfd, 128), "Couldn't listen to server file descriptor");

  Logger::log<Logger::LogLevel::INFO>("Server listening on port " + std::to_string(Server::port));

  int client;
  for (;;) {
    client = accept(serverfd, addressbp, reinterpret_cast<socklen_t*>(&addrlen));
    if (client < 0) {
      Logger::log<Logger::LogLevel::ERROR>("Couldn't accept a client");
      continue;
    }
    handover(client);
  }
}

void Server::stop() {
  close(serverfd);
}

template<size_t...Is>
std::array<Dispatch, numDispatchThreads> Server::makeDispatchThreads(std::index_sequence<Is...>) {
  return { ((void)Is, Dispatch{this})... };
}
std::array<Dispatch, numDispatchThreads> Server::makeDispatchThreads() {
  return makeDispatchThreads(std::make_index_sequence<numDispatchThreads>());
}

template<size_t...Is>
std::array<Worker, threadPoolSize> Server::makeWorkerThreads(std::index_sequence<Is...>) {
  return { ((void)Is, Worker{})... };
}
std::array<Worker, threadPoolSize> Server::makeWorkerThreads() {
  return makeWorkerThreads(std::make_index_sequence<threadPoolSize>());
}

}
