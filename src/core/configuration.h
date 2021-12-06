// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.

#pragma once

#include "util/filesystem.h"
#include "util/pimpl.h"
#include "util/result.h"
#include "util/threading.h"

#include <optional>
#include <string>
#include <vector>

#include <spdlog/common.h>

namespace zeek::agent {

namespace options {
// Default log level for new configuration objects.
extern spdlog::level::level_enum default_log_level;
} // namespace options

/**
 * Global agent options that can be set through the command-line or other
 * means.
 */
struct Options {
    /**
     * The agent's unique ID. This is generated randomly at first, then
     * persistent across runs.
     */
    std::string agent_id;

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

    /** True to run unit tests instead of normal operation. */
    bool run_tests = false;

    /** True to have any tables only report mock data for testing. */
    bool use_mock_data = false;

    /** Logs a summary of the current settings to the debug log stream. */
    void debugDump();
};

/**
 * Manages the agent's global configuration. This maintaince an `Options`
 * instance with the options currently in effect.
 *
 * All public methods in this class are thread-safe.
 */
class Configuration : public Pimpl<Configuration>, protected SynchronizedBase {
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
