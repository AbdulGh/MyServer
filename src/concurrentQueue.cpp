#include <optional>
#include <sys/epoll.h>
#include <fcntl.h>

#include "concurrentQueue.h"
#include "task.h"

namespace MyServer {

template <typename T>
std::optional<T> ConcurrentQueue<T>::take() {
  std::lock_guard<std::mutex> lock {mutex};
  if (queue.empty()) return {};
  T result = queue.front();
  queue.pop();
  return result;
}

template <typename T>
void ConcurrentQueue<T>::wait() {
  if (queue.empty()) {
    std::unique_lock<std::mutex> lock {mutex};
    cv.wait(lock, [this]() { return !queue.empty(); });
  }
}

template <typename T>
void ConcurrentQueue<T>::add(const T& addition) {
  std::lock_guard<std::mutex> lock {mutex};
  queue.push(addition);
  cv.notify_one();
}

template class ConcurrentQueue<int>; //incomingClientQueue
template class ConcurrentQueue<Task>; //taskQueue

}
