#ifndef WORKER_H
#define WORKER_H

#include "server/task.h"
#include <queue>

namespace MyServer {

// didnt want to make the ConcurrentQueue virtual
class Worker
{
private:
  std::mutex queueMutex {};
  std::queue<Task> taskQueue {};
  //managed by the above mutex
  bool deadOrDying {true};
  std::jthread thread;

  void work(std::stop_token, Task&&);
public:
  void add(Task&&);
  void requestStop();
  void waitForExit() const;
};

}
#endif
