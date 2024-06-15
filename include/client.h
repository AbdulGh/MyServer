#ifndef CLIENT_H
#define CLIENT_H

#include <string>

namespace MyServer {

class Client 
{
private:
  std::string incoming;
  std::string outgoing;

public:
  Client(int fd): fd{fd} {};
  void handleRead();
  void handleWrite();
  const int fd;

  //closes the fd
  ~Client();
};

}

#endif
