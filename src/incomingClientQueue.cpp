#include <sys/epoll.h>
#include <fcntl.h>

#include "incomingClientQueue.h"
#include "logger.h"

namespace MyServer {

int IncomingClientQueue::take() {
  int client;
  {
    std::lock_guard<std::mutex> lock {mutex};
    if (queue.empty()) return -1;
    client = queue.front();
    queue.pop();
  }

  // set client as nonblocking
  insist(
    fcntl(client, F_SETFL, insist(fcntl(client, F_GETFL), "could not get file flags") | O_NONBLOCK),
    "could not set file flags"
  );
   
  return client;
}

void IncomingClientQueue::wait() {
  if (queue.empty()) {
    std::unique_lock<std::mutex> lock {mutex};
    cv.wait(lock, [this]() { return !queue.empty(); });
  }
}

void IncomingClientQueue::add(int clientfd) {
  std::lock_guard<std::mutex> lock {mutex};
  queue.push(clientfd);
  cv.notify_one();
}

}
