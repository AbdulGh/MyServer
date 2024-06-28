#include "server/worker.h"
#include "utils/httpException.h"

namespace MyServer {

void Worker::work(Task&& task) {
  Logger::log<Logger::LogLevel::DEBUG>("Starting up a worker thread");

  //todo request stop
  while (true) {
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
    if (!taskQueue.empty()) {
      task = std::move(taskQueue.front());
      taskQueue.pop();
    }
    else {
      deadOrDying = true;
      break;
    } 
  }

  Logger::log<Logger::LogLevel::DEBUG>("Shutting down a worker thread");
}

void Worker::add(Task&& task) {
  std::lock_guard<std::mutex> lock{queueMutex};
  if (deadOrDying) {
    deadOrDying = false;
    std::thread(
      &Worker::work,
      this,
      std::move(task)
    ).detach();
  }
  else taskQueue.push(std::move(task));
}

}
