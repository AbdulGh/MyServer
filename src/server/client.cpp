#include "server/client.h"
#include "server/common.h"
#include "utils/logger.h"
#include "server/parseHTTP.h"
#include <cerrno>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace MyServer {

template <Logger::LogLevel level>
void Client::log(const std::string& str) {
  Logger::log<level>("Client " + std::to_string(fd) + ": " + str);
}

Client::IOState Client::handleRead() {
  if (isClosing) return IOState::WOULDBLOCK;
  char buf[CHUNKSIZE];
  ssize_t readBytes = read(fd, buf, CHUNKSIZE);

  if (readBytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return IOState::WOULDBLOCK;
    }
    else {
      log<Logger::LogLevel::ERROR>("Failed to read from the client");
      errorOut();
      return IOState::ERROR;
    }
  }
  else if (readBytes == 0) {
    initiateShutdown();
    return IOState::WOULDBLOCK;
  }

  readState.process(std::string_view(buf, readBytes));
  if (readState.isError()) {
    readState.reset();
    return IOState::ERROR;
  }
  return IOState::CONTINUE;
}

Client::IOState Client::handleWrite() {
  //if a worker thread is writing to the queue, we don't bother blocking a dispatch thread
  if (queueMutex.try_lock()) {
    size_t allotment = CHUNKSIZE;
    while (!outgoing.empty() && allotment > 0) {
      const std::string& out = outgoing.front();
      size_t desired = std::min(out.size() - written, allotment);
      ssize_t bytesOut = write(fd, out.data() + written, desired);

      if (bytesOut < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return IOState::WOULDBLOCK;
        }
        else {
          errorOut();
          return IOState::ERROR;
        }
      }
      else if (bytesOut == 0) {
        //todo what does this case actually mean?
        errorOut();
        return IOState::ERROR;
      }

      allotment -= bytesOut;
      written += bytesOut;

      if (written >= out.size()) {
        written = 0;
        outgoing.pop();
      }
    }
    queueMutex.unlock();
  }

  if (pending == 0 && outgoing.empty()) {
    return IOState::CLOSE;
  }
  return IOState::CONTINUE;
}

void Client::addOutgoing(std::string&& outboundStr) {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (isErrored) return;
  outgoing.push(std::move(outboundStr));
  --pending;
}

bool Client::isPending() {
  //safe as only one dispatch thread considers a client
  return pending.load() != 0 && !outgoing.empty();
}

void Client::close() {
  if (fd < 0) {
    log<Logger::LogLevel::FATAL> ("(Application error) Tried to close a closed socket");
  }
  ::close(fd);
  fd = -1;
}

void Client::errorOut() {
  log<Logger::LogLevel::ERROR>("Client error");
  isErrored = true;
  std::lock_guard<std::mutex> lock(queueMutex);
  outgoing = {};
  //todo - sometimes this is actually a 5xx...
  outgoing.push("HTTP/1.1 400 Bad Request");
  initiateShutdown();
}

void Client::initiateShutdown() {
  log<Logger::LogLevel::DEBUG>("Initiating client shutdown");
  isClosing = true;
}

std::vector<Request> Client::takeRequests() {
  std::vector requests = readState.takeRequests();
  pending += requests.size();
  return requests;
}

Client::~Client() {
  log<Logger::LogLevel::DEBUG>("Client shutdown complete");
  close();
}

}
