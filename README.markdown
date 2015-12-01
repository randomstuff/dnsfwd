# dnsfwd

DNS forwarder over a (TCP) virtual circuit:

* open UDP sockets and wait for incoming queries;

* forwards the queries to a given TCP endpoint using a persistent TCP connection
  (instead of using one TCP connection per request).

This can be used to use DNS/TLS. Unbound can delegate requests to a remote
DNS/TLS server but each request uses a new TCP connection which is not very
efficient.

## DNS/TLS setup

### Basic setup

~~~
                                                 .------------------.
.---------------.              .---------.       | Remote           |
| Stub resolver |------------->| stunnel |------>| recursive server |
'---------------' DNS/TCP 53   '---------'       '------------------'
        |                           ^ DNS/TLS/TCP 443
        |                           |
        |         .---------.       |
        '-------->| dnsfwd  |-------'
       DNS/UDP 53 '---------' DNS/TCP 53
~~~

This is an overview of how this can be used to implement communicate with a
remote recursive DNS server over TLS:

1. Find a remote DNS recursive server working over TLS.

   Unbound can serve natively over TLS. Otherwise, a stunnel instance can be
   used on the remote side to terminate the TLS encapsulation.

   The port TCP 443 is a good choice for this service as it will usually be open
   on most networks and will look like a normal HTTP/TLS traffic.

2. Setup a local stunnel instance listening on TCP 53 and initiating the TLS
   encapsulation with the remote server.

   This provides a local DNS/TCP service forwarding the requests over TLS to the
   remote recursive server: each TCP connection to the local service will create
   a new TLS/TCP connection to the remote server.

3. Setup a local dnsfwd instance listening on UDP 53 and connecting to the
   local stunnel instance.

   This provides a local DNS/UDP service forwarding the requests over TLS to the
   remote recursive server: the requests are multiplexes over a persistent TCP
   connection.

4. Point your stub resolver to this remote service:

   * it will send queries to the local DNS/UDP dnsfwd server;

   * if the query is truncated, it will send the query to the local DNS/TCP
     stunnel server.

### Possible evolutions

* native TLS support

  A stunnel instance would not be necessary. However, by using a separate TLS
  encapsulation daemon, the user can choose a suitable DNS implementation. The
  native TLS implementation would probably be tied to a given TLS
  implementation.

* DNS/TCP support on the server-side

  Each local DNS/TCP connection creates a DNS/TLS connection to the remote
  server. By implementing server-side DNS/TCP support in dnsfwd, we could
  multiplex the requests made over local DNS/TCP over persistent TCP connections
  as it is currently done with local DNS/UDP.

* multiplexing over multiple TCP (or TLS) connections with the remote server

### Advanced setup

For better performance, a local caching DNS server can be used between the stub
resolver and stunnel+dnsfwd:

1. move stunnel and dnsfwd to another port;

2. setup a local caching DNS server forwarding all requests to the local
   stunnel+dnsfwd pair (unbound can do this).

## TODO

* connect to UNIX socket;
* load balancing on multiple servers;
* truncation of the answer (EDNS0, MTU);
* logging (syslog, stderr logging);
* check the QR bit;
* forget old messages;
* limit the number of requests in queue;
* limit the number of submitted requests;
* mux the requests over multiple VC;
* native TLS VC;
* PF_INET support (?);
* async_connect.
