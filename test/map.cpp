#include <cassert>
#include <string>
#include "utils/concurrentMap.h"
#include "utils/json.h"

using namespace MyServer::Utils::JSON;
//I had insert and insert_or_assign confused
int main() {
  using Todo = JSON<Pair<"description", std::string>, Pair<"done", bool>, Pair<"due", Nullable<std::string>>>;
  MyServer::Utils::ConcurrentMap<std::string, Todo, JSON<MapOf<Todo>>> todoDatabase {};

  std::string_view todo1_str {R"({"description": "myFirstTodo", "done": false})"};
  Todo todo1 {todo1_str};

  todoDatabase.insert_or_assign("a", todo1);
  assert(*todoDatabase.get("a") == todo1);

  std::string_view todo2_str {R"({"description": "mySecondTodo", "done": false})"};
  Todo todo2 {todo2_str};

  assert (todo1 != todo2);
  todoDatabase.insert_or_assign("a", todo2);
  assert(*todoDatabase.get("a") == todo2);

  std::string_view todo3_str {R"({"description": "myThirdTodo", "done": true})"};
  Todo todo3 {todo3_str};

  todoDatabase.insert_or_assign("b", todo3);
  assert(*todoDatabase.get("a") == todo2);
  assert(*todoDatabase.get("b") == todo3);
}
