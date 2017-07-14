// Copyright 2010-2015 RethinkDB, all rights reserved.

// We need to include `openssl/evp.h` first, since it declares a function with the
// name `final`.
// Because of missing support for the `final` annotation in older GCC versions,
// we redefine final to the empty string in `errors.hpp`. So we must make
// sure that we haven't included `errors.hpp` by the time we include `evp.h`.
#include <openssl/evp.h> // NOLINT(build/include_order)

#include "client_protocol/server.hpp" // NOLINT(build/include_order)

#include <google/protobuf/stubs/common.h> // NOLINT(build/include_order)

#include <array> // NOLINT(build/include_order)
#include <random> // NOLINT(build/include_order)
#include <set> // NOLINT(build/include_order)
#include <string> // NOLINT(build/include_order)

#include "arch/arch.hpp"
#include "arch/io/network.hpp"
#include "client_protocol/client_server_error.hpp"
#include "client_protocol/protocols.hpp"
#include "clustering/administration/auth/authentication_error.hpp"
#include "clustering/administration/auth/plaintext_authenticator.hpp"
#include "clustering/administration/auth/scram_authenticator.hpp"
#include "clustering/administration/auth/username.hpp"
#include "clustering/administration/metadata.hpp"
#include "concurrency/coro_pool.hpp"
#include "concurrency/cross_thread_signal.hpp"
#include "concurrency/queue/limited_fifo.hpp"
#include "crypto/error.hpp"
#include "perfmon/perfmon.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rdb_protocol/rdb_backtrace.hpp"
#include "rdb_protocol/base64.hpp"
#include "rdb_protocol/env.hpp"
#include "rpc/semilattice/view.hpp"
#include "time.hpp"

#include "rdb_protocol/ql2.pb.h"
#include "rdb_protocol/query_server.hpp"
#include "rdb_protocol/query_cache.hpp"
#include "rdb_protocol/response.hpp"

query_server_t::query_server_t(rdb_context_t *_rdb_ctx,
                               const std::set<ip_address_t> &local_addresses,
                               int port,
                               query_handler_t *_handler,
                               tls_ctx_t *_tls_ctx) :
        tls_ctx(_tls_ctx),
        rdb_ctx(_rdb_ctx),
        handler(_handler),
        next_thread(0) {
    rassert(rdb_ctx != nullptr);
    try {
        tcp_listener.init(new tcp_listener_t(local_addresses, port,
            std::bind(&query_server_t::handle_conn,
                      this, ph::_1, auto_drainer_t::lock_t(&drainer))));
    } catch (const address_in_use_exc_t &ex) {
        throw address_in_use_exc_t(
            strprintf("Could not bind to RDB protocol port: %s", ex.what()));
    }
}

query_server_t::~query_server_t() { }

int query_server_t::get_port() const {
    return tcp_listener->get_port();
}

void write_datum(tcp_conn_t *connection, ql::datum_t datum, signal_t *interruptor) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    datum.write_json(&writer);
    buffer.Put('\0');
    connection->write(buffer.GetString(), buffer.GetSize(), interruptor);
}

ql::datum_t read_datum(tcp_conn_t *connection, signal_t *interruptor) {
    std::array<char, 2048> buffer;

    std::size_t offset = 0;
    do {
        connection->read_buffered(buffer.data() + offset, 1, interruptor);
    } while (buffer[offset++] != '\0' && offset < buffer.max_size());
    if (offset == 2048) {
        throw client_protocol::client_server_error_t(7, "Limited read buffer size.");
    }

    rapidjson::Document document;
    document.Parse(buffer.data());
    if (document.HasParseError()) {
        throw client_protocol::client_server_error_t(8, "Invalid JSON object.");
    }

    try {
        return ql::to_datum(
            document,
            ql::configured_limits_t::unlimited,
            reql_version_t::LATEST);
    } catch (ql::base_exc_t const &) {
        throw client_protocol::client_server_error_t(9, "Failed to convert JSON to datum.");
    }
}

void query_server_t::handle_conn(const scoped_ptr_t<tcp_conn_descriptor_t> &nconn,
                                 auto_drainer_t::lock_t keepalive) {
    threadnum_t chosen_thread = threadnum_t(next_thread);
    next_thread = (next_thread + 1) % get_num_db_threads();

    cross_thread_signal_t ct_keepalive(keepalive.get_drain_signal(), chosen_thread);
    on_thread_t rethreader(chosen_thread);

    scoped_ptr_t<tcp_conn_t> conn;

    try {
        nconn->make_server_connection(tls_ctx, &conn, &ct_keepalive);
    } catch (const interrupted_exc_t &) {
        // TLS handshake was interrupted.
        return;
    } catch (const crypto::openssl_error_t &err) {
        // TLS handshake failed.
        logWRN("Driver connection TLS handshake failed: %s", err.what());
        return;
    }

    uint8_t version = 0;
    std::unique_ptr<auth::base_authenticator_t> authenticator;
    uint32_t error_code = 0;
    std::string error_message;
    try {
        conn->enable_keepalive();

        int32_t client_magic_number;
        conn->read_buffered(
            &client_magic_number, sizeof(client_magic_number), &ct_keepalive);
#ifdef __s390x__
        client_magic_number = __builtin_bswap32(client_magic_number);
#endif

        switch (client_magic_number) {
            case VersionDummy::V0_1:
                version = 1;
                break;
            case VersionDummy::V0_2:
                version = 2;
                break;
            case VersionDummy::V0_3:
                version = 3;
                break;
            case VersionDummy::V0_4:
                version = 4;
                break;
            case VersionDummy::V1_0:
                version = 10;
                break;
            default:
                throw client_protocol::client_server_error_t(
                    -1,
                    "Received an unsupported protocol version. This port is for "
                    "RethinkDB queries. Does your client driver version not match the "
                    "server?");
        }
        if (version < 3) {
            // `V0_1` and `V0_2` only supported the PROTOBUF protocol
            throw client_protocol::client_server_error_t(
                -1, "The PROTOBUF client protocol is no longer supported");
        } else if (version < 10) {
            // We'll get std::make_unique in C++14
            authenticator.reset(
                new auth::plaintext_authenticator_t(rdb_ctx->get_auth_watchable()));

            uint32_t auth_key_size;
            conn->read_buffered(&auth_key_size, sizeof(uint32_t), &ct_keepalive);
#ifdef __s390x__
            auth_key_size = __builtin_bswap32(auth_key_size);
#endif
            if (auth_key_size > 2048) {
                throw client_protocol::client_server_error_t(
                    -1, "Client provided an authorization key that is too long.");
            }

            scoped_array_t<char> auth_key_buffer(auth_key_size);
            conn->read_buffered(auth_key_buffer.data(), auth_key_size, &ct_keepalive);

            try {
                authenticator->next_message(
                    std::string(auth_key_buffer.data(), auth_key_size));
            } catch (auth::authentication_error_t const &) {
                // Note we must not change this message for backwards compatibility
                throw client_protocol::client_server_error_t(
                    -1, "Incorrect authorization key.");
            }

            int32_t wire_protocol;
            conn->read_buffered(&wire_protocol, sizeof(wire_protocol), &ct_keepalive);
#ifdef __s390x__
            wire_protocol = __builtin_bswap32(wire_protocol);
#endif
            switch (wire_protocol) {
                case VersionDummy::JSON:
                    break;
                case VersionDummy::PROTOBUF:
                    throw client_protocol::client_server_error_t(
                        -1, "The PROTOBUF client protocol is no longer supported");
                    break;
                default:
                    throw client_protocol::client_server_error_t(
                        -1,
                        strprintf(
                            "Unrecognized protocol specified: '%d'", wire_protocol));
            }

            char const *success_msg = "SUCCESS";
            conn->write(success_msg, strlen(success_msg) + 1, &ct_keepalive);
        } else {
            authenticator.reset(
                new auth::scram_authenticator_t(rdb_ctx->get_auth_watchable()));

            {
                ql::datum_object_builder_t datum_object_builder;
                datum_object_builder.overwrite("success", ql::datum_t::boolean(true));
                datum_object_builder.overwrite("max_protocol_version", ql::datum_t(0.0));
                datum_object_builder.overwrite("min_protocol_version", ql::datum_t(0.0));
                datum_object_builder.overwrite(
                    "server_version", ql::datum_t(RETHINKDB_VERSION));

                write_datum(
                    conn.get(),
                    std::move(datum_object_builder).to_datum(),
                    &ct_keepalive);
            }

            {
                ql::datum_t datum = read_datum(conn.get(), &ct_keepalive);

                ql::datum_t protocol_version =
                    datum.get_field("protocol_version", ql::NOTHROW);
                if (protocol_version.get_type() != ql::datum_t::R_NUM) {
                    throw client_protocol::client_server_error_t(
                        1, "Expected a number for `protocol_version`.");
                }
                if (protocol_version.as_num() != 0.0) {
                    throw client_protocol::client_server_error_t(
                        2, "Unsupported `protocol_version`.");
                }

                ql::datum_t authentication_method =
                    datum.get_field("authentication_method", ql::NOTHROW);
                if (authentication_method.get_type() != ql::datum_t::R_STR) {
                    throw client_protocol::client_server_error_t(
                        3, "Expected a string for `authentication_method`.");
                }
                if (authentication_method.as_str() != "SCRAM-SHA-256") {
                    throw client_protocol::client_server_error_t(
                        4, "Unsupported `authentication_method`.");
                }

                ql::datum_t authentication =
                    datum.get_field("authentication", ql::NOTHROW);
                if (authentication.get_type() != ql::datum_t::R_STR) {
                    throw client_protocol::client_server_error_t(
                        5, "Expected a string for `authentication`.");
                }

                ql::datum_object_builder_t datum_object_builder;
                datum_object_builder.overwrite("success", ql::datum_t::boolean(true));
                datum_object_builder.overwrite(
                    "authentication",
                    ql::datum_t(
                        authenticator->next_message(authentication.as_str().to_std())));

                write_datum(
                    conn.get(),
                    std::move(datum_object_builder).to_datum(),
                    &ct_keepalive);
            }

            {
                ql::datum_t datum = read_datum(conn.get(), &ct_keepalive);

                ql::datum_t authentication =
                    datum.get_field("authentication", ql::NOTHROW);
                if (authentication.get_type() != ql::datum_t::R_STR) {
                    throw client_protocol::client_server_error_t(
                        5, "Expected a string for `authentication`.");
                }

                ql::datum_object_builder_t datum_object_builder;
                datum_object_builder.overwrite("success", ql::datum_t::boolean(true));
                datum_object_builder.overwrite(
                    "authentication",
                    ql::datum_t(
                        authenticator->next_message(authentication.as_str().to_std())));

                write_datum(
                    conn.get(),
                    std::move(datum_object_builder).to_datum(),
                    &ct_keepalive);
            }
        }

        ip_and_port_t client_addr_port(ip_address_t::any(AF_INET), port_t(0));
        UNUSED bool peer_res = conn->getpeername(&client_addr_port);

        guarantee(authenticator != nullptr);
        ql::query_cache_t query_cache(
            rdb_ctx,
            client_addr_port,
            (version < 4)
                ? ql::return_empty_normal_batches_t::YES
                : ql::return_empty_normal_batches_t::NO,
            auth::user_context_t(authenticator->get_authenticated_username()));

        connection_loop<json_protocol_t>(
            conn.get(),
            (version < 4)
                ? 1
                : 1024,
            &query_cache,
            &ct_keepalive);
    } catch (client_protocol::client_server_error_t const &error) {
        // We can't write the response here due to coroutine switching inside an
        // exception handler
        error_code = error.get_error_code();
        error_message = error.what();
    } catch (interrupted_exc_t const &) {
        // If we have been interrupted, we can't write a message to the client, as that
        // may block (and we would just be interrupted again anyway), just close.
    } catch (auth::authentication_error_t const &error) {
        // Note these have error codes 10 to 20
        error_code = error.get_error_code();
        error_message = error.what();
    } catch (crypto::error_t const &error) {
        error_code = 21;
        error_message = error.what();
    } catch (crypto::openssl_error_t const &error) {
        error_code = 22;
        error_message = error.code().message();
    } catch (const tcp_conn_read_closed_exc_t &) {
    } catch (const tcp_conn_write_closed_exc_t &) {
    } catch (const std::exception &ex) {
        logERR("Unexpected exception in client handler: %s", ex.what());
    }

    if (!error_message.empty()) {
        try {
            if (version < 10) {
                std::string error = "ERROR: " + error_message + "\n";
                conn->write(error.c_str(), error.length() + 1, &ct_keepalive);
            } else {
                ql::datum_object_builder_t datum_object_builder;
                datum_object_builder.overwrite("success", ql::datum_t::boolean(false));
                datum_object_builder.overwrite("error", ql::datum_t(error_message));
                datum_object_builder.overwrite(
                    "error_code", ql::datum_t(static_cast<double>(error_code)));

                write_datum(
                    conn.get(),
                    std::move(datum_object_builder).to_datum(),
                    &ct_keepalive);
            }

            conn->shutdown_write();
        } catch (client_protocol::client_server_error_t const &error) {
        } catch (interrupted_exc_t const &) {
        } catch (const tcp_conn_write_closed_exc_t &) {
            // Writing the error message failed, there is nothing we can do at this point
        }
    }
}

void query_server_t::make_error_response(bool is_draining,
                                         const tcp_conn_t &conn,
                                         const std::string &err_str,
                                         ql::response_t *response_out) {
    // Best guess at the error that occurred.
    if (!conn.is_write_open()) {
        // The other side closed it's socket - it won't get this message
        response_out->fill_error(Response::RUNTIME_ERROR,
                                 Response::OP_INDETERMINATE,
                                 "Client closed the connection.",
                                 ql::backtrace_registry_t::EMPTY_BACKTRACE);
    } else if (is_draining) {
        // The query_server_t is being destroyed so this won't actually be written
        response_out->fill_error(Response::RUNTIME_ERROR,
                                 Response::OP_INDETERMINATE,
                                 "Server is shutting down.",
                                 ql::backtrace_registry_t::EMPTY_BACKTRACE);
    } else {
        // Sort of a catch-all - there could be other reasons for this
        response_out->fill_error(
            Response::RUNTIME_ERROR, Response::OP_INDETERMINATE,
            strprintf("Fatal error on another query: %s", err_str.c_str()),
            ql::backtrace_registry_t::EMPTY_BACKTRACE);
    }
}

template <class Callable>
void save_exception(std::exception_ptr *err,
                    std::string *err_str,
                    cond_t *abort,
                    Callable &&fn) {
    try {
        fn();
    } catch (const std::exception &ex) {
        if (!(*err)) {
            *err = std::current_exception();
            err_str->assign(ex.what());
        }
        abort->pulse_if_not_already_pulsed();
    }
}

template <class protocol_t>
void query_server_t::connection_loop(tcp_conn_t *conn,
                                     size_t max_concurrent_queries,
                                     ql::query_cache_t *query_cache,
                                     signal_t *drain_signal) {
    std::exception_ptr err;
    std::string err_str;
    cond_t abort;
    new_mutex_t send_mutex;
    scoped_perfmon_counter_t connection_counter(&rdb_ctx->stats.client_connections);

#ifdef __linux
    linux_event_watcher_t *ew = conn->get_event_watcher();
    linux_event_watcher_t::watch_t conn_interrupted(ew, poll_event_rdhup);
    wait_any_t interruptor(drain_signal, &abort, &conn_interrupted);
#else
    wait_any_t interruptor(drain_signal, &abort);
#endif  // __linux

    new_semaphore_t sem(max_concurrent_queries);
    auto_drainer_t coro_drainer;
    while (!err) {
        scoped_ptr_t<ql::query_params_t> outer_query =
            protocol_t::parse_query(conn, &interruptor, query_cache);
        if (outer_query.has()) {
            outer_query->throttler.init(&sem, 1);
            wait_interruptible(outer_query->throttler.acquisition_signal(),
                               &interruptor);
            coro_t::spawn_now_dangerously([&]() {
                // We grab this right away while it's still valid.
                scoped_ptr_t<ql::query_params_t> query = std::move(outer_query);
                // Since we `spawn_now_dangerously` it's always safe to acquire this.
                auto_drainer_t::lock_t coro_drainer_lock(&coro_drainer);
                wait_any_t cb_interruptor(coro_drainer_lock.get_drain_signal(),
                                          &interruptor);
                ql::response_t response;
                bool replied = false;

                save_exception(&err, &err_str, &abort, [&]() {
                    handler->run_query(query.get(), &response, &cb_interruptor);
                    if (!query->noreply) {
                        new_mutex_acq_t send_lock(&send_mutex, &cb_interruptor);
                        protocol_t::send_response(&response, query->token,
                                                  conn, &cb_interruptor);
                        replied = true;
                    }
                });
                save_exception(&err, &err_str, &abort, [&]() {
                    if (!replied && !query->noreply) {
                        make_error_response(drain_signal->is_pulsed(), *conn,
                                            err_str, &response);
                        new_mutex_acq_t send_lock(&send_mutex, drain_signal);
                        protocol_t::send_response(&response, query->token,
                                                  conn, &cb_interruptor);
                    }
                });
            });
            guarantee(!outer_query.has());
            // Since we're using `spawn_now_dangerously` above, we need to yield
            // here to stop a client sending a constant stream of expensive queries
            // from stalling the thread.
            coro_t::yield();
        }
    }

    if (err) {
        std::rethrow_exception(err);
    }
}
