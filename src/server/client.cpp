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
  if (isClosing()) return IOState::WOULDBLOCK;

  char buf[CHUNKSIZE];
  ssize_t readBytes = read(fd, buf, CHUNKSIZE);

  if (readBytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return IOState::WOULDBLOCK;
    }
    else {
      log<Logger::LogLevel::ERROR>("Failed to read from the client");
      exit(true);
      return IOState::ERROR;
    }
  }
  else if (readBytes == 0) {
    initiateShutdown();
    return IOState::WOULDBLOCK;
  }

  readState.process(std::string_view(buf, readBytes));
  if (readState.isError()) {
    log<Logger::LogLevel::ERROR>("Could not parse http request");
    exit(true);
    return IOState::ERROR;
  }
  return IOState::CONTINUE;
}

// this looks un-threadsafe in various places, but note, this is the only place where outgoing is consumed, and by a single thread
// (the dispatch thread that owns the client)
// so for example, if we can tell outgoing is not empty, it will never be empty unless we made it so
// and sequence does not need to be atomic as we (dispatch thread) are the only people who touch it
Client::IOState Client::handleWrite() {
  if (outgoing.empty()) return IOState::CONTINUE;

  // for example, the use of cbegin leads to an eventual popFront - seems suspect,
  // but something will only 'cut in line' if we are out of sequence anyway, and the following if will break
  // in general we rely on [container.requirements.dataraces] and the fact that map iterators are not invalidated on insertion
  const std::map<unsigned long, std::string>::const_iterator cbegin = outgoing.cbegin();
  if (cbegin->first > sequence) return IOState::CONTINUE;

  // if a worker thread is writing to the (client-private) queue, we don't bother blocking a dispatch thread
  std::unique_lock<std::mutex> lock(queueMutex, std::try_to_lock);
  if (!lock.owns_lock()) return IOState::CONTINUE;

  size_t allotment = CHUNKSIZE;
  
  do {
    const std::string& out = cbegin->second;

    size_t desired = std::min(out.size() - written, allotment);
    ssize_t bytesOut = write(fd, out.data() + written, desired);

    if (bytesOut < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return IOState::WOULDBLOCK;
      else {
        exit(true);
        return IOState::ERROR;
      }
    }
    else if (bytesOut == 0) {
      // not exactly sure what this means, if we get EPOLLIN and can't write any bytes
      exit(true);
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

  if (outgoing.empty() && pending == 0) {
    sequence = 0;
  }
  return IOState::CONTINUE;
}

void Client::addOutgoing(unsigned long sequence, std::string&& outboundStr) {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (!blocked) {
    //todo check insert_or_assign for rvalue stuff
    outgoing.insert_or_assign(sequence, std::move(outboundStr));
  }
  --pending;
}

bool Client::isPending() const {
  //safe as only one dispatch thread considers a client
  return !(pending.load() == 0 && outgoing.empty() && readState.isFresh());
}

bool Client::isClosing() const {
  return closing;
}

bool Client::isBlocked() const {
  return blocked;
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
  ::close(fd);
  fd = -1;
}

void Client::exit(bool withError) {
  log<Logger::LogLevel::ERROR>("Client error");
  std::lock_guard<std::mutex> lock(queueMutex);
  blocked = true;
  outgoing.clear(); 
  resetSequence();
  if (withError) outgoing[0] = "HTTP/1.1 400 Bad Request";
  initiateShutdown();
}

void Client::initiateShutdown() {
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
