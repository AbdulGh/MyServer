#include <string_view>

#include "parseHTTP.h"
#include "logger.h"

namespace MyServer {
namespace HTTP {

RequestParser::RequestParser(): state{State::PARSE_METHOD} {};
void RequestParser::reset() {
  state = State::PARSE_METHOD;
  currentRequest = {};
  buffer = ""; auxbuffer = "";
  count = 0;
}

template <RequestParser::State s>
void RequestParser::processHelper(RequestParser*, std::string_view) {
  Logger::log<Logger::LogLevel::FATAL>("RequestParser entered some weird state");
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_METHOD>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ') self->buffer += input[head++];
  if (head < input.size() && input[head] == ' ') {
    if (self->buffer == "GET") self->currentRequest.method = Request::Method::GET;
    else if (self->buffer == "POST") self->currentRequest.method = Request::Method::POST;
    else {
      Logger::log<Logger::LogLevel::ERROR>("Parsed this unsupported method, restarting self->state machine: " + self->buffer);
      self->reset();
    }
    self->buffer = "";
    self->state = RequestParser::State::PARSE_ENDPOINT;
    self->process(input.substr(head + 1));
  }
}


template <>
void RequestParser::processHelper<RequestParser::State::PARSE_ENDPOINT>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '?') self->buffer += input[head++];

  if (head < input.size()) {
    if (input[head] == ' ') {
      self->currentRequest.endpoint = std::exchange(self->buffer, "");
      self->state = RequestParser::State::FIND_HEADERS;
      self->process(input.substr(head + 1));
    }
    else if (input[head] == '?') {
      self->currentRequest.endpoint = std::exchange(self->buffer, "");
      self->state = RequestParser::State::PARSE_QUERY_KEY;
      self->process(input.substr(head + 1));
    }
    // else we just continue in this self->state
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::FIND_HEADERS>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size()) {
    if (self->count == 2 && input[head] != '\r') break;
    else if (self->count == 4) {
      //no (more) headers
      self->state = RequestParser::State::PARSE_BODY;
      self->count = 0;
    }
    else if (self->count & 1 && input[head] != '\n') {
      Logger::log<Logger::LogLevel::ERROR>("Malformed newline in client request, resetting self->state machine");
      self->reset();
    }
    self->count += (input[head] == RequestParser::httpnewline[self->count & 1]);
    ++head;
  }
  if (self->count == 2) {
    self->count = 0;
    self->state = RequestParser::State::PARSE_HEADER_KEY;
    self->process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_QUERY_KEY>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '=') self->buffer += input[head++];
  if (head < input.size() && input[head] == '=') {
    self->state = RequestParser::State::PARSE_QUERY_VALUE;
    self->process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_QUERY_VALUE>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '&' && input[head] != ' ') self->auxbuffer += input[head++];
  if (head < input.size()) {
    if (input[head] == '&') {
      //take more queries
      self->currentRequest.query[std::exchange(self->buffer, "")] = std::exchange(self->auxbuffer, "");
      self->state = RequestParser::State::PARSE_QUERY_KEY;
      self->process(input.substr(head + 1));
    }
    else if (input[head] == ' ') {
      self->currentRequest.query[std::exchange(self->buffer, "")] = std::exchange(self->auxbuffer, "");
      self->state = RequestParser::State::FIND_HEADERS;
      self->process(input.substr(head + 1));
    }
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_HEADER_KEY>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ':') self->buffer += input[head++];
  if (head < input.size() && input[head] == ':') {
    self->state = RequestParser::State::PARSE_HEADER_VALUE;
    self->process(input.substr(head + 1));
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_HEADER_VALUE>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size() && input[head] != ' ' && input[head] != '&' && input[head] != ' ') self->auxbuffer += input[head++];
  if (head < input.size()) {
    if (input[head] == '&') {
      //take more queries
      self->currentRequest.query[std::exchange(self->buffer, "")] = std::exchange(self->auxbuffer, "");
      self->state = RequestParser::State::PARSE_QUERY_KEY;
      self->process(input.substr(head + 1));
    }
    else if (input[head] == ' ') {
      self->currentRequest.query[std::exchange(self->buffer, "")] = std::exchange(self->auxbuffer, "");
      self->state = RequestParser::State::FIND_HEADERS;
      self->process(input.substr(head + 1));
    }
  }
}

template <>
void RequestParser::processHelper<RequestParser::State::FIND_BODY>(RequestParser* self, std::string_view input) {
  //if there is no Content-Length, we expect no body, and we entered this self->state after seeing the \r\n\r\n - we are done!
  auto it = self->currentRequest.headers.find("Content-Length");
  if (it == self->currentRequest.headers.end() || it->second == "0") {
    self->parsedRequests.push_back(std::move(self->currentRequest));
    self->reset();
  }
  else {
    //we don't bother strictly enforcing the content length, it's mostly useful to disambiguate the http 'grammar'
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
      Logger::log<Logger::LogLevel::ERROR>("Client sent non-numeric Content-Length, ignoring: " + it->second);
    }
    else {
      self->buffer.reserve(contentLength + 4);
    }
    self->state = RequestParser::State::PARSE_BODY;
  }
  self->process(input);
}

template <>
void RequestParser::processHelper<RequestParser::State::PARSE_BODY>(RequestParser* self, std::string_view input) {
  int head = 0;
  while (head < input.size()) {
    if (input[head] != RequestParser::httpnewline[self->count & 1]) {
      for (int i = 0; i < self->count; ++i) {
        self->buffer += RequestParser::httpnewline[i & 1];
      }
      self->count = 0;
    }
    else ++self->count;

    if (self->count == 4) {
      Logger::log<Logger::LogLevel::INFO>("Parsed a HTTP request");
      self->currentRequest.body = std::move(self->buffer);
      self->parsedRequests.push_back(std::move(self->currentRequest));
      self->reset();
    }
    self->process(input.substr(head + 1));
  }
}

// I wanted to do this with a for loop but it doesn't compile...
template <> 
consteval void RequestParser::instantiateAction<-1>(StateActions&) {
  return;
}
template <int i> 
consteval void RequestParser::instantiateAction(StateActions& actions) {
  actions[i] = &RequestParser::processHelper<static_cast<RequestParser::State>(i)>;
  instantiateAction<i - 1>(actions);
}
consteval RequestParser::StateActions RequestParser::generateActions() {
  StateActions actions {};
  instantiateAction<std::to_underlying(RequestParser::State::NUM_STATES) - 1>(actions);
  return actions;
}

void RequestParser::process(std::string_view input) {
  static constexpr StateActions actions = RequestParser::generateActions();
  return actions[std::to_underlying(state)](this, input);
}

std::vector<Request> RequestParser::takeRequests() {
  return std::exchange(parsedRequests, {});
}

}
}
