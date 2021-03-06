/**
 * @file main.cpp main function for ralf
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include <getopt.h>
#include <signal.h>
#include <semaphore.h>
#include <strings.h>
#include <boost/filesystem.hpp>

#include "ralf_pd_definitions.h"

#include "ipv6utils.h"
#include "memcachedstore.h"
#include "httpresolver.h"
#include "chronosconnection.h"
#include "accesslogger.h"
#include "log.h"
#include "utils.h"
#include "httpstack.h"
#include "handlers.hpp"
#include "logger.h"
#include "saslogger.h"
#include "rf.h"
#include "peer_message_sender_factory.hpp"
#include "sas.h"
#include "load_monitor.h"
#include "diameterresolver.h"
#include "realmmanager.h"
#include "communicationmonitor.h"
#include "exception_handler.h"
#include "ralf_alarmdefinition.h"
#include "namespace_hop.h"

enum OptionTypes
{
  DNS_SERVER=256+1,
  MEMCACHED_WRITE_FORMAT,
  TARGET_LATENCY_US,
  MAX_TOKENS,
  INIT_TOKEN_RATE,
  MIN_TOKEN_RATE,
  EXCEPTION_MAX_TTL,
  BILLING_PEER,
  HTTP_BLACKLIST_DURATION,
  DIAMETER_BLACKLIST_DURATION,
  SAS_USE_SIGNALING_IF,
  PIDFILE,
  DAEMON,
};

enum struct MemcachedWriteFormat
{
  BINARY, JSON
};

struct options
{
  std::string local_host;
  std::string diameter_conf;
  std::string dns_server;
  std::string http_address;
  unsigned short http_port;
  int http_threads;
  std::string billing_realm;
  std::string billing_peer;
  int max_peers;
  bool access_log_enabled;
  std::string access_log_directory;
  bool log_to_file;
  std::string log_directory;
  int log_level;
  std::string sas_server;
  std::string sas_system_name;
  MemcachedWriteFormat memcached_write_format;
  int target_latency_us;
  int max_tokens;
  float init_token_rate;
  float min_token_rate;
  int exception_max_ttl;
  int http_blacklist_duration;
  int diameter_blacklist_duration;
  std::string pidfile;
  bool daemon;
  bool sas_signaling_if;
};

const static struct option long_opt[] =
{
  {"localhost",                   required_argument, NULL, 'l'},
  {"diameter-conf",               required_argument, NULL, 'c'},
  {"dns-server",                  required_argument, NULL, DNS_SERVER},
  {"http",                        required_argument, NULL, 'H'},
  {"http-threads",                required_argument, NULL, 't'},
  {"billing-realm",               required_argument, NULL, 'b'},
  {"billing-peer",                required_argument, NULL, BILLING_PEER},
  {"max-peers",                   required_argument, NULL, 'p'},
  {"access-log",                  required_argument, NULL, 'a'},
  {"log-file",                    required_argument, NULL, 'F'},
  {"log-level",                   required_argument, NULL, 'L'},
  {"sas",                         required_argument, NULL, 's'},
  {"help",                        no_argument,       NULL, 'h'},
  {"memcached-write-format",      required_argument, 0,    MEMCACHED_WRITE_FORMAT},
  {"target-latency-us",           required_argument, NULL, TARGET_LATENCY_US},
  {"max-tokens",                  required_argument, NULL, MAX_TOKENS},
  {"init-token-rate",             required_argument, NULL, INIT_TOKEN_RATE},
  {"min-token-rate",              required_argument, NULL, MIN_TOKEN_RATE},
  {"exception-max-ttl",           required_argument, NULL, EXCEPTION_MAX_TTL},
  {"http-blacklist-duration",     required_argument, NULL, HTTP_BLACKLIST_DURATION},
  {"diameter-blacklist-duration", required_argument, NULL, DIAMETER_BLACKLIST_DURATION},
  {"pidfile",                     required_argument, NULL, PIDFILE},
  {"daemon",                      no_argument,       NULL, DAEMON},
  {"sas-use-signaling-interface", no_argument,       NULL, SAS_USE_SIGNALING_IF},
  {NULL,                          0,                 NULL, 0},
};

static std::string options_description = "l:c:H:t:b:p:a:F:L:s:h";

void usage(void)
{
  puts("Options:\n"
       "\n"
       " -l, --localhost <hostname> Specify the local hostname or IP address\n"
       " -c, --diameter-conf <file> File name for Diameter configuration\n"
       "     --dns-server <IP>      DNS server to use to resolve addresses\n"
       " -H, --http <address>[:<port>]\n"
       "                            Set HTTP bind address and port (default: 0.0.0.0:8888)\n"
       " -t, --http-threads N       Number of HTTP threads (default: 1)\n"
       " -b, --billing-realm <name> Set Destination-Realm on Rf messages\n"
       "     --billing-peer <name>  If Ralf can't find a CDF by resolving the --billing-realm,\n"
       "                            it will try and connect to this Diameter peer.\n"
       " -p, --max-peers N          Number of peers to connect to (default: 2)\n"
       " -a, --access-log <directory>\n"
       "                            Generate access logs in specified directory\n"
       " -F, --log-file <directory>\n"
       "                            Log to file in specified directory\n"
       " -L, --log-level N          Set log level to N (default: 4)\n"
       " -s, --sas <host>,<system name>\n"
       "                            Use specified host as Service Assurance Server and specified\n"
       "                            system name to identify this system to SAS. If this option isn't\n"
       "                            specified, SAS is disabled\n"
       "     --memcached-write-format\n"
       "                            The data format to use when writing sessions\n"
       "                            to memcached. Values are 'binary' and 'json'\n"
       "                            (defaults to 'json')\n"
       "     --target-latency-us <usecs>\n"
       "                            Target latency above which throttling applies (default: 100000)\n"
       "     --max-tokens N         Maximum number of tokens allowed in the token bucket (used by\n"
       "                            the throttling code (default: 1000))\n"
       "     --init-token-rate N    Initial token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 100.0))\n"
       "     --min-token-rate N     Minimum token refill rate of tokens in the token bucket (used by\n"
       "                            the throttling code (default: 10.0))\n"
       "     --exception-max-ttl <secs>\n"
       "                            The maximum time before the process exits if it hits an exception.\n"
       "                            The actual time is randomised.\n"
       "     --http-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist an HTTP peer when it is unresponsive.\n"
       "     --diameter-blacklist-duration <secs>\n"
       "                            The amount of time to blacklist a Diameter peer when it is unresponsive.\n"
       "     --sas-use-signaling-interface\n"
       "                            Whether SAS traffic is to be dispatched over the signaling network\n"
       "                            interface rather than the default management interface\n"
       "     --pidfile=<filename>   Write pidfile\n"
       "     --daemon               Run as a daemon\n"
       " -h, --help                 Show this help screen\n"
      );
}

int init_logging_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'F':
      options.log_to_file = true;
      options.log_directory = std::string(optarg);
      break;

    case 'L':
      options.log_level = atoi(optarg);
      break;

    case DAEMON:
      options.daemon = true;
      break;

    default:
      // Ignore other options at this point
      break;
    }
  }

  return 0;
}

int init_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case 'l':
      TRC_INFO("Local host: %s", optarg);
      options.local_host = std::string(optarg);
      break;

    case 'c':
      TRC_INFO("Diameter configuration file: %s", optarg);
      options.diameter_conf = std::string(optarg);
      break;

    case 'H':
      TRC_INFO("HTTP address: %s", optarg);
      options.http_address = std::string(optarg);
      // TODO: Parse optional HTTP port.
      break;

    case 's':
    {
      std::vector<std::string> sas_options;
      Utils::split_string(std::string(optarg), ',', sas_options, 0, false);
      if ((sas_options.size() == 2) &&
          !sas_options[0].empty() &&
          !sas_options[1].empty())
      {
        options.sas_server = sas_options[0];
        options.sas_system_name = sas_options[1];
        TRC_INFO("SAS set to %s\n", options.sas_server.c_str());
        TRC_INFO("System name is set to %s\n", options.sas_system_name.c_str());
      }
    }
    break;

    case 't':
      TRC_INFO("HTTP threads: %s", optarg);
      options.http_threads = atoi(optarg);
      break;

    case 'b':
      TRC_INFO("Billing realm: %s", optarg);
      options.billing_realm = std::string(optarg);
      break;

    case BILLING_PEER:
      TRC_INFO("Fallback Diameter peer to connect to: %s", optarg);
      options.billing_peer = std::string(optarg);
      break;

    case 'p':
      TRC_INFO("Maximum peers: %s", optarg);
      options.max_peers = atoi(optarg);
      break;

    case 'a':
      TRC_INFO("Access log: %s", optarg);
      options.access_log_enabled = true;
      options.access_log_directory = std::string(optarg);
      break;

    case DNS_SERVER:
      TRC_INFO("DNS server set to %s", optarg);
      options.dns_server = std::string(optarg);
      break;

    case 'F':
    case 'L':
    case DAEMON:
      // Ignore daemon, F and L - these are handled by init_logging_options
      break;

    case 'h':
      usage();
      return -1;

    case MEMCACHED_WRITE_FORMAT:
      if (strcmp(optarg, "binary") == 0)
      {
        TRC_INFO("Memcached write format set to 'binary'");
        options.memcached_write_format = MemcachedWriteFormat::BINARY;
      }
      else if (strcmp(optarg, "json") == 0)
      {
        TRC_INFO("Memcached write format set to 'json'");
        options.memcached_write_format = MemcachedWriteFormat::JSON;
      }
      else
      {
        TRC_WARNING("Invalid value for memcached-write-format, using '%s'."
                    "Got '%s', valid values are 'json' and 'binary'",
                    ((options.memcached_write_format == MemcachedWriteFormat::JSON) ?
                     "json" : "binary"),
                    optarg);
      }
      break;

    case TARGET_LATENCY_US:
      options.target_latency_us = atoi(optarg);
      if (options.target_latency_us <= 0)
      {
        TRC_ERROR("Invalid --target-latency-us option %s", optarg);
        return -1;
      }
      break;

    case MAX_TOKENS:
      options.max_tokens = atoi(optarg);
      if (options.max_tokens <= 0)
      {
        TRC_ERROR("Invalid --max-tokens option %s", optarg);
        return -1;
      }
      break;

    case INIT_TOKEN_RATE:
      options.init_token_rate = atoi(optarg);
      if (options.init_token_rate <= 0)
      {
        TRC_ERROR("Invalid --init-token-rate option %s", optarg);
        return -1;
      }
      break;

    case MIN_TOKEN_RATE:
      options.min_token_rate = atoi(optarg);
      if (options.min_token_rate <= 0)
      {
        TRC_ERROR("Invalid --min-token-rate option %s", optarg);
        return -1;
      }
      break;

    case EXCEPTION_MAX_TTL:
      options.exception_max_ttl = atoi(optarg);
      TRC_INFO("Max TTL after an exception set to %d",
               options.exception_max_ttl);
      break;

    case HTTP_BLACKLIST_DURATION:
      options.http_blacklist_duration = atoi(optarg);
      TRC_INFO("HTTP blacklist duration set to %d",
               options.http_blacklist_duration);
      break;

    case DIAMETER_BLACKLIST_DURATION:
      options.diameter_blacklist_duration = atoi(optarg);
      TRC_INFO("Diameter blacklist duration set to %d",
               options.diameter_blacklist_duration);
      break;

    case PIDFILE:
      options.pidfile = std::string(optarg);
      break;

    case SAS_USE_SIGNALING_IF:
      options.sas_signaling_if = true;
      break;

    default:
      CL_RALF_INVALID_OPTION_C.log();
      TRC_ERROR("Unknown option: %d.  Run with --help for options.\n", opt);
      return -1;
    }
  }

  return 0;
}

static sem_t term_sem;
ExceptionHandler* exception_handler;

// Signal handler that triggers homestead termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void signal_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, signal_handler);

  // Log the signal, along with a backtrace.
  TRC_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  TRC_COMMIT();

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  CL_RALF_CRASHED.log(strsignal(sig));

  // Dump a core.
  abort();
}

int main(int argc, char**argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.local_host = "127.0.0.1";
  options.diameter_conf = "/var/lib/ralf/ralf.conf";
  options.dns_server = "127.0.0.1";
  options.http_address = "0.0.0.0";
  options.http_port = 10888;
  options.http_threads = 1;
  options.billing_realm = "dest-realm.unknown";
  options.billing_peer = "";
  options.max_peers = 2;
  options.access_log_enabled = false;
  options.log_to_file = false;
  options.log_level = 0;
  options.sas_server = "0.0.0.0";
  options.sas_system_name = "";
  options.memcached_write_format = MemcachedWriteFormat::JSON;
  options.target_latency_us = 100000;
  options.max_tokens = 1000;
  options.init_token_rate = 100.0;
  options.min_token_rate = 10.0;
  options.exception_max_ttl = 600;
  options.http_blacklist_duration = HttpResolver::DEFAULT_BLACKLIST_DURATION;
  options.diameter_blacklist_duration = DiameterResolver::DEFAULT_BLACKLIST_DURATION;
  options.pidfile = "";
  options.daemon = false;
  options.sas_signaling_if = false;

  // Initialise ENT logging before making "Started" log
  PDLogStatic::init(argv[0]);

  CL_RALF_STARTED.log();

  if (init_logging_options(argc, argv, options) != 0)
  {
    return 1;
  }

  Utils::daemon_log_setup(argc,
                          argv,
                          options.daemon,
                          options.log_directory,
                          options.log_level,
                          options.log_to_file);

  std::stringstream options_ss;
  for (int ii = 0; ii < argc; ii++)
  {
    options_ss << argv[ii];
    options_ss << " ";
  }
  std::string options_str = "Command-line options were: " + options_ss.str();

  TRC_INFO(options_str.c_str());

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  if (options.pidfile != "")
  {
    int rc = Utils::lock_and_write_pidfile(options.pidfile);
    if (rc == -1)
    {
      // Failure to acquire pidfile lock
      TRC_ERROR("Could not write pidfile - exiting");
      return 2;
    }
  }

  start_signal_handlers();

  if (options.sas_server == "0.0.0.0")
  {
    TRC_WARNING("SAS server option was invalid or not configured - SAS is disabled");
    CL_RALF_INVALID_SAS_OPTION.log();
  }

  // Create Ralf's alarm objects. Note that the alarm identifier strings must match those
  // in the alarm definition JSON file exactly.
  AlarmManager* alarm_manager = new AlarmManager();

  CommunicationMonitor* cdf_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                              "ralf",
                                                                              AlarmDef::RALF_CDF_COMM_ERROR,
                                                                              AlarmDef::CRITICAL),
                                                                    "Ralf",
                                                                    "CDF");

  CommunicationMonitor* chronos_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                  "ralf",
                                                                                  AlarmDef::RALF_CHRONOS_COMM_ERROR,
                                                                                  AlarmDef::CRITICAL),
                                                                        "Ralf",
                                                                        "Chronos");

  CommunicationMonitor* memcached_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                    "ralf",
                                                                                    AlarmDef::RALF_MEMCACHED_COMM_ERROR,
                                                                                    AlarmDef::CRITICAL),
                                                                          "Ralf",
                                                                          "Memcached");

  Alarm* vbucket_alarm = new Alarm(alarm_manager,
                                   "ralf",
                                   AlarmDef::RALF_VBUCKET_ERROR,
                                   AlarmDef::MAJOR);

  MemcachedStore* mstore = new MemcachedStore(true,
                                              "./cluster_settings",
                                              memcached_comm_monitor,
                                              vbucket_alarm);

  if (!(mstore->has_servers()))
  {
    TRC_ERROR("./cluster_settings file does not contain a valid set of servers");
    return 1;
  };

  AccessLogger* access_logger = NULL;
  if (options.access_log_enabled)
  {
    access_logger = new AccessLogger(options.access_log_directory);
  }

  SAS::init(options.sas_system_name,
            "ralf",
            SASEvent::CURRENT_RESOURCE_BUNDLE,
            options.sas_server,
            sas_write,
            options.sas_signaling_if ? create_connection_in_signaling_namespace
                                     : create_connection_in_management_namespace);

  LoadMonitor* load_monitor = new LoadMonitor(options.target_latency_us,
                                              options.max_tokens,
                                              options.init_token_rate,
                                              options.min_token_rate);

  HealthChecker* hc = new HealthChecker();
  hc->start_thread();

  // Create an exception handler. The exception handler doesn't need
  // to quiesce the process before killing it.
  exception_handler = new ExceptionHandler(options.exception_max_ttl,
                                           false,
                                           hc);

  Diameter::Stack* diameter_stack = Diameter::Stack::get_instance();
  Rf::Dictionary* dict = NULL;

  try
  {
    diameter_stack->initialize();
    diameter_stack->configure(options.diameter_conf,
                              exception_handler,
                              cdf_comm_monitor);
    dict = new Rf::Dictionary();
    diameter_stack->advertize_application(Diameter::Dictionary::Application::ACCT,
                                          dict->RF);
    diameter_stack->start();
  }
  catch (Diameter::Stack::Exception& e)
  {
    CL_RALF_DIAMETER_INIT_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to initialize Diameter stack - function %s, rc %d", e._func, e._rc);
    exit(2);
  }

  SessionStore::SerializerDeserializer* serializer;
  std::vector<SessionStore::SerializerDeserializer*> deserializers;

  if (options.memcached_write_format == MemcachedWriteFormat::JSON)
  {
    serializer = new SessionStore::JsonSerializerDeserializer();
  }
  else
  {
    serializer = new SessionStore::BinarySerializerDeserializer();
  }

  deserializers.push_back(new SessionStore::JsonSerializerDeserializer());
  deserializers.push_back(new SessionStore::BinarySerializerDeserializer());

  SessionStore* store = new SessionStore(mstore, serializer, deserializers);
  BillingHandlerConfig* cfg = new BillingHandlerConfig();
  PeerMessageSenderFactory* factory = new PeerMessageSenderFactory(options.billing_realm);

  // Create a DNS resolver.  We'll use this both for HTTP and for Diameter.
  DnsCachedResolver* dns_resolver = new DnsCachedResolver(options.dns_server);

  std::string port_str = std::to_string(options.http_port);

  // We want Chronos to call back to its local sprout instance so that we can
  // handle Ralfs failing without missing timers.
  int http_af = AF_INET;
  std::string chronos_callback_addr = "127.0.0.1:" + port_str;
  std::string local_chronos = "127.0.0.1:7253";
  if (is_ipv6(options.http_address))
  {
    http_af = AF_INET6;
    chronos_callback_addr = "[::1]:" + port_str;
    local_chronos = "[::1]:7253";
  }

  // Create a connection to Chronos.  This requires an HttpResolver.
  TRC_STATUS("Creating connection to Chronos at %s using %s as the callback URI", local_chronos.c_str(), chronos_callback_addr.c_str());
  HttpResolver* http_resolver = new HttpResolver(dns_resolver,
                                                 http_af,
                                                 options.http_blacklist_duration);
  ChronosConnection* timer_conn = new ChronosConnection(local_chronos, chronos_callback_addr, http_resolver, chronos_comm_monitor);

  cfg->mgr = new SessionManager(store, {}, dict, factory, timer_conn, diameter_stack, hc);

  HttpStack* http_stack = HttpStack::get_instance();
  HttpStackUtils::PingHandler ping_handler;
  BillingHandler billing_handler(cfg);
  try
  {
    http_stack->initialize();
    http_stack->configure(options.http_address,
                          options.http_port,
                          options.http_threads,
                          exception_handler,
                          access_logger,
                          load_monitor);
    http_stack->register_handler("^/ping$", &ping_handler);
    http_stack->register_handler("^/call-id/[^/]*$", &billing_handler);
    http_stack->start();
  }
  catch (HttpStack::Exception& e)
  {
    CL_RALF_HTTP_ERROR.log(e._func, e._rc);
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }

  // Create a DNS resolver and a Diameter specific resolver.
  int diameter_af = AF_INET;
  struct in6_addr dummy_addr;
  if (inet_pton(AF_INET6, options.local_host.c_str(), &dummy_addr) == 1)
  {
    TRC_DEBUG("Local host is an IPv6 address");
    diameter_af = AF_INET6;
  }

  DiameterResolver* diameter_resolver = new DiameterResolver(dns_resolver,
                                                             diameter_af,
                                                             options.diameter_blacklist_duration);
  RealmManager* realm_manager = new RealmManager(diameter_stack,
                                                 options.billing_realm,
                                                 options.billing_peer,
                                                 options.max_peers,
                                                 diameter_resolver);
  realm_manager->start();

  sem_wait(&term_sem);

  CL_RALF_ENDED.log();
  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    CL_RALF_HTTP_STOP_ERROR.log(e._func, e._rc);
    fprintf(stderr, "Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }

  try
  {
    diameter_stack->stop();
    diameter_stack->wait_stopped();
  }
  catch (Diameter::Stack::Exception& e)
  {
    CL_RALF_DIAMETER_STOP_FAIL.log(e._func, e._rc);
    TRC_ERROR("Failed to stop Diameter stack - function %s, rc %d", e._func, e._rc);
  }

  realm_manager->stop();

  delete realm_manager; realm_manager = NULL;
  delete diameter_resolver; diameter_resolver = NULL;
  delete http_resolver; http_resolver = NULL;
  delete dns_resolver; dns_resolver = NULL;
  delete load_monitor; load_monitor = NULL;

  hc->stop_thread();
  delete exception_handler; exception_handler = NULL;
  delete hc; hc = NULL;

  // Delete Ralf's alarm objects
  delete cdf_comm_monitor;
  delete chronos_comm_monitor;
  delete memcached_comm_monitor;
  delete vbucket_alarm;
  delete alarm_manager;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
