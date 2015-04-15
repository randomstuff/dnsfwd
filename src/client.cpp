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

#include <cstdint>
#include <cstring>

#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/slist_hook.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/set_hook.hpp>

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/connect.hpp>

#include <memory>

namespace dnsfwd {

client::client(boost::asio::io_service& io_service, service& service)
  : io_service_(&io_service), service_(&service), socket_(io_service)
{
  LOG(DEBUG) << "New client\n";
}

client::~client()
{
  by_client_id_.clear();
  queue_.clear_and_dispose(deleter());
  LOG(DEBUG) << "Client deleted\n";
}

void client::clear(std::chrono::steady_clock::time_point time)
{
  size_t count = 0;
  while (!queue_.empty()) {
    auto i = queue_.begin();
    message& c = *i;
    if (c.timestamp_ > time)
      break;
    queue_.erase(i);
    by_client_id_.erase_and_dispose(by_client_id_.iterator_to(c), deleter());
    count++;
  }
  if (count)
    LOG(DEBUG) << count << " requests dropped, "
      << by_client_id_.size() << " remaining\n";
}

bool client::add_request(std::unique_ptr<message>& context)
{
  if (this->context_) {
    return false;
  } else {
    context_ = std::move(context);
    this->send();
    return true;
  }
}

std::uint16_t client::random_client_id()
{
  while (1) {
    std::uint16_t id = service_->random_id();
    auto i = this->by_client_id_.find(id, order_message_by_client_id());
    if (i == this->by_client_id_.end())
      return id;
  }
}

void client::connect()
{
  // TODO, make this async
  LOG(DEBUG) << "Connecting\n";

  dnsfwd::endpoint const& endpoint = service_->tcp_connect_endpoints().at(0);

  boost::asio::ip::tcp::resolver resolver(*io_service_);
  boost::asio::ip::tcp::resolver::query query(
    endpoint.name, endpoint.port.empty() ? "domain" : endpoint.port);
  boost::asio::ip::tcp::resolver::iterator endpoint_iterator =
    resolver.resolve(query);
  boost::asio::ip::tcp::resolver::iterator end;
  if (boost::asio::connect(socket_, endpoint_iterator) == end) {
    LOG(ERR) << "Could not connect\n";
    this->reset();
  } else {
    LOG(DEBUG) << "Connected\n";

    boost::asio::ip::tcp::no_delay no_delay(true);
    socket_.set_option(no_delay);

    this->start_receive();
    this->send();
  }
}

void client::send()
{
  if (!context_) {
    context_ = service_->unqueue();
    if (!context_)
      return;
  }

  if (socket_.is_open()) {

    this->clear(std::chrono::steady_clock::now() - service_->time_to_live());

    // Choose a client ID:
    context_->client_id_ = this->random_client_id();
    context_->id(context_->client_id_);

    LOG(DEBUG) << "Forwarding request\n";
    boost::asio::async_write(
      socket_,
      context_->vc_buffer(),
      boost::bind(
        &client::on_send,
        this->shared_from_this(),
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred
      )
    );
  } else {
    reset();
  }
}

void client::on_send(const boost::system::error_code& error, std::size_t bytes_transferred)
{
  if (error) {
    LOG(ERR) << "Forward request: error " << error << '\n';
    reset();
  } else if (bytes_transferred != context_->size_ + sizeof(uint16_t)) {
    LOG(ERR) << "Forward request: transfer incomplete\n";
    reset();
  } else {
    LOG(DEBUG) << "Request forwarded\n";
    context_->timestamp_ = std::chrono::steady_clock::now();
    this->by_client_id_.insert(*context_);
    this->queue_.push_back(*context_);
    context_.release();
    this->send();
  }
}

void client::start_receive()
{
  boost::asio::async_read(
    socket_,
    boost::asio::buffer(&response_size_, sizeof(response_size_)),
    boost::bind(
      &client::on_message_size,
      this->shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred
    )
  );
}

void client::on_message_size(const boost::system::error_code& error, std::size_t size)
{
  if (error) {
    LOG(DEBUG) << "Reply reception error: " << error << '\n';
    reset();
  } else if (size != sizeof(uint16_t)) {
    LOG(ERR) << "Reply reception incomplete\n";
    reset();
  } else {
    LOG(DEBUG) << "Reply size received\n";
    this->response_size_ = ntohs(this->response_size_);
    buffer_.resize(this->response_size_);
    boost::asio::async_read(
      socket_,
      boost::asio::buffer(buffer_.data(), this->response_size_),
      boost::bind(
        &client::on_message,
        this->shared_from_this(),
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred
      )
    );
  }
}

void client::on_message(const boost::system::error_code& error, std::size_t size)
{
  if (error) {
    LOG(ERR) << "Reply reception error: " << error << '\n';
    reset();
  } else if (size != this->response_size_) {
    LOG(ERR) << "Reply reception incomplete\n";
    reset();
  } else if (size < MIN_MESSAGE_SIZE) {
    LOG(ERR) << "Reply received but too small\n";
    this->start_receive();
  } else {
    std::uint16_t client_id;
    std::memcpy(&client_id, buffer_.data(), sizeof(client_id));
    by_client_id_type::iterator i = by_client_id_.find(client_id,
      order_message_by_client_id());
    by_client_id_type::iterator end = by_client_id_.end();
    if (i == end) {
      LOG(ERR) << "Reply received not expected\n";
      this->start_receive();
      return;
    }
    LOG(DEBUG) << "Reply received\n";
    message& c = *i;
    assert(c.client_id_ == client_id);
    std::memcpy(buffer_.data(), &c.server_id_, sizeof(c.server_id_));
    c.server_->send_response(std::move(buffer_), c.endpoint_);
    by_client_id_.erase(i);
    queue_.erase_and_dispose(queue_.iterator_to(c), deleter());
    this->start_receive();
  }
}

void client::reset()
{
  if (socket_.is_open()) {
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    socket_.close();
  }
  service_->unregister(this->shared_from_this());
}

}
