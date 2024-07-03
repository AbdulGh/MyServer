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

class Server {
private:
  static constexpr int numDispatchThreads = 2; //todo should be customisable
  Utils::ConcurrentQueue<int> incomingClientQueue {};
  std::array<HandlerMap, std::to_underlying(Request::Method::NUM_METHODS)> handlers {};
  int serverfd;

  void handover(int client);

  static std::atomic<bool> exiting;
  static std::vector<Server*> servers;
  static void sigint(int);

  friend class Dispatch;

  template <typename ThreadType, size_t N, typename TupleType, size_t... Is>
  static std::array<ThreadType, N> makeThreadsHelper(TupleType args, std::index_sequence<Is...>) {
    return { ((void)Is, std::make_from_tuple<ThreadType>(args))...};
  }
  template <typename ThreadType, size_t N, typename... Args>
  static std::array<ThreadType, N> makeThreads(Args... args) {
    return makeThreadsHelper<ThreadType, N>(
      std::forward_as_tuple(args...),
      std::make_index_sequence<N>()
    );
  }

  std::array<Worker, Dispatch::threadPoolSize> workerThreads;
  std::array<Dispatch, numDispatchThreads> dispatchThreads;

public:
  Server():
    dispatchThreads{makeThreads<Dispatch, numDispatchThreads>(this)},
    workerThreads{makeThreads<Worker, Dispatch::threadPoolSize>()}
  {}

  Server(const Server&) = delete;
  Server(const Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(const Server&&) = delete;

  void registerHandler(std::string endpoint, Request::Method method, Handler handler);
  void shutdown();

  void go(int port);
};

}

#endif
