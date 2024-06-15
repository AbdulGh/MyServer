#ifndef DISPATCH_H
#define DISPATCH_H

/**
  * \brief Dispatch thread.
  *
  */
#include "server.h"
#include <thread>
namespace MyServer {

class Server::Dispatch
{
private:
  std::thread mThread;
  Server& parent;
  void dispatch(int client);

public:
  Dispatch(const Server&);
  void work();
  void join();
};

}

#endif
