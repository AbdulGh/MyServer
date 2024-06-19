#ifndef TASK_H
#define TASK_H

#include "client.h"
#include "common.h"

namespace MyServer {

struct Task {
  Client* destination;
  Request request;
  Handler handler;
};

}

#endif

