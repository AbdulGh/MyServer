#ifndef CLIENT_H
#define CLIENT_H

#include "parseHTTP.h"
#include <queue>
#include <string>

namespace MyServer {

class Client 
{
private:
  HTTP::RequestParser readState {};
  std::queue<std::string> outgoing;
  int written {0};
  
public:
  enum class IOState { GOOD, CLOSE, ERROR, WOULDBLOCK };
  Client(int fd): fd{fd} {};
  //these two return false if the client has errored (and should be disconnected)
  IOState handleRead();
  IOState handleWrite();
  const int fd;

  //closes the fd
  ~Client();
};

}

#endif
