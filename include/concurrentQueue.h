#ifndef GUARDED_QUEUE_H
#define GUARDED_QUEUE_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace MyServer {

template <typename T>
class ConcurrentQueue
{
protected:
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<T> queue {};

public:
  // returns immediately if there's nothing new
  std::optional<T> take();
  // you call this if you have nothing better to do
  void wait();
  void add(const T& addition);
};

}
#endif
