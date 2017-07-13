// Copyright 2010-2012 RethinkDB, all rights reserved.

#include <time.h>

#include <string>

#include "debug.hpp"
#include "arch/runtime/thread_pool.hpp"   /* for `run_in_blocker_pool()` */
#include "containers/archive/file_stream.hpp"
#include "http/file_app.hpp"
#include "logger.hpp"
#include "stl_utils.hpp"
#include "time.hpp"
#include "http/web_assets.hpp"

file_http_app_t::file_http_app_t(std::string _asset_dir)
    : asset_dir(_asset_dir)
{ }

bool ends_with(const std::string& str, const std::string& end) {
    return str.rfind(end) == str.length() - end.length();
}

void file_http_app_t::handle(const http_req_t &req, http_res_t *result, signal_t *) {
    *result = http_res_t(http_status_code_t::METHOD_NOT_ALLOWED);
}

void file_http_app_t::handle_blocking(std::string filename, http_res_t *res_out) {

    // FIXME: make sure that we won't walk out of our sandbox! Check symbolic links, etc.
    blocking_read_file_stream_t stream;
    bool initialized = stream.init((asset_dir + filename).c_str());

    if (!initialized) {
        res_out->code = http_status_code_t::NOT_FOUND;
        return;
    }

    std::vector<char> body;

    for (;;) {
        const int bufsize = 4096;
        char buf[bufsize];
        int64_t res = stream.read(buf, bufsize);

        if (res > 0) {
            body.insert(body.end(), buf, buf + res);
        } else if (res == 0) {
            break;
        } else {
            res_out->code = http_status_code_t::INTERNAL_SERVER_ERROR;
            return;
        }
    }

    res_out->body.assign(body.begin(), body.end());
    res_out->code = http_status_code_t::OK;
}
