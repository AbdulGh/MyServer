#include <cassert>
#include <cmath>
#include <string_view>

#include "utils/json.h"

using namespace MyServer::Utils::JSON;
using namespace std::string_view_literals;

int main() {
  using Todo = JSON<Pair<"description", std::string>, Pair<"done", bool>, Pair<"due", Nullable<std::string>>>;
  std::string_view todo {R"({"description": "myDescription", "done": true, "due": null})"};
  Todo todoParsed { todo };

  using FlatObject = JSON<
    Pair<"key1", std::string>,
    Pair<"key2", double>,
    Pair<"key3", bool>
  >;

  FlatObject test1 {};
  // test1.get<"key1">() = "hello";
  test1[Key<"key1">{}] = "hello";
  test1.get<"key2">() = 2.17;
  test1.get<"key3">() = false;

  assert(test1.get<"key1">()->contents == "hello");
  assert(test1.get<"key2">()->contents == 2.17);
  assert(test1.get<"key3">()->contents == false);

  std::string_view jsonString1 = "    \"blah blah blah\", \"next string in array\"]";
  std::string_view jsonString2 = "\"\"";

  JSON<std::string> parsed1 {jsonString1};
  assert(parsed1.contents == "blah blah blah");
  assert(jsonString1 == ", \"next string in array\"]");

  JSON<std::string> parsed2 {jsonString2};
  assert(parsed2.contents == "");
  assert(jsonString2 == "");

  std::string_view doubleString = "    123.456\n \"next key in object\": null}";
  JSON<double> parsedDouble {doubleString};
  assert(parsedDouble.contents == 123.456);
  assert(doubleString == "\n \"next key in object\": null}");

  //this type of thing will be tested more throroughly when we start testing arrays and objects anyway
  std::string_view boolString = "     true  \n   false, and onwards";
  JSON<bool> bool1 {boolString};
  JSON<bool> bool2 {boolString};
  assert(bool1.contents); assert(!bool2.contents); assert(boolString == ", and onwards");

  std::string_view raggedJSON = "[[1.23, 4.56], [7.89, 10.11, 12.13]]x";
  JSON<ListOf<ListOf<double>>> ragged {raggedJSON};
  assert(ragged[0][0] == 1.23);
  assert(fabs(ragged[1][2].contents - 12.13) == 0);
  assert(raggedJSON == "x");

  std::string_view flatjson {R"({"key1": "world", "key3": true, "key2": -3.14})"};
  FlatObject test2 {flatjson};

  assert(test2.get<"key1">()->contents == "world");
  assert(test2.get<"key2">()->contents == -3.14);
  assert(test2.get<"key3">()->contents == true);

  using User = JSON<Pair<"name", std::string>, Pair<"age", double>>;
  using UserList = JSON<ListOf<User>>;

  std::string_view userList {R"([
    {"name": "toddler", "age": 2},
    {"name": "baby", "age": 1}
  ])"};
  UserList users {userList};
  assert(users[0].get<"name">()->contents == "toddler");
  assert(users[1].get<"age">()->contents == 1.0);

  using UserGroup = JSON<Pair<"exclusive", Nullable<bool>>, Pair<"users", UserList>>;

  std::string_view finalBoss {R"(
  [
    {
      "exclusive": false,
      "users": [
        {"name": "toddler", "age": 2},
        {"name": "baby", "age": 1}
      ]
    },
    {
      "exclusive": true,
      "users": [
        {"name": "user1", "age": 30},
        {"name": "user2", "age": 25}
      ]
    },
    {
      "exclusive": null,
      "users": []
    }
  ]
  )"};
  JSON<ListOf<UserGroup>> bigTest {finalBoss};

  assert(!((*bigTest[0].get<"exclusive">())->contents));
  assert((*bigTest[1].get<"users">())[0].get<"name">() == "user1");
  assert((*bigTest[1].get<"users">())[1].get<"age">() == 25);

  assert(bigTest[2].get<"exclusive">());
  
  try {
    //todo think of better api
    **bigTest[2].get<"exclusive">();
    assert(false && "bad variant access should throw");
  }
  catch (std::bad_variant_access) {}

  assert(bigTest[2].get<"users">()->contents.size() == 0);

  std::string_view userMap { R"({"asdf": {"name": "python fan", "age": 27}, "requirements": {"name": "anonymous", "age": -1}})" };
  JSON<MapOf<User>> result {userMap};

  assert(result.size() == 2);
  assert(*result["asdf"].get<"name">() == "python fan");
  assert(*result["requirements"].get<"age">() == -1.0);
  
  return 0;
}
