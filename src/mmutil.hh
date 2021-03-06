// #include <boost/graph/adjacency_list.hpp>
// #include <boost/graph/connected_components.hpp>
// #include <boost/lexical_cast.hpp>

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Sparse>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

#include "io.hh"
#include "io_visitor.hh"
#include "utils/eigen_util.hh"
#include "utils/std_util.hh"
#include "utils/math.hh"
#include "utils/util.hh"
#include "utils/check.hh"

#ifndef MMUTIL_HH_
#define MMUTIL_HH_

using Scalar = float;
using SpMat = Eigen::SparseMatrix<Scalar, Eigen::RowMajor, std::ptrdiff_t>;
using Index = SpMat::Index;

using Mat = typename Eigen::
    Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;
using Vec = typename Eigen::Matrix<Scalar, Eigen::Dynamic, 1>;
using IntMat = typename Eigen::
    Matrix<std::ptrdiff_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;
using IntVec = typename Eigen::Matrix<std::ptrdiff_t, Eigen::Dynamic, 1>;

inline std::tuple<Index, Index, Scalar>
parse_triplet(const std::tuple<Index, Index, Scalar> &tt)
{
    return tt;
}

inline std::tuple<Index, Index, Scalar>
parse_triplet(const Eigen::Triplet<Scalar> &tt)
{
    return std::make_tuple(tt.row(), tt.col(), tt.value());
}

#endif
