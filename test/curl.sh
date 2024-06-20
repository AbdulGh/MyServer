#!/bin/bash

url="http://localhost:8675/echo"

# Very basic test
testNum() {
  local num=$1
  local expected="I'm client number $num"
  local response=$(curl -s -d "$expected" -X POST $url)
  
  if [ "$response" != "Server replies: $expected" ]; then
    echo "expected $expected, found $response"
    exit 1
  fi
}

export -f testNum
export url

seq 1 20 | xargs -n1 -P20 -I{} bash -c 'testNum "{}"'
