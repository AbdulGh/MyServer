#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <sys/epoll.h>
#include <utility>

#include "dispatch.h"
#include "common.h"
#include "incomingClientQueue.h"
#include "parseHTTP.h"

namespace MyServer {

constexpr int numDispatchThreads = 2;
constexpr int threadPoolSize = 4;

class Server {
private:
  using HandlerMap = std::unordered_map<std::string, Handler>;

  IncomingClientQueue incomingClientQueue {};
  std::array<HandlerMap, std::to_underlying(HTTP::Request::Method::NUM_METHODS)> handlers {};
  int serverfd;

  void handover(int client);

  template<size_t...Is>
  std::array<Dispatch, sizeof...(Is)> makeDispatchThreads(std::index_sequence<Is...>) {
    return { ((void)Is, Dispatch{incomingClientQueue})... };
  }
  std::array<Dispatch, numDispatchThreads> makeDispatchThreads() {
    return makeDispatchThreads(std::make_index_sequence<numDispatchThreads>());
  }
  std::array<Dispatch, numDispatchThreads> dispatchThreads;

public:
  static constexpr int port = 8080;
  Server(): dispatchThreads{makeDispatchThreads()} {}
  Server(const Server&) = delete;
  Server(const Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(const Server&&) = delete;

  void registerHandler(const std::string& endpoint, HTTP::Request::Method method, Handler handler);

  [[noreturn]]
  void go();
  void stop();
};

}

#endif
