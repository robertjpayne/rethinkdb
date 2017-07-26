// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef BTREE_GET_DISTRIBUTION_HPP_
#define BTREE_GET_DISTRIBUTION_HPP_

#include "containers/vector.hpp"

#include "btree/keys.hpp"
#include "buffer_cache/types.hpp"

class superblock_t;

void get_btree_key_distribution(superblock_t *superblock, int depth_limit,
                                int64_t *key_count_out,
                                vector_t<store_key_t> *keys_out);

#endif /* BTREE_GET_DISTRIBUTION_HPP_ */
