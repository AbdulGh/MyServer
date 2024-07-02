#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>
#include <set>

#include "server/dispatch.h"
#include "server/server.h"
#include "server/task.h"
#include "utils/logger.h"
#include "server/client.h"

namespace MyServer {
//todo handle write errors
Dispatch::Dispatch(Server* server): server{server} {
  epollfd = insist(epoll_create1(0), "Couldn't create epoll");
  thread = std::jthread(std::bind_front(&Dispatch::work, this));
}

void Dispatch::work(std::stop_token token) {
  for(;;) {
    auto now = std::chrono::system_clock::now();
    if (now > nextStatusUpdate) {
      int pendingClients = 0;
      int closingClients = 0;
      int erroredClients = 0;

      for (const auto& [_, client]: clients) {
        pendingClients += client.isPending();
        closingClients += client.isClosing();
        erroredClients += client.isBlocked();
      }

      int read = 0; int write = 0; int rdhup = 0;
      for (const auto& [_, notification]: pendingNotifications) {
        read += (notification & EPOLLIN) > 0;
        write += (notification & EPOLLOUT) > 0;
        rdhup += (notification & EPOLLRDHUP) > 0;
      }

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "Status update: {} clients, of which {} are pending, and {} are closing, and {} are errored. {} notifications.",
          clients.size(), pendingClients, closingClients, erroredClients, pendingNotifications.size()
        )
      );

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "{} want read, {} want write, {} rdhuped",
          read, write, rdhup
        )
      );

      nextStatusUpdate = now + std::chrono::seconds{5};
    } 

    if (token.stop_requested()) {
      shutdown();
      return;
    }

    //take new clients
    if (clients.empty()) {
      if (token.stop_requested()) {
        Logger::log<Logger::LogLevel::INFO>("Dispatch thread exiting");
        break;
      }
      //todo - handling shutdown?
      Logger::log<Logger::LogLevel::INFO>("Dispatch thread waiting for work");
      server->incomingClientQueue.wait();
    }
    if (std::optional<int> client; (client = server->incomingClientQueue.take())) {
      assumeClient(*client);
    }

    //process existing notifications
    auto notificationIt = pendingNotifications.begin();
    while (notificationIt != pendingNotifications.end()) {
      int fd = notificationIt->first;
      auto clientIt = clients.find(fd);

      if (clientIt == clients.end()) {
        Logger::log<Logger::LogLevel::ERROR>("Processed notification about a missing client");
        notificationIt = pendingNotifications.erase(notificationIt);
        continue;
      }
      Client& client = clientIt->second;

      unsigned& notifications = notificationIt->second;
      bool removeClient = false;

      if (notifications & EPOLLIN) {
        if (client.handleRead() == Client::IOState::WOULDBLOCK) notifications ^= EPOLLIN;
      }

      for (Request& request: client.takeRequests()){
        dispatchRequest(std::move(request), client);
      }

      if (notifications & EPOLLHUP) client.exit(false);

      if (notifications & EPOLLOUT) {
        if (client.handleWrite() == Client::IOState::WOULDBLOCK) notifications ^= EPOLLOUT;
      }

      if ((notifications & (EPOLLRDHUP | EPOLLHUP)) && !client.isPending()) {
        clients.erase(clientIt);
        notifications = 0u;
      }

      if (notifications == 0u) {
        notificationIt = pendingNotifications.erase(notificationIt);
      }
      else ++notificationIt;
    }
    getNotifications();
  }
}

void Dispatch::getNotifications() {
  //check new notifications
  ssize_t count = epoll_wait(epollfd, eventBuffer, maxNotifications, 0);
  if (count < 0) {
    if (errno == EINTR) {
      Logger::log<Logger::LogLevel::ERROR>("EINTR during epoll_wait - checking stop token");
    }
    else {
      Logger::log<Logger::LogLevel::ERROR>("Couldn't epoll_wait - may miss notifications!");
    }
  }
  else for (int i = 0; i < count; ++i) {
    pendingNotifications[eventBuffer[i].data.fd] = eventBuffer[i].events;
  }
}

void Dispatch::assumeClient(int clientfd) {
  Logger::log<Logger::LogLevel::DEBUG>("Assuming client " + std::to_string(clientfd));

  // set client as nonblocking
  insist(
    fcntl(clientfd, F_SETFL, insist(fcntl(clientfd, F_GETFL), "could not get file flags") | O_NONBLOCK),
    "could not set file flags"
  );

  epoll_event event {
    .events = EPOLL_EVENT_FLAGS | EPOLLET,
    .data = { .fd = clientfd }
  };

  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &event) < 0) {
    Logger::log<Logger::LogLevel::ERROR>("Couldn't EPOLL_CTL_ADD a clientfd");
  }
  else clients.emplace(
    std::piecewise_construct, 
    std::forward_as_tuple(clientfd),
    std::forward_as_tuple(clientfd)
  );
  //(https://devblogs.microsoft.com/oldnewthing/20231023-00/?p=108916)
}

//dispatch thread may block here...
void Dispatch::dispatchRequest(Request&& request, Client& client) {
  Logger::log<Logger::LogLevel::DEBUG>("Dispatching a request");
  const HandlerMap& methodMap = server->handlers[std::to_underlying(request.method)];
  auto handlerIt = methodMap.find(request.endpoint);
  if (handlerIt == methodMap.end()) {
    Logger::log<Logger::LogLevel::DEBUG>("Couldn't find the handler");
    client.addOutgoing(client.incrementSequence(), "HTTP/1.1 404 Not Found\r\nContent-Length: 0");
  }
  else {
    server->workerThreads[dist(eng)].add(
      Task{
        .destination = &client, .sequence = client.incrementSequence(),
        .request = std::move(request), .handler = handlerIt->second
      }
    );
  }
}

void Dispatch::shutdown() {
  exiting.test_and_set();
  exiting.notify_all();
  //diminished event loop just to handle remaining outgoing
  bool finished = false;
  while (!finished) {
    finished = true;

    auto notificationIt = pendingNotifications.begin();
    while (notificationIt != pendingNotifications.end()) {
      int fd = notificationIt->first;
      auto clientIt = clients.find(fd);

      if (clientIt == clients.end()) {
        Logger::log<Logger::LogLevel::ERROR>("Processed notification about a missing client");
        notificationIt = pendingNotifications.erase(notificationIt);
        continue;
      }
      Client& client = clientIt->second;

      unsigned& notifications = notificationIt->second;

      if (notifications & EPOLLOUT) {
        Client::IOState state = client.writeOne();
        if (state != Client::IOState::DONE) finished = false;
        if (state != Client::IOState::CONTINUE) {
          notifications = 0;
          if (state == Client::IOState::ERROR) {
            //not really a problem, we just wont bother finishing the response
            Logger::log<Logger::LogLevel::ERROR>("Client errored during shutdown");
            client.exit(true);
          }
        } 
      }
      else notifications = 0u;

      if (!notifications) {
        notificationIt = pendingNotifications.erase(notificationIt);
      }
      else ++notificationIt;
    }

    getNotifications();
  }

  Logger::log<Logger::LogLevel::INFO>("Finished writing to clients, waiting for worker threads to exit");
  for (const auto& worker: server->workerThreads) {
    worker.waitForExit();
  }

  Logger::log<Logger::LogLevel::INFO>("Clearing clients");
  clients.clear();

  Logger::log<Logger::LogLevel::INFO>("Clients closed, dispatch thread exiting");
}

void Dispatch::requestStop() {
  thread.request_stop();
}

void Dispatch::acknowledgeShutdown() {
  exiting.wait(false);
}

void Dispatch::join() {
  thread.join();
}

Dispatch::~Dispatch() {
  close(epollfd);
}

}
