#include "parseHTTP.h"
#include <string_view>
#include <cassert>
#include <iostream>

int main() 
{
  constexpr std::string_view getRequest 
    = "GET /?key1=valueA&key2=valueB HTTP/1.1\r\nConnection: close\r\nContent-length: 0\r\n\r\n";

  MyServer::HTTP::RequestParser requestParser {};
  requestParser.process(getRequest);
  std::vector<MyServer::HTTP::Request> requests = requestParser.takeRequests();
  assert(requests.size() == 1);

  MyServer::HTTP::Request& parsed = requests.front();
  assert(parsed.query.size() == 2);
  assert(parsed.query.find("key1") != parsed.query.end());
  assert(parsed.query.at("key1") == "valueA");
  assert(parsed.query.find("key2") != parsed.query.end());
  std::cout << parsed.query.at("key2") << std::endl;
  assert(parsed.query.at("key2") == "valueB");

  // constexpr std::string_view getRequestBad
    // = "GET ?key1=valueA&key2=valueB HTTP/1.1\r\nConnection: close\r\nContent-length: 0\r\n\r\n";

  return 0;
}
