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

#ifndef DNSFWD_DNSFWD_HPP
#define DNSFWD_DNSFWD_HPP

#include <syslog.h>

#include <arpa/inet.h>

#include <cstdint>

#include <memory>
#include <vector>
#include <array>
#include <string>
#include <chrono>

#include <boost/bind.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include <boost/system/error_code.hpp>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/set_hook.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/generic/datagram_protocol.hpp>

#define EMERG 0
#define ALERT 0
#define ALERT 0

#define LOG(k) if(::dnsfwd::loglevel >= LOG_ ## k)\
  std::cerr << ::dnsfwd::logformat[(LOG_ ## k)]

namespace dnsfwd {

struct endpoint;
class message;
class server;
class client;
class service;

struct order_message_by_server_id;
struct order_message_by_client_id;

const size_t MIN_MESSAGE_SIZE = 12;
extern int loglevel;
extern const char** logformat;

struct endpoint {
  std::string name;
  std::string port;

  boost::asio::ip::udp::endpoint udp_endpoint(
    boost::asio::io_service& service, const char* default_port) const;
};

struct config {
  std::vector<std::string> args;
  std::vector<endpoint> bind_udp;
  std::vector<endpoint> connect_tcp;
  int listen_fds;
};

void setup_config(dnsfwd::config& config, int argc, char** argv);

class message {
public:
  message() : buffer_(1024)
  {
  }
  message(message &) = delete;
  message& operator=(message &) = delete;

  std::vector<char> buffer_;
  std::uint16_t size_;
  std::uint16_t network_size_;
  std::uint16_t client_id_;
  std::uint16_t server_id_;
  std::chrono::steady_clock::time_point timestamp_;
  boost::asio::generic::datagram_protocol::endpoint endpoint_;
  boost::intrusive::set_member_hook<> by_client_id_hook_;
  boost::intrusive::list_member_hook<> queue_hook_;

  std::array<boost::asio::const_buffer, 2> vc_buffer()
  {
    network_size_ = htons(size_);
    return {
      boost::asio::buffer( &network_size_, sizeof(network_size_) ),
      boost::asio::buffer( buffer_.data(), size_ )
    };
  }
  bool operator==(message const& that) const {
    return this==&that;
  }
};

struct order_message_by_server_id {
  bool operator()(message const& a, message const& b) const
  {
    return a.server_id_ < b.server_id_;
  }
  bool operator()(message const& a, std::uint16_t id) const
  {
    return a.server_id_ < id;
  }
  bool operator()(std::uint16_t id, message const& b) const
  {
    return id < b.server_id_;
  }
};
struct order_message_by_client_id {
  bool operator()(message const& a, message const& b) const
  {
    return a.client_id_ < b.client_id_;
  }
  bool operator()(message const& a, std::uint16_t id) const
  {
    return a.client_id_ < id;
  }
  bool operator()(std::uint16_t id, message const& b) const
  {
    return id < b.client_id_;
  }
};

class server {
public:
  server(boost::asio::io_service& io_service, service& service,
    dnsfwd::endpoint const& udp_endpoint);
  server(boost::asio::io_service& io_service, service& service, int socket);
public:
  void send_response(
    std::vector<char> response,
    boost::asio::generic::datagram_protocol::endpoint endpoint);
private:
  void start_receive();
  void on_message(const boost::system::error_code& error, std::size_t size);
  void response_sent(std::vector<char>& response,
    const boost::system::error_code& error, std::size_t size);
private:
  service* service_;
  boost::asio::generic::datagram_protocol::socket socket_;
  std::unique_ptr<message> context_;
};

class client
  : public std::enable_shared_from_this<client> {
public:
  client(boost::asio::io_service& io_service, service& service);
  ~client();
  bool add_request(std::unique_ptr<message>& context);
  std::uint16_t random_client_id();
  void connect();
private:
  void start_receive();
  void on_message_size(const boost::system::error_code& error, std::size_t size);
  void on_message(const boost::system::error_code& error, std::size_t size);
  void reset();
  void send();
  void on_send(const boost::system::error_code& error, std::size_t bytes_transferred);
  void clear(std::chrono::steady_clock::time_point time);
private:
  typedef boost::intrusive::member_hook<
    message,
    boost::intrusive::set_member_hook<>,
    &message::by_client_id_hook_
  > ByClientIdOptions;
  typedef boost::intrusive::set<
    message,
    ByClientIdOptions,
    boost::intrusive::compare<order_message_by_client_id>
  > by_client_id_type;

  typedef boost::intrusive::member_hook<
    message,
    boost::intrusive::list_member_hook<>,
    &message::queue_hook_
  > QueueOptions;
  typedef boost::intrusive::list<
    message, QueueOptions, boost::intrusive::cache_last<true>
  > queue_type;

  boost::asio::io_service* io_service_;
  service* service_;
  boost::asio::ip::tcp::socket socket_;
  by_client_id_type by_client_id_;
  queue_type queue_;
  std::unique_ptr<message> context_;

  std::uint16_t response_size_;
  std::vector<char> buffer_;
};

class service {
public:
  service(boost::asio::io_service& io_service, dnsfwd::config config);
  void add_request(std::unique_ptr<message>& context);
  void send_response(
    std::vector<char> response,
    boost::asio::generic::datagram_protocol::endpoint endpoint)
  {
    if (config_.connect_tcp.size() == 0) {
      LOG(ERR) << "no remote endpoint\n";
      std::exit(1);
    }
    server_->send_response(std::move(response), endpoint);
  }
  std::uint16_t random_id()
  {
    return (std::uint16_t) random_();
  }
  // TODO, remote this
  std::vector<endpoint> const& tcp_connect_endpoints()
  {
    return config_.connect_tcp;
  }
  std::vector<endpoint> const& udp_listen_endpoints()
  {
    return config_.bind_udp;
  }
  std::unique_ptr<message> unqueue();
  void unregister(std::shared_ptr<client> client);
  std::chrono::seconds time_to_live() const
  {
    return std::chrono::seconds(60);
  }
private:

  typedef boost::intrusive::member_hook<
    message,
    boost::intrusive::list_member_hook<>,
    &message::queue_hook_
  > QueueOptions;
  typedef boost::intrusive::list<
    message, QueueOptions, boost::intrusive::cache_last<true>
  > queue_type;

  boost::asio::io_service* io_service_;
  dnsfwd::config config_;
  std::unique_ptr<server> server_;
  std::shared_ptr<client> client_;
  boost::random::mt11213b random_;
  queue_type queue_;
};

}

#endif
