#ifndef CLIENT_H
#define CLIENT_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "utils/logger.h"
#include "server/parseHTTP.h"

namespace MyServer {

class Client 
{
private:
  HTTP::RequestParser httpParser {};
  //we use the map as a queue
  std::mutex queueMutex {};
  std::map<unsigned long, std::string> outgoing;
  int written {0};
  std::atomic<int> pending {0};
  bool closing {false};
  bool wrhup {false}; //client is no longer reading - happens on hup, error
  int fd {-1};

  unsigned long sequence = 0;
  void resetSequence();
  void close();

  template <Logger::LogLevel level>
  void log(const std::string&);
  
public:
  enum class IOState { CONTINUE, ERROR, WOULDBLOCK, DONE };
  Client(int fd): fd{fd} {};
  int getfd();

  IOState handleRead();
  IOState handleWrite();
  //used on shutdown to finish writing the currently progress response, if any
  IOState writeOne();
  std::vector<Request> takeRequests();

  unsigned long incrementSequence();
  bool isPending() const;
  bool isClosing() const;
  void setClosing();
  bool notWriteable() const;
  void abort(bool withError);

  //we intend for this class to just sit in the fd->client map in the owning dispatch thread
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  Client(Client&) = delete;
  Client& operator=(Client&) = delete;

  bool addOutgoing(unsigned long sequence, std::string&& outboundStr);

  void initiateShutdown();
  ~Client();
};

}

#endif
