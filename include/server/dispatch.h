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

  std::jthread thread;
  std::unordered_map<int, Client> clients {};
  std::unordered_map<int, unsigned> pendingNotifications {};
  int epollfd {-1};

  //for basic load balancing
  std::minstd_rand eng {std::random_device{}()};
  std::uniform_int_distribution<> dist{0, threadPoolSize - 1};

  void work(std::stop_token);
  void assumeClient(const int client);
  void dispatchRequest(Request&& request, Client& destination);
  void getNotifications();

  //set to promise the SIGINT handler that we will start up no more worker threads
  std::atomic_flag exiting {false};
  void shutdown();
public:
  static constexpr int threadPoolSize = 6; 
  Dispatch(Server* parent);
  void join();

  void requestStop();
  void acknowledgeShutdown();
  ~Dispatch();
};

}

#endif
