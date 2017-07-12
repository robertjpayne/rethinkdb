// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "extproc/http_job.hpp"

#include <ctype.h>
#include <re2/re2.h>

#include <limits>

#include "containers/archive/boost_types.hpp"
#include "containers/archive/stl_types.hpp"
#include "extproc/extproc_job.hpp"
#include "http/http_parser.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rdb_protocol/env.hpp"

// The job_t runs in the context of the main rethinkdb process
http_job_t::http_job_t(extproc_pool_t *pool, signal_t *interruptor) :
    extproc_job(pool, &worker_fn, interruptor) { }

void http_job_t::http(const http_opts_t &opts,
                      http_result_t *res_out) {
    write_message_t msg;
    serialize<cluster_version_t::LATEST_OVERALL>(&msg, opts);
    {
        int res = send_write_message(extproc_job.write_stream(), &msg);
        if (res != 0) {
            throw extproc_worker_exc_t("failed to send data to the worker");
        }
    }

    archive_result_t res
        = deserialize<cluster_version_t::LATEST_OVERALL>(extproc_job.read_stream(),
                                                         res_out);
    if (bad(res)) {
        throw extproc_worker_exc_t(strprintf("failed to deserialize result from worker "
                                             "(%s)", archive_result_as_str(res)));
    }
}

void http_job_t::worker_error() {
    extproc_job.worker_error();
}

bool http_job_t::worker_fn(read_stream_t *stream_in, write_stream_t *stream_out) {
    return true;
}

