# HTTP-proxy
Usage:
* compile: make
* run: ./httpproxy <port_number> or ./parallel_httpproxy <port_number>

Simple test:
Run the proxy in a terminal, and then from another terminal use telnet:

telnet localhost <PORT>
  
<port> Trying 127.0.0.1... Connected to localhost.localdomain
  
(127.0.0.1). Escape character is ’ˆ]’.

GET http://www.google.com/ HTTP/1.0
  
For more complex tests, it is required to configure the browser to use the proxy.
