#ifndef PARSEHTTP_H
#define PARSEHTTP_H

#include <string_view>
#include <utility>

#include "server/common.h"

namespace MyServer {
namespace HTTP {

class RequestParser {
private:
  enum class State {
    PARSE_METHOD, PARSE_ENDPOINT, PARSE_QUERY_KEY, PARSE_QUERY_VALUE,
    FIND_HEADERS, PARSE_HEADER_KEY, PARSE_HEADER_VALUE, FIND_BODY,
    PARSE_BODY, NUM_STATES
  };

  //this stuff is because I wanted the compiler to generate the state jump table w/ a sort of pattern matching
  //more for fun than for good software engineering
  using Action = void (RequestParser::*)(std::string_view);
  using StateActions = std::array<Action, std::to_underlying(State::NUM_STATES)>;
  template <int i> static consteval void instantiateActions(StateActions& actions);
  static consteval StateActions generateActions(); 
  template <State state> void processHelper(std::string_view input);
  static constexpr Action jumpToAction(State state);

  State state;
  std::vector<Request> parsedRequests {};
  Request currentRequest {};
  std::string buffer {};
  std::string auxbuffer {}; // used for the v of kv 
  int count { 0 }; //used to count \r\n, etc 
  bool error { false };
  bool fresh { true };

  static constexpr std::string_view httpnewline { "\r\n" };

  void commitAndContinue(std::string_view);

public:
  RequestParser();
  void process(std::string_view input);
  bool isError() const;
  bool isFresh() const;
  void reset();
  std::vector<Request> takeRequests();
};

}

}

#endif
