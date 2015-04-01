# dnsfwd

DNS forwarder over a (TCP) virtual circuit.

Roadmap:

* load balancing on multiple servers;
* truncation of the answer (EDNS0, MTU);
* logging (syslog, stderr logging);
* check the QR bit;
* decent CLI options;
* forget old messages;
* limit the number of requests in queue;
* limit the number of submitted requests;
* multiple server-side sockets;
* mux the requests over multiple VC;
* native TLS VC;
* PF_INET support (?);
* async_connect.
