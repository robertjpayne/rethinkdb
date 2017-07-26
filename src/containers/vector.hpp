// Copyright 2010-2013 RethinkDB, all rights reserved.
#ifndef CONTAINERS_VECTOR_HPP_
#define CONTAINERS_VECTOR_HPP_

#ifndef USE_FOLLY
#define USE_FOLLY 1
#endif

#if USE_FOLLY
#include <folly/FBVector.h>
template <typename T>
using vector_t = folly::fbvector<T>;
#else
#include <vector>
template <typename T>
using vector_t = std::vector<T>;
#endif

#endif  // CONTAINERS_VECTOR_HPP_
