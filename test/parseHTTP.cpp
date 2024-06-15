#include "parseHTTP.h"
#include <string_view>
#include <cassert>
#include <iostream>

int main() 
{
  constexpr std::string_view getRequest 
    = "GET /?key1=valueA&key2=valueB HTTP/1.1\r\nConnection: close\r\nContent-length: 0\r\n\r\n";
  std::optional<Server::Query> parsedOpt = Server::parseRequest(getRequest);
  assert(parsedOpt);

  Server::Query parsed = *parsedOpt;
  assert(parsed.size() == 2);
  assert(parsed.find("key1") != parsed.end());
  assert(parsed.at("key1") == "valueA");
  assert(parsed.find("key2") != parsed.end());
  std::cout << parsed.at("key2") << std::endl;
  assert(parsed.at("key2") == "valueB");

  constexpr std::string_view getRequestBad
    = "GET ?key1=valueA&key2=valueB HTTP/1.1\r\nConnection: close\r\nContent-length: 0\r\n\r\n";

  assert(!Server::parseRequest(getRequestBad));
  assert(!Server::parseRequest(""));

  return 0;
}
