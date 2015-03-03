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

#include <ctime>

#include <boost/asio/io_service.hpp>

namespace dnsfwd {

service::service(boost::asio::io_service& io_service, char* host, char* port, int socket)
  : io_service_(&io_service),
    host_(host), port_(port),
    server_(io_service, *this, socket),
    random_(std::time(nullptr))
{
}

service::service(boost::asio::io_service& io_service, char* host, char* port)
  : io_service_(&io_service),
    host_(host), port_(port),
    server_(io_service, *this),
    random_(std::time(nullptr))
{
}

void service::add_request(std::unique_ptr<message>& context)
{
  std::memcpy(&context->server_id_, context->buffer_.data(), sizeof(uint16_t));

  if (!client_) {
    client_ = std::make_shared<client>(*io_service_, *this);
    client_->connect();
  }

  if (!client_->add_request(context)) {
    this->queue_.push_back(*context);
    context.release();
  }
}

std::unique_ptr<message> service::unqueue()
{
  if (queue_.empty()) {
    return nullptr;
  } else {
    message& c = queue_.front();
    queue_.pop_front();
    return std::unique_ptr<message>(&c);
  }
}

void service::unregister(std::shared_ptr<client> client)
{
  if (client == client_) {
    client_ = nullptr;
  }
}

}
