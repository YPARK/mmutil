#include <algorithm>
#include <functional>
#include <unordered_map>
#include <tuple>
#include <string>
#include <sstream>
#include <vector>
#include <random>

// #include <execution> c++17

#ifndef STD_UTIL_HH_
#define STD_UTIL_HH_

char *
str2char(const std::string &s)
{
    char *ret = new char[s.size() + 1];
    std::strcpy(ret, s.c_str());
    return ret;
}

std::vector<std::string>
split(const std::string &s, char delim)
{
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delim)) {
        elems.push_back(std::move(item));
    }
    return elems;
}

template <typename Vec>
auto
std_argsort(const Vec &data)
{
    using Index = std::ptrdiff_t;
    std::vector<Index> index(data.size());
    std::iota(std::begin(index), std::end(index), 0);
    std::sort(std::begin(index), std::end(index), [&](Index lhs, Index rhs) {
        return data.at(lhs) > data.at(rhs);
    });
    return index;
}

/**
 * vector -> map: name -> position index
 */
template <typename S, typename IDX>
std::unordered_map<S, IDX>
make_position_dict(const std::vector<S> &name_vec)
{

    std::unordered_map<S, IDX> name_to_id;

    for (IDX i = 0; i < name_vec.size(); ++i) {
        const S &j = name_vec.at(i);
        name_to_id[j] = i;
    }

    return name_to_id;
}

template <typename S, typename IDX>
std::tuple<std::vector<IDX>, std::vector<S>, std::unordered_map<S, IDX>>
make_indexed_vector(const std::vector<S> &name_vec)
{

    std::unordered_map<S, IDX> name_to_id;
    std::vector<S> id_to_name;
    std::vector<IDX> id_vec;
    id_vec.reserve(name_vec.size());

    for (IDX i = 0; i < name_vec.size(); ++i) {
        const S &ii = name_vec.at(i);
        if (name_to_id.count(ii) == 0) {
            const IDX j = name_to_id.size();
            name_to_id[ii] = j;
            id_to_name.push_back(ii);
        }
        id_vec.emplace_back(name_to_id.at(ii));
    }

    return std::make_tuple(id_vec, id_to_name, name_to_id);
}

template <typename IDX>
std::vector<std::vector<IDX>>
make_index_vec_vec(const std::vector<IDX> &_id)
{
    using vec_ivec = std::vector<std::vector<IDX>>;

    const IDX nn = *std::max_element(_id.begin(), _id.end()) + 1;

    vec_ivec ret(nn, std::vector<IDX> {});

    for (IDX i = 0; i < _id.size(); ++i) {
        const IDX k = _id.at(i);
        ret[k].push_back(i);
    }
    return ret;
}

template<typename Iter, typename RandomGenerator>
Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(g));
    return start;
}

template<typename Iter>
Iter select_randomly(Iter start, Iter end) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return select_randomly(start, end, gen);
}

// template <typename Vec>
// auto
// std_argsort_par(const Vec& data) {
//   using Index = std::ptrdiff_t;
//   std::vector<Index> index(data.size());
//   std::iota(std::begin(index), std::end(index), 0);
//   std::sort(std::execution::par, std::begin(index), std::end(index),
//             [&](Index lhs, Index rhs) { return data.at(lhs) > data.at(rhs);
//             });
//   return index;
// }

#endif
