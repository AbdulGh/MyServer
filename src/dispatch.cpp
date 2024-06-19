#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

#include "dispatch.h"
#include "server.h"
#include "logger.h"
#include "client.h"

namespace MyServer {

Dispatch::Dispatch(Server* server): server {server} {
  epollfd = insist(epoll_create1(0), "Couldn't create epoll");
  thread = std::thread(&Dispatch::work, this);
}

void Dispatch::work() {
  for(;;) {
    //take new clients
    if (clients.empty()) server->incomingClientQueue.wait();
    if (std::optional<int> client; (client = server->incomingClientQueue.take())) {
      assumeClient(*client);
    }

    //process existing notifications
    auto notificationIt = pending.begin();
    while (notificationIt != pending.end()) {
      int fd = notificationIt->first;
      auto clientIt = clients.find(fd);

      if (clientIt == clients.end()) {
        Logger::log<Logger::LogLevel::ERROR>("Processed notification about a missing client");
        ++notificationIt;
        continue;
      }
      Client& client = clientIt->second;

      epoll_event& event = notificationIt->second;
      if ((event.events & EPOLLIN) && !(event.events & EPOLLRDHUP)) {
        Client::IOState result = client.handleRead();
        switch (result) {
          default:
            std::unreachable();
          case Client::IOState::ERROR:
            std::cout << "error\n";
            //todo let them know
          case Client::IOState::CLOSE:
            std::cout << "close\n";
            // client.close();
          case Client::IOState::WOULDBLOCK:
            std::cout << "block\n";
            event.events ^= EPOLLIN;
          case Client::IOState::CONTINUE:
            break;
        }
      }

      if (event.events & EPOLLOUT) {
        Client::IOState result = client.handleWrite();
        //todo deduplicate
        switch (result) {
          default:
            std::unreachable();
          case Client::IOState::ERROR:
            //todo let them know
          case Client::IOState::CLOSE:
            // client.close();
          case Client::IOState::WOULDBLOCK:
            event.events ^= EPOLLOUT;
          case Client::IOState::CONTINUE:
            break;
        }
      }

      //todo move this stuff
      for (Request& request: client.takeRequests()){
        dispatchRequest(request, client);
      }
      
      if (!(event.events & (EPOLLIN | EPOLLOUT))) {
        notificationIt = pending.erase(notificationIt);
        if (client.closed()) clients.erase(clientIt);
      }
      else ++notificationIt;
    }

    //check new notifications
    ssize_t count = epoll_wait(epollfd, eventBuffer, maxNotifications, 0);
    if (count < 0) {
      Logger::log<Logger::LogLevel::ERROR>("Couldn't epoll_wait - may miss notifications!");
    }
    else for (int i = 0; i < count; ++i) {
      pending[eventBuffer[i].data.fd] = std::move(eventBuffer[i]);
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

void Dispatch::dispatchRequest(Request& request, Client& client) {
  Logger::log<Logger::LogLevel::DEBUG>("Dispatching a request");
  const HandlerMap& methodMap = server->handlers[std::to_underlying(request.method)];
  auto handlerIt = methodMap.find(request.endpoint);
  if (handlerIt == methodMap.end()) {
    Logger::log<Logger::LogLevel::DEBUG>("Couldn't find the handler");
  }
  else {
    Response response = (handlerIt->second)(request);
    client.addOutgoing(response.toHTTPResponse());
  }
}

Dispatch::~Dispatch() {
  close(epollfd);
}

}
