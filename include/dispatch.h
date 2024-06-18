#ifndef DISPATCH_H
#define DISPATCH_H

#include <sys/epoll.h>
#include <unordered_map>

#include "incomingClientQueue.h"
#include "client.h"

namespace MyServer {
class Dispatch {
private:
  static constexpr int EPOLL_EVENT_FLAGS = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
  static constexpr int maxNotifications = 1000;
  epoll_event eventBuffer[maxNotifications];

  std::thread mThread;
  IncomingClientQueue& incomingClients;
  std::unordered_map<int, Client> clients {};
  std::unordered_map<int, epoll_event> pending {};
  int epollfd {-1};

  void assumeClient(int client);
public:
  Dispatch(IncomingClientQueue&);
  void work();
  void join();
  ~Dispatch();
};

}

#endif
