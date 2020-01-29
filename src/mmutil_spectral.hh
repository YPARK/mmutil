#include <getopt.h>

#include <random>

#include "io.hh"
#include "mmutil.hh"
#include "mmutil_normalize.hh"
#include "mmutil_stat.hh"
#include "svd.hh"

#ifndef MMUTIL_SPECTRAL_HH_
#define MMUTIL_SPECTRAL_HH_

struct spectral_options_t {
  using Str = std::string;

  spectral_options_t() {

    mtx = "";
    out = "output.txt.gz";

    tau      = 1.0;
    rank     = 50;
    iter     = 5;
    col_norm = 10000;

    raw_scale       = false;
    log_scale       = true;
    row_weight_file = "";

    nystrom_sample = 10000;
    nystrom_batch  = 10000;
  }

  Str mtx;
  Str out;

  Scalar tau;
  Index rank;
  Index iter;
  Scalar col_norm;

  bool raw_scale;
  bool log_scale;
  Str row_weight_file;

  Index nystrom_sample;
  Index nystrom_batch;
};

int
parse_spectral_options(const int argc,      //
                       const char* argv[],  //
                       spectral_options_t& options) {

  const char* _usage =
      "\n"
      "[Arguments]\n"
      "--data (-d)           : MTX file (data)\n"
      "--mtx (-d)            : MTX file (data)\n"
      "--tau (-u)            : Regularization parameter (default: tau = 1)\n"
      "--rank (-r)           : The maximal rank of SVD (default: rank = 50)\n"
      "--iter (-i)           : # of LU iterations (default: iter = 5)\n"
      "--row_weight (-w)     : Feature re-weighting (default: none)\n"
      "--col_norm (-c)       : Column normalization (default: 10000)\n"
      "--nystrom_sample (-S) : Nystrom sample size (default: 10000)\n"
      "--nystrom_batch (-B)  : Nystrom batch size (default: 10000)\n"
      "--log_scale (-L)      : Data in a log-scale (default: true)\n"
      "--raw_scale (-R)      : Data in a raw-scale (default: false)\n"
      "--out (-o)            : Output file name\n"
      "\n"
      "[Details]\n"
      "Qin and Rohe (2013), Regularized Spectral Clustering under "
      "Degree-corrected Stochastic Block Model\n"
      "Li, Kwok, Lu (2010), Making Large-Scale Nystrom Approximation Possible\n"
      "\n";

  const char* const short_opts = "d:m:u:r:i:c:w:S:B:LRho:";

  const option long_opts[] = {
      {"mtx", required_argument, nullptr, 'd'},         //
      {"data", required_argument, nullptr, 'd'},        //
      {"out", required_argument, nullptr, 'o'},         //
      {"tau", required_argument, nullptr, 'u'},         //
      {"rank", required_argument, nullptr, 'r'},        //
      {"iter", required_argument, nullptr, 'i'},        //
      {"row_weight", required_argument, nullptr, 'w'},  //
      {"col_norm", required_argument, nullptr, 'c'},    //
      {"log_scale", no_argument, nullptr, 'L'},         //
      {"raw_scale", no_argument, nullptr, 'R'},         //
      {"help", no_argument, nullptr, 'h'},              //
      {nullptr, no_argument, nullptr, 0}};

  while (true) {
    const auto opt = getopt_long(argc,                      //
                                 const_cast<char**>(argv),  //
                                 short_opts,                //
                                 long_opts,                 //
                                 nullptr);

    if (-1 == opt) break;

    switch (opt) {
      case 'd':
        options.mtx = std::string(optarg);
        break;
      case 'o':
        options.out = std::string(optarg);
        break;
      case 'u':
        options.tau = std::stof(optarg);
        break;
      case 'c':
        options.col_norm = std::stof(optarg);
        break;
      case 'r':
        options.rank = std::stoi(optarg);
        break;
      case 'i':
        options.iter = std::stoi(optarg);
        break;
      case 'w':
        options.row_weight_file = std::string(optarg);
        break;
      case 'S':
        options.nystrom_sample = std::stoi(optarg);
        break;
      case 'B':
        options.nystrom_batch = std::stoi(optarg);
        break;
      case 'L':
        options.log_scale = true;
        options.raw_scale = false;
        break;
      case 'R':
        options.log_scale = false;
        options.raw_scale = true;
        break;

      case 'h':  // -h or --help
      case '?':  // Unrecognized option
        std::cerr << _usage << std::endl;
        return EXIT_FAILURE;
      default:  //
                ;
    }
  }

  return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// Why is this graph Laplacian?
//
// (1) We let adjacency matrix A = X'X assuming elements in X are non-negative
//
//
// (2) Let the Laplacian L = I - D^{-1/2} A D^{-1/2}
//                         = I - D^{-1/2} (X'X) D^{-1/2}
///////////////////////////////////////////////////////////////////////////////

template <typename Derived>
inline Mat
make_scaled_regularized(
    const Eigen::SparseMatrixBase<Derived>& _X0,  // sparse data
    const float tau_scale,                        // regularization
    const bool log_trans = true                   // log-transformation
) {

  const Derived& X0 = _X0.derived();

  using Scalar = typename Derived::Scalar;
  using Index  = typename Derived::Index;
  using Mat    = typename Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

  const Index max_row = X0.rows();

  auto trans_fun = [&log_trans](const Scalar& x) -> Scalar {
    if (x < 0.0) return 0.;
    return log_trans ? fasterlog(x + 1.0) : x;
  };

  const SpMat X = normalize_to_median(X0).unaryExpr(trans_fun);

  TLOG("Constructing a reguarlized graph Laplacian ...");

  const Mat Deg =
      (X.transpose().cwiseProduct(X.transpose()) * Mat::Ones(max_row, 1));
  const Scalar tau = Deg.mean() * tau_scale;

  const Mat denom = Deg.unaryExpr([&tau](const Scalar& x) {
    const Scalar _one = 1.0;
    return _one / std::max(_one, std::sqrt(x + tau));
  });

  Mat ret = denom.asDiagonal() * (X.transpose());
  return ret;  // RVO
}

// Batch-normalized graph Laplacian
// * Apply weights on features (rows; genes) to X matrix.

template <typename Derived, typename Derived2>
inline Mat
make_normalized_laplacian(
    const Eigen::SparseMatrixBase<Derived>& _X0,  // sparse data
    const Eigen::MatrixBase<Derived2>& _weights,  // row weights
    const float tau_scale,                        // regularization
    const float norm_target = 0,                  // normalization
    const bool log_trans    = true                // log-transformation
) {

  const Derived& X0       = _X0.derived();
  const Derived2& weights = _weights.derived();
  const Index max_row     = X0.rows();

  ASSERT(weights.rows() == max_row, "We need weights on each row");
  ASSERT(weights.cols() == 1, "Provide summary vector");

  auto trans_fun = [&log_trans](const Scalar& x) -> Scalar {
    if (x < 0.0) return 0.;
    return log_trans ? fasterlog(x + 1.0) : x;
  };

  SpMat X(X0.rows(), X0.cols());

  if (norm_target > 0.) {
#ifdef DEBUG
    TLOG("Normalized to fixed value: " << norm_target);
#endif
    X = normalize_to_fixed(X0, norm_target).unaryExpr(trans_fun).eval();
  } else {
#ifdef DEBUG
    TLOG("Normalized to median");
#endif
    X = normalize_to_median(X0).unaryExpr(trans_fun).eval();
  }

#ifdef DEBUG
  TLOG("X: " << X.rows() << " x " << X.cols());
#endif

  ////////////////////////////////////////////////////////
  // make X(g,i) <- X(g,i) * min{1/sqrt(weight(g)),  1} //
  ////////////////////////////////////////////////////////

  auto _row_fun = [](const Scalar& x) -> Scalar {
    return x <= 0.0 ? 0.0 : std::sqrt(1.0 / x);
  };

  const Mat _rows_denom = weights.unaryExpr(_row_fun);

#ifdef DEBUG
  TLOG("rows_denom: " << _rows_denom.rows() << " x " << _rows_denom.cols());
#endif

  //////////////////////////////////////////////
  // make X(g,i) <- X(g,i) / sqrt(D(i) + tau) //
  //////////////////////////////////////////////

  const Mat col_deg = X.cwiseProduct(X).transpose() * Mat::Ones(X.rows(), 1);
  const Scalar tau  = col_deg.mean() * tau_scale;

  auto _col_fun = [&tau](const Scalar& x) -> Scalar {
    const Scalar _one = 1.0;
    return _one / std::max(_one, std::sqrt(x + tau));
  };

  const Mat _cols_denom = col_deg.unaryExpr(_col_fun);

  ////////////////////
  // normalize them //
  ////////////////////

  Mat ret =
      (_rows_denom.asDiagonal() * X * _cols_denom.asDiagonal()).transpose();

  return ret;
}

/////////////////////////////////////////////
// 1. construct normalized scaled data     //
// 2. identify eigen spectrum by using SVD //
/////////////////////////////////////////////

template <typename Derived>
inline std::tuple<Mat, Mat, Mat>
take_spectrum_laplacian(                          //
    const Eigen::SparseMatrixBase<Derived>& _X0,  // sparse data
    const float tau_scale,                        // regularization
    const int rank,                               // desired rank
    const int iter = 5                            // should be enough
) {

  const Mat XtTau = make_scaled_regularized(_X0, tau_scale);

  TLOG("Running SVD on X [" << XtTau.rows() << " x " << XtTau.cols() << "]");

  RandomizedSVD<Mat> svd(rank, iter);
  svd.set_verbose();
  svd.compute(XtTau);

  TLOG("Done SVD");

  Mat U = svd.matrixU();
  Mat V = svd.matrixV();
  Vec D = svd.singularValues();

  return std::make_tuple(U, V, D);
}

template <typename Derived>
inline std::tuple<Mat, Mat, Mat>
take_spectrum_nystrom(
    const std::string mtx_file,                  // matrix file
    const Eigen::MatrixBase<Derived>& _weights,  // row weights
    const Scalar tau,                            // regularization
    const Scalar norm,                           // normalization
    const int rank,                              // desired rank
    const int iter         = 5,                  // should be enough
    const Index Nsample    = 10000,              // #selected columns
    const Index batch_size = 10000,              // batch size
    const bool take_ln     = true                // log-transformation
) {

  TLOG("Collecting stats from the matrix file " << mtx_file);

  col_stat_collector_t collector;
  visit_matrix_market_file(mtx_file, collector);
  const Vec& s1      = collector.Col_S1;
  const Vec& s2      = collector.Col_S2;
  const IntVec& nnz_col = collector.Col_N;

  const Index N  = collector.max_col;
  const Index nn = std::min(N, Nsample);

  TLOG("|nnz| = " << nnz_col.size());

  ///////////////////////////////////////
  // step 1 -- random column selection //
  ///////////////////////////////////////

  const Index nnz_tot = nnz_col.sum();

  TLOG("Randomly select " << nn << " columns (N: " << N << ", NNZ: " << nnz_tot
                          << ")");

  std::random_device rd;
  std::mt19937 rgen(rd());

  std::vector<Index> index_r(N);
  std::iota(index_r.begin(), index_r.end(), 0);
  std::shuffle(index_r.begin(), index_r.end(), rgen);

  using _reader_t = eigen_triplet_reader_remapped_cols_t;

  SpMat X;

  {
    _reader_t::index_map_t Remap;
    Index NNZ = 0;
    for (Index new_index = 0; new_index < nn; ++new_index) {
      const Index old_index = index_r.at(new_index);
      Remap[old_index]      = new_index;
      NNZ += nnz_col(old_index);
    }

    _reader_t::TripletVec Tvec;
    _reader_t reader(Tvec, Remap, NNZ);
    visit_matrix_market_file(mtx_file, reader);

    X.resize(reader.max_row, nn);
    X.reserve(Tvec.size());
    X.setFromTriplets(Tvec.begin(), Tvec.end());
  }

  Vec ww(X.rows(), 1);
  ww.setOnes();

  if (_weights.size() > 0) {
    ww == _weights.derived();
  }

  TLOG("Found a stochastic X [" << X.rows() << " x " << X.cols() << "]");

  ////////////////////////////////////////////
  // step 2 -- svd on much smaller examples //
  ////////////////////////////////////////////

  RandomizedSVD<Mat> svd(rank, iter);
  {
    // nn x feature
    Mat xx_t = make_normalized_laplacian(X, ww, tau, norm, take_ln);
    svd.compute(xx_t);
  }

  Mat uu = svd.matrixU();         // nn x rank
  Mat vv = svd.matrixV();         // feature x rank
  Vec dd = svd.singularValues();  // rank x 1

  TLOG("Trained SVD on the matrix X");

  //////////////////////////////////
  // step 3 -- Nystrom projection //
  //////////////////////////////////

  Mat proj = vv * dd.cwiseInverse().asDiagonal();  // feature x rank

  Mat U(N, rank);
  U.setZero();

  for (Index lb = 0; lb < N; lb += batch_size) {

    _reader_t::index_map_t Remap;
    const Index ub = std::min(N, batch_size + lb);

    TLOG("Projection on the batch [" << lb << ", " << ub << ")");

    Index new_index = 0;
    Index NNZ       = 0;

    for (Index old_index = lb; old_index < ub; ++old_index) {
      Remap[old_index] = new_index;
      NNZ += nnz_col(old_index);
      new_index++;
    }

#ifdef DEBUG
    TLOG("Non zeros = " << NNZ << ", " << new_index);
#endif

    _reader_t::TripletVec Tvec;
    _reader_t reader(Tvec, Remap, NNZ);

    visit_matrix_market_file(mtx_file, reader);

    SpMat xx(reader.max_row, ub - lb);

    xx.setFromTriplets(Tvec.begin(), Tvec.end());

    Mat xx_t = make_normalized_laplacian(xx, ww, tau, norm, take_ln);
    Index i = 0;
    for (Index j = lb; j < ub; ++j) {
      U.row(j) += xx_t.row(i++) * proj;
    }
  }

  TLOG("Finished Nystrom Approx.");

  return std::make_tuple(U, vv, dd);
}

#endif
