// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.

#pragma once

#include "util/filesystem.h"
#include "util/helpers.h"
#include "util/pimpl.h"
#include "util/result.h"

#include <optional>
#include <string>
#include <vector>

#include <spdlog/common.h>

namespace zeek::agent {

namespace options {
// Default log level for new configuration objects.
extern spdlog::level::level_enum default_log_level;

/**
 * Defines the mode of operation for the Zeek Agent process. This captures a
 * couple of special modes beyond normal operation.
 **/
enum class Mode {
    Standard, /**< normal operation */
    Test,     /**< run unit tests and exit */
    AutoDoc   /**< print out JSON describing table schemas and exit */
};

} // namespace options

/**
 * Global agent options that can be set through the command-line or other
 * means.
 */
struct Options {
    /** Logs a summary of the current settings to the debug log stream. */
    void debugDump();

    /** Mode of operation for the current process. */
    options::Mode mode = options::Mode::Standard;

    /**
     * The agent's unique ID. This is generated randomly at first, then
     * persistent across runs.
     */
    std::string agent_id;

    /**
     * ID for the current agent process. This ID is unique relative to the
     * agent ID and changes with each restart of the agent. This is
     * automatically determined and not user-changeable.
     */
    std::string instance_id;

    /**
     * Configuration file to load at startup (which will update options in
     * turn).
     */
    std::optional<filesystem::path> config_file;

    /** Console statement/command to execute at startup, and then terminate */
    std::string execute;

    /** True to spawn the interactive console */
    bool interactive = false;

    /** The agent's level of logging. Default is `warn` and worse. */
    std::optional<spdlog::level::level_enum> log_level;

    /** True to have any tables only report mock data for testing. */
    bool use_mock_data = false;

    /** Terminate when a Zeek connections goes down (instead of retrying). */
    bool terminate_on_disconnect = false;

    /** Zeek instances to connect to */
    std::vector<std::string> zeek_destinations;

    /**
     * Set of groups that the agent is part of. In addition, all agengs are
     * implicitly part of the groups `all` and `<os>`.
     */
    std::vector<std::string> zeek_groups;

    /**
     * Interval to attempt reconnecting after a Zeek connection went down.
     */
    Interval zeek_reconnect_interval = 30s;

    /**
     * Interval to expire any state (incl. queries) for a connected Zeek
     * instance if no activity has been seen from it. (Note that this should be
     * longer than the Zeek-side hello interval.)
     */
    Interval zeek_timeout = 120s;

    /** Interval to broadcast "hello" pings. */
    Interval zeek_hello_interval = 60s;

    /**
     * If true, do not use SSL for network connections. By default, SSL will
     * even be used even if no certificates / CAs have been configured, so that
     * the communication will always be encrypted (but not authenticated in that
     * case).
     */
    bool zeek_ssl_disable = false;

    /**
     * Path to a file containing concatenated trusted certificates in PEM
     * format. If set, the agent will require valid certificates for all peers.
     */
    std::string zeek_ssl_cafile;

    /**
     * Path to an OpenSSL-style directory of trusted certificates. If set, the
     * agent will require valid certificates for all peers.
     */
    std::string zeek_ssl_capath;

    /**
     * Path to a file containing a X.509 certificate for this node in PEM
     * format. If set, the agent will require valid certificates for all peers.
     */
    std::string zeek_ssl_certificate;

    /**
     * Passphrase to decrypt the private key specified by `zeek_ssl_keyfile`. If
     * set, the agent will require valid certificates for all peers.
     */
    std::string zeek_ssl_passphrase;

    /**
     * Path to the file containing the private key for this node's certificate.
     * If set, the agent will require valid certificates for all peers.
     */
    std::string zeek_ssl_keyfile;
};

/**
 * Manages the agent's global configuration. This maintaince an `Options`
 * instance with the options currently in effect.
 *
 * All public methods in this class are thread-safe.
 */
class Configuration : public Pimpl<Configuration> {
public:
    Configuration();
    ~Configuration();

    /** Returns the options currently in effect. */
    const Options& options() const;

    /**
     * Parses a set of command line options. This first resets the current
     * options back to their defaults, and then updates them according to any
     * options given. If the options specify a configuration file to read, that
     * will be pulled in as well (with command line options taking precedence).
     *
     * The method internally stores the options for later re-application.
     *
     * For a couple of diagnostic options, this will directly terminate the
     * current processes (e.g., ``--help``).
     *
     * @param argc number of arguments
     * @param argv array of size `argc` with arguments, with argv[0] being the executable
     * @return result will flag any errors that occurred
     */
    Result<Nothing> initFromArgv(int argc, const char* const* argv);

    /**
     * Parses an agent configuration file. This first resets the current
     * options back to their defaults, and updates the current set of options
     * accordingly. If any command line options have been previously provided,
     * it reapplys them on top at the end as well.
     *
     * @param path file to read
     * @return result will flag any errors that occurred
     **/
    Result<Nothing> read(const filesystem::path& path);

    /**
     * Parses an agent configuration file from already open input stream. This
     * first resets the current options back to their defaults, and updates the
     * current set of options accordingly. If any command line options have
     * been previously provided, it reapplys them on top at the end as well.
     *
     * @param in already open stream to read the content from
     * @param path path associated with input (for error messages)
     * @return result will flag any errors that occurred
     **/
    Result<Nothing> read(std::istream& in, const filesystem::path& path);
};

} // namespace zeek::agent
