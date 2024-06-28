#ifndef CONCURRENTQUEUE_H
#define CONCURRENTQUEUE_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace MyServer::Utils {

template <typename T>
class ConcurrentQueue
{
protected:
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<T> queue {};

public:
  // returns immediately if there's nothing new
  std::optional<T> take() {
    std::lock_guard<std::mutex> lock {mutex};
    if (queue.empty()) return {};
    T result = queue.front();
    queue.pop();
    return result;
  }

  // you call this if you have nothing better to do
  void wait() {
    if (queue.empty()) {
      std::unique_lock<std::mutex> lock {mutex};
      cv.wait(lock, [this]() { return !queue.empty(); });
    }
  }

  void add(const T& addition) {
    std::lock_guard<std::mutex> lock {mutex};
    queue.push(addition);
    cv.notify_one();
  }

  size_t size() {
    return queue.size();
  }
};

}
#endif
