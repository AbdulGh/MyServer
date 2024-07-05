#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

#include "server/dispatch.h"
#include "server/server.h"
#include "server/task.h"
#include "utils/logger.h"
#include "server/client.h"

namespace MyServer {
//todo disconnect inactive clients
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
        erroredClients += client.notWriteable();
      }

      int read = 0; int write = 0; int rdhup = 0;
      for (const auto& [_, notification]: pendingNotifications) {
        read += (notification & EPOLLIN) > 0;
        write += (notification & EPOLLOUT) > 0;
        rdhup += (notification & EPOLLRDHUP) > 0;
      }

      int taskCount = 0;
      for (const Worker& worker: server->workerThreads) {
        taskCount += worker.tasks();
      }

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "Status update: {} clients, of which {} are pending, and {} are closing, and {} are errored. {} notifications.",
          clients.size(), pendingClients, closingClients, erroredClients, pendingNotifications.size()
        )
      );

      Logger::log<Logger::LogLevel::INFO>(
        std::format(
          "{} want read, {} want write, {} rdhuped. {} many tasks queued",
          read, write, rdhup, taskCount
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
      Logger::log<Logger::LogLevel::INFO>("Dispatch thread waiting for work");
      server->incomingClientQueue.wait();
    }
    if (std::optional<int> client; (client = server->incomingClientQueue.take())) {
      if (client == -1) {
        // main thread does this to wake us up on shutdown
        shutdown();
        return;
      }
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

      unsigned& clientNotifications = notificationIt->second;
      bool removeClient = false;

      if (clientNotifications & EPOLLIN) {
        Client::IOState state = client.handleRead();
        if (state == Client::IOState::WOULDBLOCK) clientNotifications ^= EPOLLIN;
        else if (state == Client::IOState::ERROR) {
          clientNotifications = 0u;
        }
      }

      for (Request& request: client.takeRequests()){
        dispatchRequest(std::move(request), client);
      }

      if (clientNotifications & EPOLLHUP) {
        client.initiateShutdown();
        clientNotifications ^= EPOLLHUP;
      }

      if (clientNotifications & EPOLLRDHUP) {
        client.setClosing();
        clientNotifications ^= EPOLLRDHUP;
      }

      if (clientNotifications & EPOLLOUT) {
        //this also handles the error - the client shuts itself down in this case, and we drop the notification
        //if still pending the last worker thread will let us know to check again (and close it)
        if (client.handleWrite() != Client::IOState::CONTINUE) clientNotifications ^= EPOLLOUT;
      }

      if (client.isClosing() && !client.isPending()) {
        clients.erase(clientIt);
        clientNotifications = 0u;
      }

      if (clientNotifications == 0u) {
        notificationIt = pendingNotifications.erase(notificationIt);
      }
      else ++notificationIt;
    }

    doEpoll();
    //additionally, see if we now want to write to some dormant clients
    //clientsWantWrite.take() may block, but only for a single insertion to a set
    for (int awokenClient: clientsWantWrite.take()) {
      pendingNotifications[awokenClient] |= EPOLLOUT;
    }
  }
}

void Dispatch::doEpoll() {
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

//dispatch thread may block here, by a worker thread and/or the other dispatch threads
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
      Task {
        .destination = &client, .owner = this,
        .sequence = client.incrementSequence(),
        .request = std::move(request),
        .handler = handlerIt->second
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

      unsigned& clientNotifications = notificationIt->second;

      if (clientNotifications & EPOLLOUT) {
        Client::IOState state = client.writeOne();
        if (state != Client::IOState::DONE) finished = false;
        if (state != Client::IOState::CONTINUE) {
          clientNotifications = 0;
          if (state == Client::IOState::ERROR) {
            //not really a problem, we just wont bother finishing the response
            client.initiateShutdown();
          }
        } 
      }
      else clientNotifications = 0u;

      if (!clientNotifications) {
        notificationIt = pendingNotifications.erase(notificationIt);
      }
      else ++notificationIt;
    }

    doEpoll();
  }

  Logger::log<Logger::LogLevel::INFO>("Finished writing to clients, waiting for worker threads to exit");
  for (const auto& worker: server->workerThreads) {
    worker.waitForExit();
  }

  Logger::log<Logger::LogLevel::INFO>("Clearing clients");
  clients.clear();

  Logger::log<Logger::LogLevel::INFO>("Clients closed, dispatch thread exiting");
}

void Dispatch::notifyForClient(int clientfd) {
  clientsWantWrite.add(clientfd);
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
