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

#include <cstdlib>

#include <iostream>
#include <exception>

#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <boost/asio/io_service.hpp>

#include "dnsfwd.hpp"

namespace dnsfwd {

int loglevel = 5;

}

static int systemd_main(char* host, char* port)
{
#ifdef USE_SYSTEMD
  int count = sd_listen_fds(1);
  if (count < 0) {
    LOG(CRIT) << "No systemd environment\n";
    return 1;
  }
  if (count != 1) {
    LOG(CRIT) << "Expected 1 socket but got " << count << '\n';
    return 1;
  }
  boost::asio::io_service io_service;
  dnsfwd::service service(io_service, host, port, SD_LISTEN_FDS_START);
  io_service.run();
  return 0;
#else
  LOG(CRIT) << "No systemd support\n";
  std::exit(1);
#endif
}

static int standalone_main(char* host, char* port)
{
  boost::asio::io_service io_service;
  dnsfwd::service service(io_service, host, port);
  io_service.run();
  return 0;
}

int main(int argc, char** argv)
{
  try {
    if (argc < 3) {
      LOG(CRIT) << "Bad number of arguments\n";
      std::exit(1);
    }

    if (strcmp(argv[1], "--listen-fds") == 0) {
      if (argc < 4) {
        LOG(CRIT) << "Bad number of arguments\n";
        std::exit(1);
      }
      return systemd_main(argv[2], argv[3]);
    }

    return standalone_main(argv[1], argv[2]);
  }
  catch (std::exception& e) {
    LOG(CRIT) << e.what() << "\n";
    return 1;
  }
}
