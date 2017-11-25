/* The MIT License (MIT)

Copyright (c) 2015 Gabriel Corona

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "dnsfwd.hpp"

#include <utility>
#include <memory>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>

#include <boost/bind.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/placeholders.hpp>

namespace dnsfwd {

namespace {

boost::asio::generic::datagram_protocol datagram_protocol_from_socket(int fd)
{
  int domain, protocol;
  socklen_t len = sizeof(domain);

#if HAVE_SO_DOMAIN
  if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &len) != 0) {
    LOG(CRIT) << "Could not get socket domain.\n";
    std::exit(1);
  }
#else
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  if (getsockname(fd, (struct sockaddr*) &addr, &addrlen) != 0) {
    LOG(CRIT) << "Could not get socket name.\n";
    std::exit(1);
  }
  // PF_ and AF_ are the same on BSD, Linux, POSIX:
  domain = addr.ss_family;
#endif

#if HAVE_SO_TYPE
  int type;
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) {
    LOG(CRIT) << "Could not get socket type.\n";
    std::exit(1);
  }
  if (type != SOCK_DGRAM) {
    LOG(CRIT) << "Not a datagram protocol\n";
    std::exit(1);
  }
#endif

  if (getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &len) != 0) {
    LOG(CRIT) << "Could not get socket protocol.\n";
    std::exit(1);
  }

  return boost::asio::generic::datagram_protocol(domain, protocol);
}

}

server::server(boost::asio::io_service& io_service, service& service, int socket)
  : service_(&service),
    socket_(io_service, datagram_protocol_from_socket(socket), socket),
    context_(nullptr)
{
  start_receive();
}

server::server(boost::asio::io_service& io_service, service& service,
    dnsfwd::endpoint const& udp_endpoint)
  : service_(&service),
    socket_(io_service,
      boost::asio::generic::datagram_protocol::endpoint(
        udp_endpoint.udp_endpoint(io_service, "domain")
      )
    ),
    context_(nullptr)
{
  start_receive();
}

void server::start_receive()
{
  if (!context_)
    context_ = std::unique_ptr<message>(new message());
  socket_.async_receive_from(
    boost::asio::buffer(
      context_->buffer_.data(),
      context_->buffer_.size()),
    context_->endpoint_,
    boost::bind(
      &server::on_message,
      this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred)
  );
}

void server::on_message(const boost::system::error_code& error, std::size_t size)
{
  if (error) {
    LOG(ERR) << "Request reception error: " << error << '\n';
  } else if (size < MIN_MESSAGE_SIZE) {
    LOG(DEBUG) << "Request is too small (" << size << " bytes)\n";
  } else {
    LOG(DEBUG) << "Request received\n";
    context_->size_ = size;
    context_->server_ = this;
    service_->add_request(context_);
  }
  start_receive();
}

void server::send_response(
  std::vector<char> response,
  boost::asio::generic::datagram_protocol::endpoint endpoint)
{
  auto buffer = boost::asio::buffer(response.data(), response.size());
  socket_.async_send_to(
    buffer,
    endpoint,
    boost::bind(
      &server::response_sent,
      this,
      std::move(response),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred
    )
  );
}

void server::response_sent(std::vector<char>& response,
  const boost::system::error_code& error, std::size_t size)
{
  if (error) {
    LOG(ERR) << "Error forwarding response\n";
  } else if (size != response.size()) {
    LOG(ERR) << "Response forward incomplete " << size << " " << response.size() << '\n';
  } else {
    LOG(DEBUG) << "Response sent\n";
  }
}

}
