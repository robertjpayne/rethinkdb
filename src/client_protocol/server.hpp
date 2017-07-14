// Copyright 2010-2015 RethinkDB, all rights reserved.
#ifndef CLIENT_PROTOCOL_SERVER_HPP_
#define CLIENT_PROTOCOL_SERVER_HPP_

#include <set>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "arch/address.hpp"
#include "arch/io/openssl.hpp"
#include "arch/runtime/runtime.hpp"
#include "arch/timing.hpp"
#include "concurrency/auto_drainer.hpp"
#include "concurrency/cross_thread_signal.hpp"
#include "containers/archive/archive.hpp"
#include "containers/archive/tcp_conn_stream.hpp"
#include "containers/counted.hpp"
#include "perfmon/perfmon.hpp"
#include "utils.hpp"

class auth_key_t;

class rdb_context_t;
namespace ql {
class query_params_t;
class query_cache_t;
class response_t;
}

class new_semaphore_in_line_t;
class query_handler_t {
public:
    virtual ~query_handler_t() { }

    virtual void run_query(ql::query_params_t *query_params,
                           ql::response_t *response_out,
                           signal_t *interruptor) = 0;
};

class query_server_t {
public:
    query_server_t(
        rdb_context_t *rdb_ctx,
        const std::set<ip_address_t> &local_addresses,
        int port,
        query_handler_t *_handler,
        tls_ctx_t* tls_ctx);
    ~query_server_t();

    int get_port() const;

private:
    void make_error_response(bool is_draining,
                             const tcp_conn_t &conn,
                             const std::string &err,
                             ql::response_t *response_out);

    // For the client driver socket
    void handle_conn(const scoped_ptr_t<tcp_conn_descriptor_t> &nconn,
                     auto_drainer_t::lock_t);

    // This is templatized based on the wire protocol requested by the client
    template<class protocol_t>
    void connection_loop(tcp_conn_t *conn,
                         size_t max_concurrent_queries,
                         ql::query_cache_t *query_cache,
                         signal_t *interruptor);

    tls_ctx_t *tls_ctx;
    rdb_context_t *const rdb_ctx;
    query_handler_t *const handler;

    /* WARNING: The order here is fragile. */
    auto_drainer_t drainer;
    scoped_ptr_t<tcp_listener_t> tcp_listener;

    int next_thread;
};

#endif /* CLIENT_PROTOCOL_SERVER_HPP_ */
