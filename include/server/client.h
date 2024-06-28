#ifndef CLIENT_H
#define CLIENT_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "utils/concurrentMap.h"
#include "utils/logger.h"
#include "server/parseHTTP.h"

namespace MyServer {

class Client 
{
private:
  HTTP::RequestParser readState {};
  //we use the map as a queue
  std::mutex queueMutex {};
  std::map<unsigned long, std::string> outgoing;
  int written {0};
  int fd {-1};
  std::atomic<int> pending {0};
  bool closing {false};
  bool blocked {false}; //client is no longer reading

  unsigned long sequence = 0;
  void resetSequence();

  void close();
  void initiateShutdown();
  template <Logger::LogLevel level>
  void log(const std::string&);
  
public:
  enum class IOState { CONTINUE, ERROR, WOULDBLOCK };
  Client(int fd): fd{fd} {};

  IOState handleRead();
  IOState handleWrite();
  std::vector<Request> takeRequests();

  unsigned long incrementSequence();
  bool isPending() const;
  bool isClosing() const;
  bool isBlocked() const;
  void exit(bool withError);

  //we intend for this to just sit in the fd->client map in the owning dispatch thread
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  Client(Client&) = delete;
  Client& operator=(Client&) = delete;

  void addOutgoing(unsigned long sequence, std::string&& outboundStr);

  ~Client();
};

}

#endif
