// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef PERFMON_FILTER_HPP_
#define PERFMON_FILTER_HPP_

#include <set>
#include <string>
#include "containers/vector.hpp"

#include <re2/re2.h>

#include "errors.hpp"

#include "rdb_protocol/datum.hpp"

template <class> class scoped_ptr_t;

class perfmon_filter_t {
public:
    explicit perfmon_filter_t(const std::set<vector_t<std::string> > &paths);
    ql::datum_t filter(const ql::datum_t &stats) const;
private:
    ql::datum_t subfilter(const ql::datum_t &stats,
                          size_t depth, vector_t<bool> active) const;
    vector_t<vector_t<scoped_ptr_t<RE2> > > regexps; //regexps[PATH][DEPTH]
    DISABLE_COPYING(perfmon_filter_t);
};

#endif  // PERFMON_FILTER_HPP_
