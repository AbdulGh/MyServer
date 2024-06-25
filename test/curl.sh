#!/bin/bash

# Very basic test
testNum() {
  local num=$1
  local expected="I'm client number $num"
  local response=$(curl -s -d "$expected" -X POST http://localhost:8675/echo)
  
  if [ "$response" != "Server replies: $expected" ]; then
    echo "expected $expected, found $response"
    exit 1
  fi
}

export -f testNum
export url

# seq 1 20 | xargs -n1 -P20 -I{} bash -c 'testNum "{}"'

makeTodo() {
  local response=$(curl -s -X PUT -d "{\"description\": \"$1\"}" http://localhost:8675/todo);
  echo $response;
}

makeTodo "todo 1"
makeTodo "todo 2"
makeTodo "todo 3"
makeTodo "todo 4"
makeTodo "todo 5"

