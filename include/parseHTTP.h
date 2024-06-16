#ifndef PARSEHTTP_H
#define PARSEHTTP_H

#include <string_view>
#include <utility>

#include "common.h"

namespace MyServer {
namespace HTTP {

struct Request {
  enum class Method { GET, POST, NUM_METHODS };
  Method method;
  std::string endpoint;
  Query query;
  Query headers;
  std::string body;
};

class RequestParser {
private:
  enum class State {
    PARSE_METHOD, PARSE_ENDPOINT, PARSE_QUERY_KEY, PARSE_QUERY_VALUE,
    FIND_HEADERS, PARSE_HEADER_KEY, PARSE_HEADER_VALUE, FIND_BODY,
    PARSE_BODY, NUM_STATES
  };

  //this stuff is because I wanted the compiler to generate the switch(State) jump table... more fun than good software engineering
  using StateActions = std::array<void(*)(RequestParser*, std::string_view), std::to_underlying(State::NUM_STATES)>;
  template <int i> static consteval void instantiateAction(StateActions& actions);
  static consteval StateActions generateActions(); 
  template <State state> static void processHelper(RequestParser* self, std::string_view input);

  State state;
  std::vector<Request> parsedRequests {};
  Request currentRequest {};
  std::string buffer {};
  std::string auxbuffer {}; // used for the v of kv 
  int count { 0 }; //used to count \r\n, etc 

  static constexpr std::string_view httpnewline { "\r\n" };
  void reset();

public:
  RequestParser();
  void process(std::string_view input);
  std::vector<Request> takeRequests();
};

}

}

#endif
