#include "server/worker.h"
#include "utils/httpException.h"

namespace MyServer {

void Worker::work(std::stop_token token, Task&& task) {
  Logger::log<Logger::LogLevel::DEBUG>("Starting up a worker thread");

  while (!token.stop_requested()) {
    Response result;

    try {
      result = task.handler(task.request);
    }
    catch (Utils::HTTPException e) {
      result = {
        .statusCode = e.statusCode(),
        .body = e.what()
      };
    }
    catch (std::exception e) {
      Logger::log<Logger::LogLevel::ERROR>(std::string{"Uncaught exception: "} + e.what());
      result = {
        .statusCode = Response::StatusCode::INTERNAL_SERVER_ERROR,
        .body = "Sorry, something went wrong - we are working extremely hard to find the problem"
      };
    }

    task.destination->addOutgoing(task.sequence, result.toHTTPResponse());

    std::lock_guard<std::mutex> lock{queueMutex};
    if (!(taskQueue.empty() || token.stop_requested())) {
      task = std::move(taskQueue.front());
      taskQueue.pop();
    }
    else {
      deadOrDying = true;
      break;
    } 
  }

  if (token.stop_requested()) {
    Logger::log<Logger::LogLevel::DEBUG>("Exiting a worker thread");
  }
  else {
    Logger::log<Logger::LogLevel::DEBUG>("Scaling down the thread pool");
  }
}

void Worker::add(Task&& task) {
  std::lock_guard<std::mutex> lock{queueMutex};
  if (deadOrDying) {
    deadOrDying = false;
    thread = std::jthread{
      std::bind_front(&Worker::work, this),
      std::move(task)
    };
  }
  else taskQueue.push(std::move(task));
}

void Worker::requestStop() {
  thread.request_stop();
}

void Worker::waitForExit() const {
  // could probably be improved - it's the only (explicit) busy wait in the application - but it's just during the shutdown
  // and I didn't want to introduce an atomic flag or cv to do this
  while (!deadOrDying) {
    std::this_thread::yield();
  }
}

}
