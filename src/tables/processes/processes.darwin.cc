// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.

#include "processes.h"

#include "autogen/config.h"
#include "core/configuration.h"
#include "core/database.h"
#include "core/logger.h"
#include "core/table.h"
#include "util/fmt.h"

#include <libproc.h>

namespace zeek::agent::table {

class ProcessesDarwin : public ProcessesCommon {
public:
    std::vector<std::vector<Value>> snapshot(const std::vector<table::Where>& wheres) override;
    void addProcess(std::vector<std::vector<Value>>* rows, const struct proc_bsdinfo* p,
                    const struct proc_taskinfo* ti);
};

namespace {
database::RegisterTable<ProcessesDarwin> _;
}

std::vector<std::vector<Value>> ProcessesDarwin::snapshot(const std::vector<table::Where>& wheres) {
    auto buffer_size = proc_listpids(PROC_ALL_PIDS, 0, 0, 0);
    pid_t pids[buffer_size / sizeof(pid_t)];
    buffer_size = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    if ( buffer_size <= 0 ) {
        logger()->warn(format("sockets: cannot get pids"));
        return {};
    }

    std::vector<std::vector<Value>> rows;

    for ( size_t i = 0; i < buffer_size / sizeof(pid_t); i++ ) {
        errno = 0;
        struct proc_bsdinfo pi;
        auto n = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &pi, sizeof(pi));

        if ( n < static_cast<int>(sizeof(pi)) || errno != 0 ) {
            if ( errno == ESRCH ) // ESRCH -> process is gone
                continue;

            logger()->debug(format("sockets: could not get process information for PID {}", pids[i]));
            continue;
        }

        struct proc_taskinfo ti;
        n = proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, sizeof(ti)); // this should succeed now, but we stay careful

        if ( pids[i] > 0 )
            addProcess(&rows, &pi, (n >= 0 ? &ti : nullptr));
    }

    return rows;
}

void ProcessesDarwin::addProcess(std::vector<std::vector<Value>>* rows, const struct proc_bsdinfo* pi,
                                 const struct proc_taskinfo* ti) {
    Value name = value::fromOptionalString(pi->pbi_name);
    Value pid = pi->pbi_pid;
    Value uid = pi->pbi_uid;
    Value gid = pi->pbi_gid;
    Value ppid = pi->pbi_ppid;
    Value nice = pi->pbi_nice;
    Value started = static_cast<int64_t>(pi->pbi_start_tvsec);

    Value vsize, rsize, utime, stime;
    if ( ti ) {
        vsize = static_cast<int64_t>(ti->pti_virtual_size);
        rsize = static_cast<int64_t>(ti->pti_resident_size);
        utime = static_cast<int64_t>(ti->pti_total_user);
        stime = static_cast<int64_t>(ti->pti_total_system);
    }

    rows->push_back({name, pid, uid, gid, ppid, nice, started, vsize, rsize, utime, stime});
}

} // namespace zeek::agent::table
