#ifndef CONCURRENTQUEUE_H
#define CONCURRENTQUEUE_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>

namespace MyServer::Utils {

template <typename T, typename Container = std::deque<T>>
class ConcurrentQueue
{
protected:
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::queue<T, Container> queue {};

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
      cv.wait(lock, [this]{ return !queue.empty(); });
    }
  }

  void add(const T& addition) {
    std::lock_guard<std::mutex> lock {mutex};
    queue.push(addition);
    cv.notify_one();
  }

  template <std::ranges::forward_range Range>
  [[maybe_unused]] std::queue<T, Container> swap(Range range) {
    std::lock_guard<std::mutex> lock {mutex};
    std::queue<T, Container> newQueue {};
    for (const T& el: range) {
      newQueue.push(el);
    }
    std::swap(queue, newQueue);
    cv.notify_all();
    return newQueue;
  }
};

}
#endif
