#ifndef GUARDED_QUEUE_H
#define GUARDED_QUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>

// this queue is used by the master thread to pass fds to worker threads for addition to their interest set
// **NOTE**: this queue only should have only one consumer (the worker thread) - otherwise the wait/pop until empty would have obvious problems
// and as we empty the entire 'queue' into epoll, we just leave it as a vector

namespace MyServer {
class IncomingClientQueue
{
private:
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<int> queue {};

public:
  // returns immediately if there's nothing new
  int take();
  // you call this if you have nothing better to do
  void wait();
  // called by the main thread (producer) after accepting a client
  void add(int clientfd);
};

}
#endif
