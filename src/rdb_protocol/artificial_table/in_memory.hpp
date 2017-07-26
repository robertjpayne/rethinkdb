// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef RDB_PROTOCOL_ARTIFICIAL_TABLE_IN_MEMORY_HPP_
#define RDB_PROTOCOL_ARTIFICIAL_TABLE_IN_MEMORY_HPP_

#include <map>
#include <string>
#include "containers/vector.hpp"

#include "containers/archive/archive.hpp"
#include "rdb_protocol/artificial_table/backend.hpp"
#include "rdb_protocol/artificial_table/caching_cfeed_backend.hpp"
#include "rdb_protocol/datum.hpp"
#include "rdb_protocol/serialize_datum.hpp"

/* This is the backend for an artificial table that acts as much as possible like a real
table. It accepts all reads and writes, storing the results in a `std::map`. It's used
for testing `artificial_table_t`. */

class in_memory_artificial_table_backend_t :
    public caching_cfeed_artificial_table_backend_t
{
public:
    in_memory_artificial_table_backend_t(
            name_string_t const &table_name,
            rdb_context_t *rdb_context,
            lifetime_t<name_resolver_t const &> name_resolver)
        : caching_cfeed_artificial_table_backend_t(
            table_name, rdb_context, name_resolver) {
    }

    ~in_memory_artificial_table_backend_t() {
        begin_changefeed_destruction();
    }

    std::string get_primary_key_name() {
        return "id";
    }

    bool read_all_rows_as_vector(
            auth::user_context_t const &user_context,
            signal_t *interruptor,
            vector_t<ql::datum_t> *rows_out,
            UNUSED admin_err_t *error_out) {
        random_delay(interruptor);
        on_thread_t thread_switcher(home_thread());

        user_context.require_admin_user();

        rows_out->clear();
        for (auto const &item : data) {
            rows_out->push_back(item.second);
        }
        return true;
    }

    bool read_row(
            auth::user_context_t const &user_context,
            ql::datum_t primary_key,
            signal_t *interruptor,
            ql::datum_t *row_out,
            UNUSED admin_err_t *error_out) {
        random_delay(interruptor);
        on_thread_t thread_switcher(home_thread());

        user_context.require_admin_user();

        auto it = data.find(primary_key.print_primary());
        if (it != data.end()) {
            *row_out = it->second;
        } else {
            *row_out = ql::datum_t();
        }
        return true;
    }

    bool write_row(
            auth::user_context_t const &user_context,
            ql::datum_t primary_key,
            UNUSED bool pkey_was_autogenerated,
            ql::datum_t *new_value_inout,
            signal_t *interruptor,
            UNUSED admin_err_t *error_out) {
        random_delay(interruptor);
        on_thread_t thread_switcher(home_thread());

        user_context.require_admin_user();

        if (new_value_inout->has()) {
            /* Not all datums can be serialized into an actual table (r.minval,
            r.maxval and large arrays in particular). To make the in-memory test
            table behave as closely to an actual table as possible, we attempt to
            serialize the datum, check for errors, and then discard the serialization
            result. */
            {
                write_message_t wm;
                ql::serialization_result_t res = ql::datum_serialize(
                    &wm,
                    *new_value_inout,
                    ql::check_datum_serialization_errors_t::YES);
                if (res & ql::serialization_result_t::ARRAY_TOO_BIG) {
                    rfail_typed_target(new_value_inout, "Array too large for disk "
                                       "writes (limit 100,000 elements).");
                } else if (res & ql::serialization_result_t::EXTREMA_PRESENT) {
                    rfail_typed_target(new_value_inout, "`r.minval` and `r.maxval` "
                                       "cannot be written to disk.");
                }
                r_sanity_check(!ql::bad(res));
            }

            ql::datum_t primary_key_2 = new_value_inout->get_field("id", ql::NOTHROW);
            guarantee(primary_key_2.has());
            guarantee(primary_key == primary_key_2);
            data[primary_key.print_primary()] = *new_value_inout;
        } else {
            data.erase(primary_key.print_primary());
        }
        notify_row(primary_key);
        return true;
    }

private:
    /* The purpose of `random_delay()` is to mix things up a bit to increase the
    likelihood of exposing a bug in `artificial_table_t`. */
    void random_delay(signal_t *interruptor) {
        if (randint(2) == 0) {
            signal_timer_t timer;
            timer.start(randint(100));
            wait_interruptible(&timer, interruptor);
        }
    }

    std::map<std::string, ql::datum_t> data;
};

#endif /* RDB_PROTOCOL_ARTIFICIAL_TABLE_IN_MEMORY_HPP_ */

