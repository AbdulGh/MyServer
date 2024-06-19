# Overview
This is a basic multithreaded C++ HTTP server. It serves any function that takes a `Request` object (consisting of parsed query parameters and a request body) and that returns a string.

There is a always warm nonblocking main thread, which accepts clients, passing the sockets to some fixed number of always warm and nonblocking dispatch theads.
These threads parse the requests, and then make use of an adjustable thread pool of dedicated worker threads to compute the response, which is then returned by the dispatch threads directly to the clients.

## Technical overview
### The main thread
Despite the name, this thread does the least - it `accept`s clients, and in a round-robin fashion, communicates them to the chosen dispatch thread via a concurrent queue.
The main thread notifies the chosen dispatch thread that there is work to do, in case it was waiting for the queue to become populated. And these dispatch threads handle the client from here on out.

### The dispatch thread
On construction the dispatch thread creates an epoll instance. At each client socket handover we make a `Client` keeping the file descriptor, an input buffer, an output buffer, and some basic atomic reference counter (a client may be considered by multiple worker threads, for example). A `std::shared_ptr` could also work instead of manual ref counting.

The epoll operates in edge triggered mode. For our application, level triggered probably makes more sense (we have a separate epfd per relevant thread) but edge triggered is better practice.

The 'event loop' of the dispatch thread is as follows:
1. Check the `incomingClientQueue`. If you have no clients, we wait here.
2. Check the read/write ready map (pointing from fds to `Client`s, initially empty). 
    1. In the read case, we read until we would block, or until we hit some fixed `CHUNKSIZE`. As mentioned in the manpages, we can't just read until `EAGAIN | EWOULDBLOCK` - a fast or adversarial client could starve other clients. This isn't considered in a lot of online examples. If we would block, we remove the `Client` from the queue. We read by feeding the received bytes into the state machine `RequestParser`, which then exposes a list of parsed `Requests`, to be routed to the appropriate handler.
    2. The `Client`s write queue is a queue of `std::string`s and a count of how many bytes we have sent so far from the front of the queue. We just write up until at most the `CHUNKSIZE`. All of this unless the queue is locked, in which case, a worker is inserting their computed result - we leave the client in the round robin and move on.
3. Check the epoll, updating the client map as appropriate.

Here's how dispatching works. 
We maintain a count of the number of worker threads, stored in an array up to some fixed size (we will use this array to close any running worker threads when the server shuts down). 
If we are below some fixed limit of worker threads, we just spawn and detatch a new worker thread. Otherwise, we push the task onto a task queue. Unlike the client queue, this task queue has multiple producers (the dispatch threads) and possibly multiple consumers (the worker threads). 

### The worker threads
Each worker thread in progress has a `Task`, containing the `Request`, a `std::function<std::string(RequestContent)>` (being the registered handler), and a reference to the clients outgoing queue where we will produce our result. When it has passed its result, it checks the task queues for any more work - if there is none, it exits and scales down the thread pool.
