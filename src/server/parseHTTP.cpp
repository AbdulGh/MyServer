#include <string_view>

#include "server/parseHTTP.h"
#include "utils/logger.h"

namespace MyServer {
namespace HTTP {

//todo the function resolution inside the state definitions could be constexpr

RequestParser::RequestParser(): state{State::PARSE_METHOD} {};
void RequestParser::reset() {
  state = State::PARSE_METHOD;
  currentRequest = {};
  buffer = ""; auxbuffer = "";
  count = 0;
  error = false;
  fresh = true;
}

void RequestParser::commitAndContinue(std::string_view input) {
  Logger::log<Logger::LogLevel::INFO>("Parsed a HTTP request");
  parsedRequests.push_back(std::move(currentRequest));
  reset();
  process(input);
}

bool RequestParser::isError() const {
  return error;
}

bool RequestParser::isFresh() const {
  return fresh;
}

/* State machine goes here */
template <RequestParser::State s>
void RequestParser::processHelper(std::string_view) {
  Logger::log<Logger::LogLevel::FATAL>("RequestParser entered some weird state");
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_METHOD>(std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ') buffer += input[head++];
  if (head < input.size() && input[head] == ' ') {
    if (buffer == "GET") currentRequest.method = Request::Method::GET;
    else if (buffer == "POST") currentRequest.method = Request::Method::POST;
    else if (buffer == "PUT") currentRequest.method = Request::Method::PUT;
    else if (buffer == "DELETE") currentRequest.method = Request::Method::DELETE;
    else {
      Logger::log<Logger::LogLevel::WARN>("Parsed this unsupported method: " + buffer);
      error = true;
    }
    buffer = "";
    state = RequestParser::State::PARSE_ENDPOINT;
    process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_ENDPOINT>(std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '?') buffer += input[head++];

  if (head < input.size()) {
    if (input[head] == ' ') {
      currentRequest.endpoint = std::exchange(buffer, "");
      state = RequestParser::State::FIND_HEADERS;
      process(input.substr(head + 1));
    }
    else if (input[head] == '?') {
      currentRequest.endpoint = std::exchange(buffer, "");
      state = RequestParser::State::PARSE_QUERY_KEY;
      process(input.substr(head + 1));
    }
    // else we just continue in this state
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::FIND_HEADERS>(std::string_view input) {
  int head = 0;
  while (head < input.size()) {
    if (input[head] != RequestParser::httpnewline[count & 1]) {
      if (count == 2) {
        count = 0;
        state = RequestParser::State::PARSE_HEADER_KEY;
        process(input.substr(head));
        return;
      }
      else count = 0;
    }
    else {
      ++count;
      if (count == 4) {
        //no more headers
        count = 0;
        state = RequestParser::State::FIND_BODY;
        process(input.substr(head + 1));
        return;
      }
    }

    ++head;
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_QUERY_KEY>(std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '=') buffer += input[head++];
  if (head < input.size() && input[head] == '=') {
    state = RequestParser::State::PARSE_QUERY_VALUE;
    process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_QUERY_VALUE>(std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '&' && input[head] != ' ') auxbuffer += input[head++];
  if (head < input.size()) {
    if (input[head] == '&') {
      //take more queries
      currentRequest.query[std::exchange(buffer, "")] = std::exchange(auxbuffer, "");
      state = RequestParser::State::PARSE_QUERY_KEY;
      process(input.substr(head + 1));
    }
    else if (input[head] == ' ') {
      currentRequest.query[std::exchange(buffer, "")] = std::exchange(auxbuffer, "");
      state = RequestParser::State::FIND_HEADERS;
      process(input.substr(head + 1));
    }
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_HEADER_KEY>(std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ':') buffer += input[head++];
  if (head < input.size() && input[head] == ':') {
    state = RequestParser::State::PARSE_HEADER_VALUE;
    process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_HEADER_VALUE>(std::string_view input) {
  int head = 0;
  while (head < input.size() && count < 2) {
    //newline symbols cannot be part of the value anyway
    if (input[head] == RequestParser::httpnewline[count]) ++count;
    else {
      auxbuffer += input[head];
      count = 0;
    }
    ++head;
  }

  if (count == 2) {
    //HTTP standard requires we remove trailing whitespace from header values...
    //Would be better to not take the spaces at the front, but would need an extra TAKE_SPACE state
    int l = 0;
    while (l < auxbuffer.size() && std::isspace(auxbuffer[l])) ++l;
    int r = auxbuffer.size() - 1;
    while (r > l && std::isspace(auxbuffer[r])) --r;
    auxbuffer = auxbuffer.substr(l, r - l + 1);
    currentRequest.headers[std::exchange(buffer, "")] = std::exchange(auxbuffer, "");

    //purposefully do not reset count, so FIND_HEADERS knows if it's found the next header or the body
    state = RequestParser::State::FIND_HEADERS;
    process(input.substr(head));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::FIND_BODY>(std::string_view input) {
  //if there is no Content-Length, we expect no body, and we entered this state after seeing the \r\n\r\n - we are done!
  auto it = currentRequest.headers.find("Content-Length");

  if (it == currentRequest.headers.end() || it->second == "0") {
    commitAndContinue(input);
  }
  else {
    long contentLength = 0;
    for (const char& c: it->second) {
      if (!std::isdigit(c)) {
        contentLength = -1;
        break;
      }
      contentLength *= 10;
      contentLength += c - '0';
    }

    if (contentLength == -1) {
      Logger::log<Logger::LogLevel::ERROR>("Client sent non-numeric Content-Length: " + it->second);
      error = true;
    }
    else {
      buffer.reserve(contentLength);
      count = contentLength;
    }
    state = RequestParser::State::PARSE_BODY;
    process(input);
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_BODY>(std::string_view input) {
  int head = 0;
  while (head < input.size() && count-- > 0) buffer += input[head++]; 
  if (count <= 0) {
    currentRequest.body = std::move(buffer);
    commitAndContinue(input.substr(head));
  }
}

// I wanted to do this with a for loop but it doesn't compile...
template <> 
consteval void RequestParser::instantiateActions<-1>(StateActions&) {
  return;
}

template <int i> 
consteval void RequestParser::instantiateActions(StateActions& actions) {
  actions[i] = &RequestParser::processHelper<static_cast<RequestParser::State>(i)>;
  instantiateActions<i - 1>(actions);
}

consteval RequestParser::StateActions RequestParser::generateActions() {
  StateActions actions {};
  instantiateActions<std::to_underlying(RequestParser::State::NUM_STATES) - 1>(actions);
  return actions;
}

void RequestParser::process(std::string_view input) {
  static constexpr StateActions actions = RequestParser::generateActions();
  //todo feels off
  if (input.size() != 0) {
    fresh = false;
  }
  return (this->*actions[std::to_underlying(state)])(input);
}

std::vector<Request> RequestParser::takeRequests() {
  return std::exchange(parsedRequests, {});
}

}
}
