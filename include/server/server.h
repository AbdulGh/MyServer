#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <sys/epoll.h>
#include <utility>

#include "server/dispatch.h"
#include "server/common.h"
#include "server/worker.h"
#include "utils/concurrentQueue.h"

namespace MyServer {

constexpr int numDispatchThreads = 2;
//currentlty we may actually go over this, but by at most the # of dispatch threads - 1
constexpr int threadPoolSize = 4;

class Server {
private:
  Utils::ConcurrentQueue<int> incomingClientQueue {};
  std::array<HandlerMap, std::to_underlying(Request::Method::NUM_METHODS)> handlers {};
  int serverfd;

  void handover(int client);

  friend class Dispatch;

  template<size_t...Is>
  std::array<Dispatch, numDispatchThreads> makeDispatchThreads(std::index_sequence<Is...>);
  std::array<Dispatch, numDispatchThreads> makeDispatchThreads();

  //todo
  template<size_t...Is>
  std::array<Worker, threadPoolSize> makeWorkerThreads(std::index_sequence<Is...>);
  std::array<Worker, threadPoolSize> makeWorkerThreads();

  std::array<Worker, threadPoolSize> workerThreads;
  std::array<Dispatch, numDispatchThreads> dispatchThreads;

public:
  static constexpr int port = 8675;
  Server():
    dispatchThreads{makeDispatchThreads()},
    workerThreads{makeWorkerThreads()}
  {}
  Server(const Server&) = delete;
  Server(const Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(const Server&&) = delete;

  void registerHandler(std::string endpoint, Request::Method method, Handler handler);

  [[noreturn]]
  void go();
  void stop();
};

}

#endif
