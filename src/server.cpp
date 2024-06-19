#include <csignal>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "server.h"
#include "common.h"
#include "logger.h"
#include "concurrentQueue.h"
#include "task.h"

namespace MyServer {

void Server::handover(int client) {
  Logger::log<Logger::LogLevel::DEBUG>("Handing over client " + std::to_string(client));
  incomingClientQueue.add(client);
}

//todo check copies etc for std::function
void Server::registerHandler(const std::string& endpoint, Request::Method method, Handler handler) {
  handlers[std::to_underlying(method)][endpoint] = handler;
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
    if ((client = accept(serverfd, addressbp, reinterpret_cast<socklen_t*>(&addrlen))) < 0) {
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

void Server::worker(Task task) {
  Logger::log<Logger::LogLevel::DEBUG>("Starting up a worker thread");
  ++numWorkerThreads;
  //todo request stop
  while (true) {
    Response result;

    try {
      result = task.handler(task.request);
    }
    catch (std::exception e) {
      result = {
        .statusCode = Response::StatusCode::INTERNAL_SERVER_ERROR,
        .body = e.what()
      };
    }

    task.destination->addOutgoing(result.toHTTPResponse());

    if (std::optional<Task> taskOpt {taskQueue.take()})  {
      task = std::move(*taskOpt);
    }
    else break;
  }
  Logger::log<Logger::LogLevel::DEBUG>("Shutting down a worker thread");
  --numWorkerThreads;
}

}
