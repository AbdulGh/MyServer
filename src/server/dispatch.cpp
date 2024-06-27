#include <cstring>
#include <fcntl.h>
#include <format>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

#include "server/dispatch.h"
#include "server/server.h"
#include "utils/logger.h"
#include "server/client.h"

namespace MyServer {

Dispatch::Dispatch(Server* server): server {server} {
  epollfd = insist(epoll_create1(0), "Couldn't create epoll");
  thread = std::thread(&Dispatch::work, this);
}

void Dispatch::work() {
  for(;;) {
    auto now = std::chrono::system_clock::now();
    if (now > nextStatusUpdate) {
      int pendingClients = 0;
      int closingClients = 0;
      int erroredClients = 0;

      for (const auto& [_, client]: clients) {
        pendingClients += client.isPending();
        closingClients += client.isClosing();
        erroredClients += client.isErrored();
      }

      int read = 0; int write = 0; int rdhup = 0;
      for (const auto& [_, notification]: pendingNotifications) {
        read += (notification & EPOLLIN) > 0;
        write += (notification & EPOLLOUT) > 0;
        rdhup += (notification & EPOLLRDHUP) > 0;
      }

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "Status update: {} clients, of which {} are pending, and {} are closing, and {} are errored. {} notifications and {} active worker threads.",
          clients.size(), pendingClients, closingClients, erroredClients, pendingNotifications.size(), server->numWorkerThreads.load()
        )
      );

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "{} want read, {} want write, {} rdhuped.",
          read, write, rdhup
        )
      );

      nextStatusUpdate = now + std::chrono::seconds{5};
    } 

    //take new clients
    if (clients.empty()) {
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

      if (notifications & EPOLLHUP) {
        //todo
        client.errorOut();
      }

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

    //check new notifications
    ssize_t count = epoll_wait(epollfd, eventBuffer, maxNotifications, 0);
    if (count < 0) {
      Logger::log<Logger::LogLevel::ERROR>("Couldn't epoll_wait - may miss notifications!");
    }
    else for (int i = 0; i < count; ++i) {
      pendingNotifications[eventBuffer[i].data.fd] = eventBuffer[i].events;
    }
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

void Dispatch::dispatchRequest(Request&& request, Client& client) {
  Logger::log<Logger::LogLevel::DEBUG>("Dispatching a request");
  const HandlerMap& methodMap = server->handlers[std::to_underlying(request.method)];
  auto handlerIt = methodMap.find(request.endpoint);
  if (handlerIt == methodMap.end()) {
    Logger::log<Logger::LogLevel::DEBUG>("Couldn't find the handler");
    //todo throw 404
  }
  else {
    Task newTask = Task{.destination = &client, .request = std::move(request), .handler = handlerIt->second};
    if (server->numWorkerThreads < threadPoolSize) {
      std::thread(
        &Server::worker,
        server,
        std::move(newTask)
      ).detach();
    }
    else {
      Logger::log<Logger::LogLevel::DEBUG>("Threadpool busy - enqueuing task");
      //only place the dispatch thread may block...
      server->taskQueue.add(newTask);
    }
  }
}

Dispatch::~Dispatch() {
  close(epollfd);
}

}
