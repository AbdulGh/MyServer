#include <cerrno>
#include <csignal>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <utility>

#include "server/server.h"
#include "server/common.h"
#include "utils/logger.h"
#include "utils/concurrentQueue.h"

namespace MyServer {

std::vector<Server*> Server::servers {};
std::atomic<bool> Server::exiting { false };
void Server::handover(int client) {
  Logger::log<Logger::LogLevel::DEBUG>("Handing over client " + std::to_string(client));
  incomingClientQueue.add(client);
}

void Server::registerHandler(std::string endpoint, Request::Method method, Handler handler) {
  handlers[std::to_underlying(method)][std::move(endpoint)] = std::move(handler);
}

void Server::go(int port) {
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  sockaddr* const addressbp = reinterpret_cast<sockaddr*>(&address);

  serverfd = insist(socket(AF_INET, SOCK_STREAM, 0), "Couldn't make server socket");
  insist(setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)), "Couldn't set the socket as reusable");
  insist(bind(serverfd, addressbp, sizeof(address)), "Couldn't bind to server socket");
  insist(listen(serverfd, 128), "Couldn't listen to server file descriptor");

  servers.push_back(this);

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = sigint;
  insist(sigaction(SIGINT, &sa, NULL), "Couldn't registed SIGINT handler");

  Logger::log<Logger::LogLevel::INFO>("Server listening on port " + std::to_string(port));
  int client;
  while (!exiting) {
    client = accept(serverfd, addressbp, reinterpret_cast<socklen_t*>(&addrlen));
    if (client < 0) {
      if (exiting) {
        Logger::log<Logger::LogLevel::DEBUG>("Server accept interrupted by exit");
        break;
      }
      else if (errno == ECONNABORTED) {
        Logger::log<Logger::LogLevel::ERROR>("Client aborted connection during except");
        continue;
      }
      else {
        Logger::log<Logger::LogLevel::ERROR>("Received unhandled error during accept");
        return;
      }
    }
    handover(client);
  }
  Logger::log<Logger::LogLevel::INFO>("Exiting server");
  shutdown();
}

// Interestingly, we don't need to bother with SA_RESTART, the default (non-restarting) behaviour is what we wanted anyway.
// From man signal, read/write are only restarted if they are blocking (not us)
// accept, epoll_wait never restart for any setting of the flag
// close may return EINTR, but according to Linus, we are to basically consider this a success: https://lkml.org/lkml/2005/9/10/129
// It turns out this is that not well specified (what? do we retry or not?!) but on Linux it's basically just a successful close
// (and we don't do anything with an erroneous close anyway apart from log it)
// Our other syscalls are not interruptible, or at least, man -wK EINTR makes me think so

// Anyway, the way shutdown works is:
// - First, we request stop frop the dispatch threads. We wake up any blocked dispatch threads by writing as many -1s to the client queue
// - Then we wait for them to set a flag that acknowleding that they are closing
//    - The point being that from now on they will spin up no more worker threads
// - Then we go through the worker threads and request stop
// -  - They check their stop token before they accept any work/push their result to an outgoing queue
// - In the meantime, the dispatch threads finishing writing any requests they were half way through writing (if any)
// - Then they check that all the worker threads have exited (as they maintain references to clients)
// - Then they close all the clients, and then we are done
void Server::shutdown() {
  Logger::log<Logger::LogLevel::INFO>("Requesting stop from dispatchers");
  for (Dispatch& dispatch: dispatchThreads) dispatch.requestStop();

  std::vector dummyClients(numDispatchThreads, -1);
  incomingClientQueue.swap(dummyClients);

  Logger::log<Logger::LogLevel::INFO>("Waiting for dispatch shutdown acknowledgement");
  for (Dispatch& dispatch: dispatchThreads) dispatch.acknowledgeShutdown();

  Logger::log<Logger::LogLevel::INFO>("Requesting stop from workers");
  for (Worker& worker: workerThreads) worker.requestStop();

  Logger::log<Logger::LogLevel::INFO>("Waiting for dispatch threads to wind down");
  for (Dispatch& dispatch: dispatchThreads) dispatch.join();

  Logger::log<Logger::LogLevel::INFO>("Shutdown complete, exiting");
}

// we dont do much in the sigint, because any mutex that we might touch could already be locked
void Server::sigint(int) {
  if (exiting) {
    exit(0);
  }
  exiting = true;
  for (Server* server: servers) {
    ::shutdown(server->serverfd, SHUT_RDWR);
    close(server->serverfd);
  }
}

}
