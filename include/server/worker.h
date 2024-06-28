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
  //managed by same mutex
  bool deadOrDying {true};

  void work(Task&& task);
public:
  void add(Task&& task);
};

}
#endif
