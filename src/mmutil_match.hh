#include <getopt.h>

#include <unordered_set>

#include "eigen_util.hh"
#include "ext/hnswlib/hnswlib.h"
#include "mmutil.hh"
#include "mmutil_stat.hh"
#include "utils/progress.hh"

#ifndef MMUTIL_MATCH_HH_
#define MMUTIL_MATCH_HH_

struct match_options_t {
  using Str = std::string;

  match_options_t() {
    knn    = 1;
    bilink = 10;
    nlist  = 10;

    src_mtx = "";
    src_col = "";
    tgt_mtx = "";
    tgt_col = "";
    out     = "output.txt.gz";

    tau  = 1.0;
    rank = 50;
    iter = 5;
  }

  Str src_mtx;
  Str src_col;
  Str tgt_mtx;
  Str tgt_col;

  Index knn;
  Index bilink;
  Index nlist;
  Str out;

  Scalar tau;
  Index rank;
  Index iter;
};

int parse_match_options(const int argc,      //
                        const char* argv[],  //
                        match_options_t& mopt) {

  const char* _usage =
      "\n"
      "[Arguments]\n"
      "--src_mtx (-s)  : Source MTX file\n"
      "--src_col (-c)  : Source MTX file\n"
      "--tgt_mtx (-t)  : Target MTX file\n"
      "--tgt_col (-g)  : Target MTX file\n"
      "--tgt_dict (-d) : Target dictionary file\n"
      "--knn (-k)      : K nearest neighbors (default: 1)\n"
      "--bilink (-m)   : # of bidirectional links (default: 10)\n"
      "--nlist (-f)    : # nearest neighbor lists (default: 10)\n"
      "--out (-o)      : Output file name\n"
      "\n"
      "[Arguments for spectral matching]\n"
      "--tau (-u)      : Regularization parameter (default: tau = 1)\n"
      "--rank (-r)     : The maximal rank of SVD (default: rank = 50)\n"
      "--iter (-i)     : # of LU iterations (default: iter = 5)\n"
      "\n"
      "[Details]\n"
      "\n"
      "(M)\n"
      "The number of bi-directional links created for every new element during construction.\n"
      "Reasonable range for M is 2-100. Higher M work better on datasets with high intrinsic\n"
      "dimensionality and/or high recall, while low M work better for datasets with low intrinsic\n"
      "dimensionality and/or low recalls.\n"
      "\n"
      "(N)\n"
      "The size of the dynamic list for the nearest neighbors (used during the search). A higher \n"
      "value leads to more accurate but slower search. This cannot be set lower than the number \n"
      "of queried nearest neighbors k. The value ef of can be anything between k and the size of \n"
      "the dataset.\n"
      "\n"
      "[Reference]\n"
      "Malkov, Yu, and Yashunin. `Efficient and robust approximate nearest neighbor search using\n"
      "Hierarchical Navigable Small World graphs.` preprint: https://arxiv.org/abs/1603.09320\n"
      "\n"
      "See also:\n"
      "https://github.com/nmslib/hnswlib\n"
      "\n";

  const char* const short_opts = "s:c:t:g:k:m:f:o:u:r:i:h";

  const option long_opts[] = {{"src_mtx", required_argument, nullptr, 's'},  //
                              {"src_col", required_argument, nullptr, 'c'},  //
                              {"tgt_mtx", required_argument, nullptr, 't'},  //
                              {"tgt_col", required_argument, nullptr, 'g'},  //
                              {"knn", required_argument, nullptr, 'k'},      //
                              {"bilink", required_argument, nullptr, 'm'},   //
                              {"nlist", required_argument, nullptr, 'f'},    //
                              {"out", required_argument, nullptr, 'o'},      //
                              {"tau", required_argument, nullptr, 'u'},      //
                              {"rank", required_argument, nullptr, 'r'},     //
                              {"iter", required_argument, nullptr, 'i'},     //
                              {"help", no_argument, nullptr, 'h'},           //
                              {nullptr, no_argument, nullptr, 0}};

  while (true) {
    const auto opt = getopt_long(argc,                      //
                                 const_cast<char**>(argv),  //
                                 short_opts,                //
                                 long_opts,                 //
                                 nullptr);

    if (-1 == opt) break;

    switch (opt) {
      case 's':
        mopt.src_mtx = std::string(optarg);
        break;
      case 't':
        mopt.tgt_mtx = std::string(optarg);
        break;
      case 'c':
        mopt.src_col = std::string(optarg);
        break;
      case 'g':
        mopt.tgt_col = std::string(optarg);
        break;
      case 'o':
        mopt.out = std::string(optarg);
        break;
      case 'k':
        mopt.knn = std::stoi(optarg);
        break;
      case 'm':
        mopt.bilink = std::stoi(optarg);
        break;
      case 'n':
        mopt.nlist = std::stoi(optarg);
        break;
      case 'u':
        mopt.tau = std::stof(optarg);
        break;
      case 'r':
        mopt.rank = std::stoi(optarg);
        break;
      case 'i':
        mopt.iter = std::stoi(optarg);
        break;
      case 'h':  // -h or --help
      case '?':  // Unrecognized option
        std::cerr << _usage << std::endl;
        return EXIT_FAILURE;
      default:;
    }
  }

  return EXIT_SUCCESS;
}

/////////////////////////////////
// k-nearest neighbor matching //
/////////////////////////////////

using KnnAlg = hnswlib::HierarchicalNSW<Scalar>;

struct SrcSparseRowsT {
  explicit SrcSparseRowsT(const SpMat& _data) : data(_data){};
  const SpMat& data;
};

struct TgtSparseRowsT {
  explicit TgtSparseRowsT(const SpMat& _data) : data(_data){};
  const SpMat& data;
};

struct KNN {
  explicit KNN(const std::size_t _val) : val(_val) {}
  const std::size_t val;
};

struct BILINK {
  explicit BILINK(const std::size_t _val) : val(_val) {}
  const std::size_t val;
};

struct NNLIST {
  explicit NNLIST(const std::size_t _val) : val(_val) {}
  const std::size_t val;
};

using index_triplet_vec = std::vector<std::tuple<Index, Index, Scalar> >;

struct SrcDataT {
  explicit SrcDataT(const float* _data, const Index d, const Index s)
      : data(_data), vecdim(d), vecsize(s) {}
  const float* data;
  const Index vecdim;
  const Index vecsize;
};

struct TgtDataT {
  explicit TgtDataT(const float* _data, const Index d, const Index s)
      : data(_data), vecdim(d), vecsize(s) {}
  const float* data;
  const Index vecdim;
  const Index vecsize;
};

///////////////////////////////////////////
// search over the rows of sparse matrix //
///////////////////////////////////////////

int search_knn(const SrcSparseRowsT _SrcRows,  //
               const TgtSparseRowsT _TgtRows,  //
               const KNN _knn,                 //
               const BILINK _bilink,           //
               const NNLIST _nnlist,           //
               index_triplet_vec& out);

////////////////////////////////
// search over the dense data //
////////////////////////////////

int search_knn(const SrcDataT _SrcData,  //
               const TgtDataT _TgtData,  //
               const KNN _knn,           //
               const BILINK _bilink,     //
               const NNLIST _nnlist,     //
               index_triplet_vec& out);

////////////////////////////////////////////////////////////////

int search_knn(const SrcSparseRowsT _SrcRows,  //
               const TgtSparseRowsT _TgtRows,  //
               const KNN _knn,                 //
               const BILINK _bilink,           //
               const NNLIST _nnlist,           //
               index_triplet_vec& out) {

  const SpMat& SrcRows = _SrcRows.data;
  const SpMat& TgtRows = _TgtRows.data;

  ERR_RET(TgtRows.cols() != SrcRows.cols(),
          "Target and source data must have the same dimensionality");

  const std::size_t knn          = _knn.val;
  const std::size_t param_bilink = _bilink.val;
  const std::size_t param_nnlist = _nnlist.val;

  const std::size_t vecdim  = TgtRows.cols();
  const std::size_t vecsize = TgtRows.rows();

  ERR_RET(param_bilink >= vecdim, "too big M value");
  ERR_RET(param_bilink < 2, "too small M value");
  ERR_RET(param_nnlist < knn, "too small N value");

  // Construct KnnAlg interface
  hnswlib::InnerProductSpace vecspace(vecdim);
  KnnAlg alg(&vecspace, vecsize, param_bilink, param_nnlist);
  alg.ef_ = param_nnlist;

  TLOG("Initializing kNN algorithm");

  // We need to store target data in the memory (or in the disk)
  std::vector<float> target_data(TgtRows.rows() * TgtRows.cols());
  std::fill(target_data.begin(), target_data.end(), 0.0);

  {
    float* mass = target_data.data();

    progress_bar_t<Index> prog(vecsize, 1e2);

#pragma omp parallel for
    for (Index i = 0; i < TgtRows.outerSize(); ++i) {

#pragma omp criticial
      {
        float norm = 0.0;
        for (SpMat::InnerIterator it(TgtRows, i); it; ++it) {
          float w = it.value();
          norm += w * w;
        }
        norm = std::sqrt(std::max(norm, static_cast<float>(1.0)));

        for (SpMat::InnerIterator it(TgtRows, i); it; ++it) {
          const Index j               = it.col();
          target_data[vecdim * i + j] = it.value() / norm;
        }

        prog.update();
        prog(std::cerr);
      }
      alg.addPoint((void*)(mass + vecdim * i), static_cast<std::size_t>(i));
    }
  }
  ////////////
  // recall //
  ////////////

  TLOG("Finding " << knn << " nearest neighbors");

  {
    progress_bar_t<Index> prog(SrcRows.outerSize(), 1e2);

#pragma omp parallel for
    for (Index i = 0; i < SrcRows.outerSize(); ++i) {

      std::vector<float> lookup(vecdim);
      float* lookup_mass = lookup.data();

#pragma omp critical
      {
        float norm = 0.0;
        std::fill(lookup.begin(), lookup.end(), 0.0);
        for (SpMat::InnerIterator it(SrcRows, i); it; ++it) {
          float w = it.value();
          norm += w * w;
        }
        norm = std::sqrt(std::max(norm, static_cast<float>(1.0)));

        for (SpMat::InnerIterator it(SrcRows, i); it; ++it) {
          float w          = it.value();
          lookup[it.col()] = w / norm;
        }
        prog.update();
        prog(std::cerr);
      }

      auto pq = alg.searchKnn(lookup_mass, knn);
      float d = 0;
      std::size_t j;
      while (!pq.empty()) {
        std::tie(d, j) = pq.top();
        out.push_back(std::make_tuple(i, j, d));
        pq.pop();
      }
    }
  }
  TLOG("Done kNN searches");
  return EXIT_SUCCESS;
}

int search_knn(const SrcDataT _SrcData,  //
               const TgtDataT _TgtData,  //
               const KNN _knn,           //
               const BILINK _bilink,     //
               const NNLIST _nnlist,     //
               index_triplet_vec& out) {

  ERR_RET(_SrcData.vecdim != _TgtData.vecdim,
          "source and target must have the same dimensionality");

  const std::size_t knn          = _knn.val;
  const std::size_t param_bilink = _bilink.val;
  const std::size_t param_nnlist = _nnlist.val;

  const std::size_t vecdim  = _TgtData.vecdim;
  const std::size_t vecsize = _TgtData.vecsize;

  ERR_RET(param_bilink >= vecdim, "too big M value");
  ERR_RET(param_bilink < 2, "too small M value");
  ERR_RET(param_nnlist < knn, "too small N value");

  // Construct KnnAlg interface
  hnswlib::InnerProductSpace vecspace(vecdim);
  KnnAlg alg(&vecspace, vecsize, param_bilink, param_nnlist);
  alg.ef_ = param_nnlist;

  TLOG("Initializing kNN algorithm");

  {
    const float* mass = _TgtData.data;

    progress_bar_t<Index> prog(vecsize, 1e2);

#pragma omp parallel for
    for (Index i = 0; i < vecsize; ++i) {
      alg.addPoint((void*)(mass + vecdim * i), static_cast<std::size_t>(i));
      prog.update();
      prog(std::cerr);
    }
  }

  ////////////
  // recall //
  ////////////

  TLOG("Finding " << knn << " nearest neighbors");

  {
    const float* mass = _SrcData.data;
    progress_bar_t<Index> prog(_SrcData.vecsize, 1e2);

    for (Index i = 0; i < _SrcData.vecsize; ++i) {
      auto pq = alg.searchKnn((void*)(mass + vecdim * i), knn);
      float d = 0;
      std::size_t j;
      while (!pq.empty()) {
        std::tie(d, j) = pq.top();
        out.push_back(std::make_tuple(i, j, d));
        pq.pop();
      }
      prog.update();
      prog(std::cerr);
    }
  }
  TLOG("Done kNN searches");
  return EXIT_SUCCESS;
}

inline std::unordered_set<Index> find_nz_cols(const std::string mtx_file) {
  col_stat_collector_t collector;
  visit_matrix_market_file(mtx_file, collector);
  std::unordered_set<Index> valid;
  const Vec& nn = collector.Col_N;
  for (Index j = 0; j < nn.size(); ++j) {
    if (nn(j) > 0.0) valid.insert(j);
  }
  return valid;  // RVO
}

template <typename TVec, typename SVec>
auto build_knn_named(const TVec& out_index,      //
                     const SVec& col_src_names,  //
                     const SVec& col_tgt_names) {

  using RET = std::vector<std::tuple<std::string, std::string, Scalar> >;

  RET out_named;
  out_named.reserve(out_index.size());

  for (auto tt : out_index) {
    Index i, j;
    Scalar d;
    std::tie(i, j, d) = tt;
    out_named.push_back(std::make_tuple(col_src_names.at(i), col_tgt_names.at(j), d));
  }

  return out_named;
}

template <typename TVec, typename SVec, typename VVec>
auto build_knn_named(const TVec& out_index,      //
                     const SVec& col_src_names,  //
                     const SVec& col_tgt_names,  //
                     const VVec& valid_src,      //
                     const VVec& valid_tgt) {

  using RET = std::vector<std::tuple<std::string, std::string, Scalar> >;

  RET out_named;
  out_named.reserve(out_index.size());

  for (auto tt : out_index) {
    Index i, j;
    Scalar d;
    std::tie(i, j, d) = tt;
    if (valid_src.count(i) > 0 && valid_tgt.count(j) > 0) {
      out_named.push_back(std::make_tuple(col_src_names.at(i), col_tgt_names.at(j), d));
    }
  }

  return out_named;
}

#endif
