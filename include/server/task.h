#ifndef TASK_H
#define TASK_H

#include "server/client.h"
#include "server/common.h"
#include "server/dispatch.h"

namespace MyServer {

struct Task {
  Client* destination;
  Dispatch* owner;
  unsigned long sequence;
  Request request;
  Handler handler;
};

}

#endif

