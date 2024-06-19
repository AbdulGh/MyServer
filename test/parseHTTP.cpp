#include "parseHTTP.h"
#include <random>
#include <string_view>
#include <cassert>
#include <iostream>

using namespace MyServer;

std::vector<std::string> requestDiff(const Request& a, const Request& b) {
  std::vector<std::string> diffs {};
  if (a.method != b.method) diffs.push_back("Different methods");
  if (a.body != b.body) diffs.push_back("Different bodies:\n\n" + a.body + "\n\n" + b.body);
  if (a.endpoint != b.endpoint) diffs.push_back("Different endpoints:\n\n" + a.endpoint + "\n\n" + b.endpoint);

  if (a.query.size() != b.query.size()) diffs.push_back("Different number of queries");
  for (const auto& [k, v]: a.query) {
    auto it = b.query.find(k);
    if (it == b.query.end()) diffs.push_back("Query key " + k + " not found in RHS");
    else if (it->second != v) diffs.push_back("Query key " + k + " has different values: " + v + " vs " + it->second);
  }
  for (const auto& [k, v]: b.query) {
    auto it = a.query.find(k);
    if (it == a.query.end()) diffs.push_back("Query key " + k + " not found in LHS");
  }

  if (a.headers.size() != b.headers.size()) diffs.push_back("Different number of headers");
  for (const auto& [k, v]: a.headers) {
    auto it = b.headers.find(k);
    if (it == b.headers.end()) diffs.push_back("Key " + k + " not found in RHS");
    else if (it->second != v) diffs.push_back("Key " + k + " has different values: " + v + " vs " + it->second);
  }
  for (const auto& [k, v]: b.headers) {
    auto it = a.headers.find(k);
    if (it == a.headers.end()) diffs.push_back("Header key " + k + " not found in LHS");
  }
  return diffs;
}

void assertRequestEquality(const Request& a, const Request& b) {
  std::vector<std::string> diffs = requestDiff(a, b);
  if (!diffs.empty()) {
    std::cerr << "Found unexpected differences:\n";
    for (const std::string& diff: diffs) std::cerr << diff << std::endl;
    exit(1);
  }
}

int main() 
{
  constexpr std::string_view getRequest 
    = "GET /?key1=valueA&key2=valueB HTTP/1.1\r\nHost: example.com    \r\nConnection: close\t\r\nContent-Length: 0\r\n\r\n";

  HTTP::RequestParser requestParser {};
  requestParser.process(getRequest);
  std::vector<Request> requests = requestParser.takeRequests();
  assert(requests.size() == 1);
  assert(requestParser.takeRequests().size() == 0);

  Request parsed = requests.front();
  assert(parsed.method == Request::Method::GET);
  assert(parsed.query.size() == 2);
  assert(parsed.endpoint == "/");
  assert(parsed.query.find("key1") != parsed.query.end());
  assert(parsed.query.at("key1") == "valueA");
  assert(parsed.query.find("key2") != parsed.query.end());
  assert(parsed.query.at("key2") == "valueB");
  assert(parsed.headers.size() == 3);
  assert(parsed.headers.find("Host") != parsed.headers.end());
  assert(parsed.headers.at("Host") == "example.com");
  assert(parsed.headers.find("Connection") != parsed.headers.end());
  assert(parsed.headers.at("Connection") == "close");
  assert(parsed.headers.find("Content-Length") != parsed.headers.end());
  assert(parsed.headers.at("Content-Length") == "0");
  assert(parsed.body == "");

  //tests are random but repeatable
  std::mt19937 gen {0};
  std::geometric_distribution<size_t> flips(0.2);

  int head = 0;
  while (head < getRequest.size()) {
    int length = std::min(getRequest.size() - head, flips(gen));
    requestParser.process(getRequest.substr(head, length));
    head += length;
  }

  requests = requestParser.takeRequests();
  assert(requests.size() == 1);
  assertRequestEquality(requests.front(), parsed);

  constexpr std::string_view twoRequests
    = "POST /submit?user_id=12345 HTTP/1.1\r\nHost: api.example.com\r\nContent-Length: 50\r\n\r\nabdul@laptop:~$ fortune\r\nYou are as I am with You.GET /?key1=valueA&key2=valueB HTTP/1.1\r\nHost: example.com    \r\nConnection: close\t\r\nContent-Length: 0\r\n\r\nPOST /submit?user_id=12345 HTTP/1.1\r\nHost: api.example.com\r\nContent-Length: 50\r\n\r\nabdul@laptop:~$ fortune\r\nYou are as I am with You.";

  //generate a bunch of splits
  for (int i = 0; i < 2048; ++i) {
    head = 0;

    while (head < twoRequests.size()) {
      int length = std::min(twoRequests.size() - head, flips(gen));
      requestParser.process(twoRequests.substr(head, length));
      head += length;
    }

    requests = requestParser.takeRequests();
    assert(requests.size() == 3);
    Request& postRequest = requests.front();
    assert(postRequest.method == Request::Method::POST);
    assert(postRequest.query.size() == 1);
    assert(postRequest.endpoint == "/submit");
    assert(postRequest.query.size() == 1);
    assert(postRequest.query.find("user_id") != postRequest.query.end());
    assert(postRequest.query.at("user_id") == "12345");
    assert(postRequest.headers.size() == 2);
    assert(postRequest.headers.find("Host") != postRequest.headers.end());
    assert(postRequest.headers.at("Host") == "api.example.com");
    assert(postRequest.headers.find("Content-Length") != postRequest.headers.end());
    assert(postRequest.headers.at("Content-Length") == "50");
    assert(postRequest.body == "abdul@laptop:~$ fortune\r\nYou are as I am with You.");

    assertRequestEquality(requests[1], parsed);
    assertRequestEquality(requests.front(), requests.back());
  }

  //todo error handling checks
  return 0;
}
