#ifndef DISPATCH_H
#define DISPATCH_H

#include <chrono>
#include <random>
#include <sys/epoll.h>
#include <thread>
#include <unordered_map>

#include "server/client.h"

namespace MyServer {

class Server;

class Dispatch {
private:
  static constexpr int EPOLL_EVENT_FLAGS = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;
  static constexpr int maxNotifications = 1000;
  epoll_event eventBuffer[maxNotifications];
  Server* const server {nullptr};

  std::chrono::time_point<std::chrono::system_clock> nextStatusUpdate {
    std::chrono::time_point<std::chrono::system_clock>::min()
  };

  std::thread thread;
  std::unordered_map<int, Client> clients {};
  std::unordered_map<int, unsigned> pendingNotifications {};
  int epollfd {-1};

  std::minstd_rand eng {std::random_device{}()};
  std::uniform_int_distribution<> dist{0, threadPoolSize - 1};

  void assumeClient(const int client);
  void dispatchRequest(Request&& request, Client& destination);
public:
  static constexpr int threadPoolSize = 6; 
  Dispatch(Server* parent);
  void work();
  void join();
  ~Dispatch();
};

}

#endif
