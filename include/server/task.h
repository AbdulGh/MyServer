#ifndef TASK_H
#define TASK_H

#include "server/client.h"
#include "server/common.h"

namespace MyServer {

struct Task {
  Client* destination;
  unsigned long sequence;
  Request request;
  Handler handler;
};

}

#endif

