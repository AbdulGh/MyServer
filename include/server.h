#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <utility>

#include "common.h"
#include "incomingClientQueue.h"
#include "parseHTTP.h"

namespace MyServer {

constexpr int threadPoolSize = 5;

class Server {
private:
  class Dispatch;

  using HandlerMap = std::unordered_map<std::string, Handler>;

  IncomingClientQueue incomingClientQueue {};
  std::array<HandlerMap, std::to_underlying(Request::HTTPMethod::NUM_HTTP_METHODS)> handlers {};
  int serverfd;

  void handover(int client);

public:
  static constexpr int port = 8080;
  Server() = default;
  Server(const Server&) = delete;
  Server(const Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(const Server&&) = delete;

  void registerHandler(const std::string& endpoint, Request::HTTPMethod method, Handler handler);

  [[noreturn]]
  void go();
  void stop();
};

}

#endif
