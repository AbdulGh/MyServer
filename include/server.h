#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <sys/epoll.h>
#include <utility>

#include "dispatch.h"
#include "common.h"
#include "concurrentQueue.h"
#include "task.h"

namespace MyServer {

constexpr int numDispatchThreads = 2;
//currentlty we may actually go over this, but by at most the # of dispatch threads - 1
constexpr int threadPoolSize = 4;

class Server {
private:
  ConcurrentQueue<int> incomingClientQueue {};
  ConcurrentQueue<Task> taskQueue {};
  std::array<HandlerMap, std::to_underlying(Request::Method::NUM_METHODS)> handlers {};
  int serverfd;

  void handover(int client);

  friend class Dispatch;
  friend class Worker;
  template<size_t...Is>
  std::array<Dispatch, numDispatchThreads> makeDispatchThreads(std::index_sequence<Is...>);
  std::array<Dispatch, numDispatchThreads> makeDispatchThreads();
  std::array<Dispatch, numDispatchThreads> dispatchThreads;
  std::atomic<int> numWorkerThreads {0};

  void worker(Task task);
public:
  static constexpr int port = 8675;
  Server(): dispatchThreads{makeDispatchThreads()} {}
  Server(const Server&) = delete;
  Server(const Server&&) = delete;
  Server& operator=(const Server&) = delete;
  Server& operator=(const Server&&) = delete;

  void registerHandler(const std::string& endpoint, Request::Method method, Handler handler);

  [[noreturn]]
  void go();
  void stop();
};

}

#endif
