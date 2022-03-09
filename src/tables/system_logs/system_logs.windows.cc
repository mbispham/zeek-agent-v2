// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.
//
// Interface to journald. To avoid dependencies on external libraries, we spawn
// journalctl as a child process if we find it, reading from its output.

#include "system_logs.h"

#include "core/database.h"
#include "core/logger.h"
#include "util/fmt.h"
#include "util/windows-util.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace zeek::agent;
using namespace zeek::agent::table;

namespace {

// See the comment in getLogs() for info about this.
constexpr int MAX_RECORDS_TO_READ = 50;

enum class LogKind { System, Security };

static std::string kind_string(LogKind kind) {
    switch ( kind ) {
        case LogKind::System: return "System";
        case LogKind::Security: return "Security";
        default: return "Unknown";
    }
}

static std::wstring kind_wstring(LogKind kind) {
    switch ( kind ) {
        case LogKind::System: return L"System";
        case LogKind::Security: return L"Security";
        default: return L"Unknown";
    }
}

struct LogEntry {
    int64_t id;
    int64_t ts;
    LogKind kind;
    std::string priority;
    std::string source;
    std::string message;
};

struct LogHandle {
    HANDLE handle;
    DWORD last_read;
    LogKind kind;
};

class SystemLogsWindows : public SystemLogs {
public:
    void activate() override;
    void deactivate() override;
    void poll() override;

private:
    void getLogs(LogHandle& log_handle, std::vector<LogEntry>& results);
    std::optional<LogEntry> processRecord(char* buffer, PEVENTLOGRECORD record, LogKind kind);

    LogHandle system{nullptr, 0, LogKind::System};
    LogHandle security{nullptr, 0, LogKind::Security};

    std::map<std::wstring, HMODULE> dll_cache;
};

database::RegisterTable<SystemLogsWindows> _;

void SystemLogsWindows::activate() {
    system.handle = OpenEventLog(NULL, "System");
    if ( ! system.handle ) {
        std::error_condition cond = std::system_category().default_error_condition(GetLastError());
        logger()->info(format("Failed to open System event log: {}", cond.message()));
    }

    // The system log requires elevated privileges to open. Right now, it'll just log something if it
    // fails to open it.
    security.handle = OpenEventLog(NULL, "Security");
    if ( ! security.handle ) {
        std::error_condition cond = std::system_category().default_error_condition(GetLastError());
        logger()->info(format("Failed to open Security event log: {}", cond.message()));
    }
}

void SystemLogsWindows::deactivate() {
    if ( system.handle ) {
        CloseEventLog(system.handle);
        system.handle = nullptr;
    }

    if ( security.handle ) {
        CloseEventLog(security.handle);
        security.handle = nullptr;
    }

    logger()->debug(format("SystemLogsWindows: {} entries in dll cache at shutdown", dll_cache.size()));
    for ( auto& [key, library] : dll_cache )
        FreeLibrary(library);
}

void SystemLogsWindows::poll() {
    std::vector<LogEntry> logs;
    logs.reserve(MAX_RECORDS_TO_READ);

    if ( system.handle )
        getLogs(system, logs);
    if ( security.handle )
        getLogs(security, logs);

    std::sort(logs.begin(), logs.end(),
              [](const auto& a, const auto& b) { return std::tie(a.ts, a.kind, a.id) < std::tie(b.ts, b.kind, b.id); });

    int count = 0;
    for ( const auto& log : logs ) {
        Value t = Time(std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(log.ts)));
        Value id = format("{} {}", kind_string(log.kind), log.id);
        newEvent({t, log.source, log.priority, log.message, id});
        if ( ++count > MAX_RECORDS_TO_READ )
            break;
    }
}

void SystemLogsWindows::getLogs(LogHandle& log_handle, std::vector<LogEntry>& results) {
    DWORD status = ERROR_SUCCESS;
    DWORD bytes_to_read = 0x10000;
    DWORD bytes_needed;
    DWORD bytes_read = 0;

    // We don't really have any way to control exactly how many records get read here
    // since the size of a record may vary fairly wildly. Instead, we read as many as
    // we can on each pass, but stop after we've reached some arbitrary limit so that
    // it doesn't go crazy and read all multi-tens-of-thousands of records at once.
    int records_read = 0;

    DWORD read_flag = 0;
    if ( log_handle.last_read == 0 )
        read_flag = EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ;
    else
        read_flag = EVENTLOG_FORWARDS_READ | EVENTLOG_SEEK_READ;

    auto buffer = reinterpret_cast<wchar_t*>(malloc(bytes_to_read));
    if ( ! buffer )
        return;

    while ( status == ERROR_SUCCESS && records_read < MAX_RECORDS_TO_READ ) {
        // If reading in SEEK mode, read from the record after the last record read.
        if ( ! ReadEventLogW(log_handle.handle, read_flag, log_handle.last_read + 1, buffer, bytes_to_read, &bytes_read,
                             &bytes_needed) ) {
            status = GetLastError();
            if ( status == ERROR_INSUFFICIENT_BUFFER ) {
                auto temp = reinterpret_cast<wchar_t*>(realloc(buffer, bytes_needed));
                if ( ! temp )
                    break;

                buffer = temp;
                bytes_to_read = bytes_needed;
            }
            else if ( status != ERROR_HANDLE_EOF ) {
                std::error_condition cond = std::system_category().default_error_condition(static_cast<int>(status));
                logger()->debug(format("Failed to read the event log: {}", cond.message()));
                break;
            }
        }
        else {
            char* current = reinterpret_cast<char*>(buffer);
            char* end = current + bytes_read;

            while ( current < end && records_read < MAX_RECORDS_TO_READ ) {
                auto record = reinterpret_cast<PEVENTLOGRECORD>(current);
                if ( auto result = processRecord(current, record, log_handle.kind) )
                    results.push_back(result.value());
                if ( record->RecordNumber > log_handle.last_read )
                    log_handle.last_read = record->RecordNumber;
                current += record->Length;
                records_read++;
            }
        }
    }

    if ( buffer )
        free(buffer);
}

constexpr auto KEY_SIZE = 8192;

static std::string event_type_string(DWORD type) {
    switch ( type ) {
        case EVENTLOG_ERROR_TYPE: return "error";
        case EVENTLOG_AUDIT_FAILURE: return "audit_failure";
        case EVENTLOG_AUDIT_SUCCESS: return "audit_success";
        case EVENTLOG_INFORMATION_TYPE: return "info";
        case EVENTLOG_WARNING_TYPE: return "warning";
        default: return format("unknown ({})", type);
    }
}

std::optional<LogEntry> SystemLogsWindows::processRecord(char* buffer, PEVENTLOGRECORD record, LogKind kind) {
    LogEntry entry{};
    entry.id = static_cast<int64_t>(record->RecordNumber);
    entry.ts = static_cast<int64_t>(record->TimeGenerated);
    entry.priority = event_type_string(record->EventType);
    entry.kind = kind;

    std::wstring source = reinterpret_cast<wchar_t*>(buffer + sizeof(*record));
    entry.source = narrow_wstring(source);

    std::wstring key_name = L"SYSTEM\\CurrentControlSet\\Services\\Eventlog\\" + kind_wstring(kind) + L"\\" + source;

    // TODO: we could potentially add some caching here to avoid needing to do this registry look up every
    // single time we come through here. Maybe something keyed off the key_name above.

    HKEY key_handle;
    wchar_t message_file[KEY_SIZE];

    // Look in the registry for a key called EventMessageFile for the above directory. This key will store
    // the path to a DLL used for formatting the strings from the event log entry.
    DWORD res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_name.c_str(), 0, KEY_READ, &key_handle);
    if ( FAILED(res) ) {
        std::error_condition cond = std::system_category().default_error_condition(static_cast<int>(GetLastError()));
        logger()->error(format("Failed open registry for key {}: {}", narrow_wstring(key_name), cond.message()));

        RegCloseKey(key_handle);
        return entry;
    }
    else {
        DWORD key_size = KEY_SIZE;
        DWORD key_type;
        res = RegQueryValueExW(key_handle, L"EventMessageFile", NULL, &key_type,
                               reinterpret_cast<LPBYTE>(message_file), &key_size);
        RegCloseKey(key_handle);

        // It's not really an error if we don't find the key here. It just means that we won't be able
        // to format the strings. We still want the rest of the event log entry. Log something just so
        // we know what happened.
        if ( res != ERROR_SUCCESS ) {
            if ( res != ERROR_FILE_NOT_FOUND ) {
                std::error_condition cond =
                    std::system_category().default_error_condition(static_cast<int>(GetLastError()));
                logger()->error(format("Failed to find EventMessageFile registry entry for {}: {}",
                                       narrow_wstring(key_name), cond.message()));
            }
        }
    }

    wchar_t formatted[KEY_SIZE];

    // The filename from the registry entry might have things like %SYSTEMROOT% in it. Calling ExpandEnvironementStrings
    // will expand any of those into the value stored in the matching environment variables.
    res = ExpandEnvironmentStringsW(message_file, formatted, KEY_SIZE);
    if ( res == 0 ) {
        std::error_condition cond = std::system_category().default_error_condition(static_cast<int>(GetLastError()));
        logger()->error(format("Failed to expand strings for {}: {}", narrow_wstring(message_file), cond.message()));
    }

    // Break up the strings from the record into an array of strings so we can
    // pass them as a whole to FormatMessage().
    std::vector<wchar_t*> all_strings;
    all_strings.reserve(record->NumStrings);
    auto* curr_string = reinterpret_cast<wchar_t*>(buffer + record->StringOffset);

    for ( int i = 0; i < record->NumStrings; i++ ) {
        all_strings.push_back(curr_string);
        if ( i <= record->NumStrings - 1 )
            curr_string += wcslen(curr_string) + 1;
    }

    auto parts = split(std::wstring(formatted), L";");
    for ( const auto& filename : parts ) {
        if ( filename.empty() )
            continue;

        // Check if we already have this module in the cache and load it if we don't.
        HMODULE dll;
        auto it = dll_cache.find(filename);
        if ( it != dll_cache.end() )
            dll = it->second;
        else {
            dll = LoadLibraryExW(filename.data(), NULL, LOAD_LIBRARY_AS_DATAFILE);
            if ( dll )
                dll_cache.insert({filename, dll});
            else {
                std::error_condition cond =
                    std::system_category().default_error_condition(static_cast<int>(GetLastError()));
                logger()->error(format("Failed to load dll from {}: {}", narrow_wstring(filename), cond.message()));
            }
        }

        wchar_t* actual_message = nullptr;
        res =
            FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                           dll, record->EventID, NULL, reinterpret_cast<LPWSTR>(&actual_message), 0,
                           reinterpret_cast<va_list*>(all_strings.data()));
        if ( res == 0 && GetLastError() != ERROR_MR_MID_NOT_FOUND ) {
            std::error_condition cond =
                std::system_category().default_error_condition(static_cast<int>(GetLastError()));
            logger()->error(format("Failed to format message: {}", narrow_wstring(filename), cond.message()));
        }

        if ( actual_message ) {
            entry.message = narrow_wstring(std::wstring(actual_message));
            LocalFree(actual_message);
            break;
        }
    }

    // DCOM (and others) are missing this EventMessageFile registry entry and so therefore won't get
    // their strings formatted correctly. In these cases, return a concatenated list of the strings
    // from the event.
    if ( entry.message.empty() ) {
        auto trans =
            transform(all_strings, [](const wchar_t* s) -> std::string { return narrow_wstring(std::wstring(s)); });
        entry.message = join(trans, ", ");
    }

    return entry;
}

} // namespace
