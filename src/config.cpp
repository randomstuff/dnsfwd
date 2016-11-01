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

#include <regex>

#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#else
#define SD_LISTEN_FDS_START 3
#endif

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options.hpp>

#include "dnsfwd.hpp"

namespace {

static const char* kernel_logformat[8] = {
  "<0>",
  "<1>",
  "<2>",
  "<3>",
  "<4>",
  "<5>",
  "<6>",
  "<7>"
};

static const char* daemon_logformat[8] = {
  "<24>",
  "<25>",
  "<26>",
  "<27>",
  "<28>",
  "<29>",
  "<30>",
  "<31>"
};

static const char* human_logformat[8] = {
  "EMERG: ",
  "ALERT: ",
  "CRIT: ",
  "ERR: ",
  "WARN: ",
  "NOTICE: ",
  "INFO: ",
  "DEBUG: "
};

}

namespace dnsfwd {

int loglevel = 5;

const char** logformat = kernel_logformat;

namespace {

endpoint parse_endpoint(std::string const& e)
{

  std::smatch match;

  std::regex ipv6_port("^\\[([^\\]]*)\\]:([0-9a-zA-Z]*)$");
  std::regex ipv6_no_port("^\\[([^\\]]*)\\]$");
  std::regex other_port("^([^:]*):([0-9a-zA-Z]*)$");
  std::regex other_no_port("^([^:]*)$");

  if (std::regex_match(e, match, ipv6_port)
    || std::regex_match(e, match, ipv6_no_port)
    || std::regex_match(e, match, other_port)
    || std::regex_match(e, match, other_no_port)) {
    endpoint res;
    res.name = match[1];
    if (match.size() > 1)
      res.port = match[2];
    return res;
  } else {
    LOG(ERR) << "Invalid endpoint specification\n";
    std::exit(1);
  }
}

}

boost::asio::ip::udp::endpoint endpoint::udp_endpoint(
  boost::asio::io_service& service, const char* default_port) const
{
  boost::asio::ip::udp::resolver resolver(service);
  boost::asio::ip::udp::resolver::query query(
    this->name, this->port.empty() ? default_port : this->port);
  boost::asio::ip::udp::resolver::iterator endpoint_iterator =
    resolver.resolve(query);
  boost::asio::ip::udp::resolver::iterator end;
  return *endpoint_iterator;
}

void setup_config(dnsfwd::config& config, int argc, char** argv)
{
  using boost::program_options::options_description;
  using boost::program_options::value;
  using boost::program_options::variables_map;
  using boost::program_options::store;
  using boost::program_options::option;
  using boost::program_options::notify;
  using boost::program_options::parse_command_line;
  using boost::program_options::command_line_parser;
  using boost::program_options::positional_options_description;

  options_description desc("Allowed options");
  desc.add_options()
    ("help", "help")
    ("bind-udp", value<std::vector<std::string>>(), "bind to the given UDP address")
    ("connect-tcp", value<std::vector<std::string>>(), "connect to the given TCP endpoint")
    ("loglevel", value<int>(), "loglevel")
    ("logformat", value<std::string>(), "logformat")
    ;

  variables_map vm;
  store(command_line_parser(argc, argv).options(desc).run(), vm);
  notify(vm);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    std::exit(0);
  }

  if (vm.count("logformat")) {
    std::string format = vm["logformat"].as<std::string>();
    if (format == "kernel") {
      logformat = kernel_logformat;
    } else if (format == "daemon") {
      logformat = daemon_logformat;
    } else if (format == "human") {
      logformat = human_logformat;
    } else {
      LOG(ERR) << "unexpected log format\n";
      std::exit(1);
    }
  }

  if (vm.count("loglevel")) {
    int loglevel = vm["loglevel"].as<int>();
    if (loglevel < 0 || loglevel > 8) {
      LOG(ERR) << "unexpected loglevel\n";
      std::exit(1);
    }
    ::dnsfwd::loglevel = loglevel;
  }

  if (vm.count("bind-udp"))
    for (std::string const& e : vm["bind-udp"].as<std::vector<std::string>>())
      config.bind_udp.push_back(parse_endpoint(e));
  if (vm.count("connect-tcp"))
    for (std::string const& e : vm["connect-tcp"].as<std::vector<std::string>>())
      config.connect_tcp.push_back(parse_endpoint(e));

  #ifdef USE_SYSTEMD
    if (!config.listen_fds) {
      config.listen_fds = sd_listen_fds(1);
      if (config.listen_fds < 0)
        config.listen_fds = 0;
    }
  #endif
}

}
