#ifndef CLIENT_H
#define CLIENT_H

#include <atomic>
#include <mutex>
#include <queue>
#include <string>

#include "logger.h"
#include "parseHTTP.h"

namespace MyServer {

class Client 
{
private:
  HTTP::RequestParser readState {};
  std::mutex queueMutex {};
  std::queue<std::string> outgoing;
  int written {0};
  int fd {-1};
  std::atomic<int> pending {0};
  bool isClosing {false};
  bool isErrored {false};
  void close();

  void errorOut();
  void initiateShutdown();
  bool closing();
  bool isPending();

  template <Logger::LogLevel level>
  void log(const std::string&);
  
public:
  enum class IOState { CONTINUE, CLOSE, ERROR, WOULDBLOCK };
  Client(int fd): fd{fd} {};
  //these two return false if the client has errored (and should be disconnected)
  IOState handleRead();
  IOState handleWrite();
  std::vector<Request> takeRequests();

  //we intend for this to just sit in the fd->client map in the owning dispatch thread
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  Client(Client&) = delete;
  Client& operator=(Client&) = delete;

  void addOutgoing(std::string&& outboundStr);

  ~Client();
};

}

#endif
