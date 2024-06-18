#ifndef CLIENT_H
#define CLIENT_H

#include <queue>
#include <string>

#include "parseHTTP.h"

namespace MyServer {

class Client 
{
private:
  HTTP::RequestParser readState {};
  std::queue<std::string> outgoing;
  int written {0};
  int fd {-1};

public:
  enum class IOState { CONTINUE, CLOSE, ERROR, WOULDBLOCK };
  Client(int fd): fd{fd} {};
  //these two return false if the client has errored (and should be disconnected)
  IOState handleRead();
  IOState handleWrite();

  //we intend for this to just sit in the fd->client map in the owning dispatch thread
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  Client(Client&) = delete;
  Client& operator=(Client&) = delete;

  void close();
  bool closed();

  ~Client();
};

}

#endif
