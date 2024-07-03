#include <cerrno>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

#include "server/client.h"
#include "server/common.h"
#include "utils/logger.h"
#include "server/parseHTTP.h"

namespace MyServer {

template <Logger::LogLevel level>
void Client::log(const std::string& str) {
  Logger::log<level>("Client " + std::to_string(fd) + ": " + str);
}

int Client::getfd() {
  return fd;
}

Client::IOState Client::handleRead() {
  if (isClosing()) return IOState::WOULDBLOCK;

  char buf[CHUNKSIZE];
  ssize_t readBytes = read(fd, buf, CHUNKSIZE);

  if (readBytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return IOState::WOULDBLOCK;
    }
    else {
      log<Logger::LogLevel::ERROR>("Failed to read from the client");
      initiateShutdown(true);
      return IOState::ERROR;
    }
  }
  else if (readBytes == 0) {
    setClosing();
    return IOState::WOULDBLOCK;
  }

  readState.process(std::string_view(buf, readBytes));
  if (readState.isError()) {
    log<Logger::LogLevel::ERROR>("Could not parse http request");
    initiateShutdown(true);
    return IOState::ERROR;
  }
  return IOState::CONTINUE;
}

Client::IOState Client::handleWrite() {
  // if a worker thread is writing to the (client-private) queue, we don't bother blocking a dispatch thread
  std::unique_lock<std::mutex> lock { queueMutex, std::try_to_lock };
  if (!lock.owns_lock()) return IOState::CONTINUE;
  else if (outgoing.empty()) return IOState::DONE;

  const std::map<unsigned long, std::string>::const_iterator cbegin = outgoing.cbegin();
  if (cbegin->first > sequence) return IOState::CONTINUE;

  size_t allotment = CHUNKSIZE;
  do {
    const std::string& out = cbegin->second;

    size_t desired = std::min(out.size() - written, allotment);
    ssize_t bytesOut = write(fd, out.data() + written, desired);

    if (bytesOut < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return IOState::WOULDBLOCK;
      else {
        initiateShutdown(true);
        return IOState::ERROR;
      }
    }
    else if (bytesOut == 0) {
      // not exactly sure what this means, if we get EPOLLIN and can't write any bytes
      initiateShutdown(true);
      return IOState::ERROR;
    }

    allotment -= bytesOut;
    written += bytesOut;

    if (written >= out.size()) {
      ++sequence;
      written = 0;
      outgoing.erase(cbegin);
    }
  } while (!outgoing.empty() && allotment > 0) ;

 if (outgoing.empty()) {
    if (pending == 0) sequence = 0;
    return IOState::DONE;
  }
  return IOState::CONTINUE;
}

Client::IOState Client::writeOne() {
  if (written == 0) return IOState::DONE;
  const std::string& out = outgoing.cbegin()->second;

  size_t desired = std::min(out.size() - written, CHUNKSIZE);
  ssize_t bytesOut = write(fd, out.data() + written, desired);

  if (bytesOut <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return IOState::WOULDBLOCK;
    else {
      initiateShutdown(true);
      return IOState::ERROR;
    }
  }

  if (written >= out.size()) return IOState::DONE;
  return IOState::CONTINUE;
}

bool Client::addOutgoing(unsigned long sequence, std::string&& outboundStr) {
  std::lock_guard<std::mutex> lock { queueMutex };
  bool awoken = false;
  if (!wrhup) {
    size_t oldSize = outgoing.size();
    outgoing.insert_or_assign(sequence, std::move(outboundStr));
    awoken = outgoing.size() > oldSize;
  }
  --pending;
  return awoken;
}

bool Client::isPending() const {
  //safe as only one dispatch thread considers a client
  return !(pending.load() == 0 && outgoing.empty() && readState.isFresh());
}

bool Client::isClosing() const {
  return closing;
}

bool Client::notWriteable() const {
  return wrhup;
}

unsigned long Client::incrementSequence() {
  return sequence++;
}

void Client::resetSequence() {
  sequence = 0;
}

void Client::close() {
  if (fd < 0) {
    log<Logger::LogLevel::FATAL> ("(Application error) Tried to close a closed socket");
  }
  if (::close(fd) < 0) {
    log<Logger::LogLevel::ERROR> ("Error while closing client fd");
  }
  fd = -1;
}

void Client::initiateShutdown(bool withError) {
  if (wrhup) return;
  std::lock_guard<std::mutex> lock(queueMutex);
  wrhup = true;
  outgoing.clear(); 
  resetSequence();
  if (withError) {
    log<Logger::LogLevel::ERROR>("Client error");
    outgoing[0] = "HTTP/1.1 400 Bad Request";
  }
  setClosing();
}

void Client::setClosing() {
  log<Logger::LogLevel::DEBUG>("Initiating client shutdown");
  closing = true;
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
