import string
from typing import Optional, Dict
from dataclasses import dataclass
from dataclasses_json import dataclass_json
import random
import aiohttp
import asyncio

url = "http://localhost:8675/todo"
numTodos = 1

def randomString(length: int) -> str:
	return ''.join(random.choices(string.ascii_uppercase + string.digits, k=length))

@dataclass_json
@dataclass
class Todo:
	description: str
	done: bool
	due: Optional[str] = None

@dataclass_json
@dataclass
class TodoResponse:
	id: str
	todo: Todo

class Client:
	__slots__ = ['todoDict', 'session']

	def __init__(self):
		self.todoDict: Dict[str, Todo] = {}
		self.session = None
  
	async def __aenter__(self):
		# self.session = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(sock_read=1, total=2))
		self.session = aiohttp.ClientSession()
		return self

	async def __aexit__(self, *excinfo):
		await self.session.close()

	def getRandomTodo(self):
		return Todo(description = randomString(3), done = random.choice([True, False]), due = random.choice([randomString(2), None]))

	async def createTodo(self):
		todo = self.getRandomTodo()
		request = await self.session.put(url, data=todo.to_json())
		result = TodoResponse.schema().loads(await request.text())
		assert result.todo == todo
		print(f"created todo with id {result.id}")
		self.todoDict[result.id] = result.todo

	async def modifyTodo(self, id):
		print(f"modifying todo with id {id}")
		todo = self.getRandomTodo()
		request = await self.session.put(url, params={"id": id}, data=todo.to_json())
		result = TodoResponse.schema().loads(await request.text())
		assert result.todo == todo
		assert result.id == id
		self.todoDict[result.id] = result.todo

	async def checkTodo(self, id):
		print(f"checking todo with id {id}")
		request = await self.session.get(url, params={"id": id})
		result = Todo.schema().loads(await request.text())
		assert result == self.todoDict[id]
  
	async def eraseTodo(self, id):
		await self.checkTodo(id)
		await self.session.delete(url, params={"id": id})
		result = await self.session.get(url, params={"id": id})
		assert result.status == 404
		self.todoDict.pop(id)
		
	def doSomethingRandom(self, id):
		return random.choice([self.modifyTodo, self.checkTodo, self.eraseTodo])(id)

	async def run(self, numIterations: int):
		for i in range(numIterations):
			print(f"round  {i}")
			await asyncio.gather(*(self.createTodo() for i in range(numTodos - len(self.todoDict))))
			await asyncio.gather(*(self.doSomethingRandom(id) for id in list(self.todoDict.keys())))

if __name__ == "__main__":
	async def main():
		async with Client() as client:
			await client.run(10000)

	asyncio.run(main())
