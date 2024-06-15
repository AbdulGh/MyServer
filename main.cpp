#include "server.h"

int main()
{
  MyServer::Server server {};
  server.go();
  return 0;
}
