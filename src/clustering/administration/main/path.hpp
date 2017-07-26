#ifndef CLUSTERING_ADMINISTRATION_MAIN_PATH_HPP_
#define CLUSTERING_ADMINISTRATION_MAIN_PATH_HPP_

#include <string>
#include "containers/vector.hpp"

struct path_t {
    vector_t<std::string> nodes;
    bool is_absolute;
};

path_t parse_as_path(const std::string &);
std::string render_as_path(const path_t &);

#endif  // CLUSTERING_ADMINISTRATION_MAIN_PATH_HPP_
