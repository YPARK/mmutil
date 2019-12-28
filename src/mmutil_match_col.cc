#include "mmutil_match.hh"

void print_help(const char* fname) {

  const char* _desc =
      "[Arguments]\n"
      "SRC_MTX	:    Source MTX file\n"
      "SRC_COL	:    Source MTX file\n"
      "TGT_MTX	:    Target MTX file\n"
      "TGT_COL	:    Target MTX file\n"
      "K	:    K nearest neighbors\n"
      "M	:    # of bidirectional links\n"
      "\n"
      "The number of bi-directional links created for every new element during construction.\n"
      "Reasonable range for M is 2-100. Higher M work better on datasets with high intrinsic\n"
      "dimensionality and/or high recall, while low M work better for datasets with low intrinsic\n"
      "dimensionality and/or low recalls.\n"
      "\n"
      "N	:    # nearest neighbor lists\n"
      "\n"
      "The size of the dynamic list for the nearest neighbors (used during the search). A higher \n"
      "value leads to more accurate but slower search. This cannot be set lower than the number \n"
      "of queried nearest neighbors k. The value ef of can be anything between k and the size of \n"
      "the dataset.\n"
      "\n"
      "OUTPUT	:    Output file header\n"
      "\n"
      "[Reference]\n"
      "Malkov, Yu, and Yashunin. `Efficient and robust approximate nearest neighbor search using\n"
      "Hierarchical Navigable Small World graphs.` preprint: https://arxiv.org/abs/1603.09320\n"
      "\n"
      "See also:\n"
      "https://github.com/nmslib/hnswlib\n"
      "\n";

  std::cerr << "Find k-nearest neighbors of the source columns among the target data." << std::endl;
  std::cerr << std::endl;
  std::cerr << fname << " SRC_MTX SRC_COL TGT_MTX TGT_COL K M N OUTPUT" << std::endl;
  std::cerr << std::endl;
  std::cerr << _desc << std::endl;
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

int main(const int argc, const char* argv[]) {

  if (argc < 9) {
    print_help(argv[0]);
    return EXIT_FAILURE;
  }

  const std::string mtx_src_file(argv[1]);
  const std::string col_src_file(argv[2]);
  const std::string mtx_tgt_file(argv[3]);
  const std::string col_tgt_file(argv[4]);

  std::vector<std::string> col_src_names;
  CHECK(read_vector_file(col_src_file, col_src_names));

  std::vector<std::string> col_tgt_names;
  CHECK(read_vector_file(col_tgt_file, col_tgt_names));

  std::vector<std::tuple<Index, Index, Scalar> > out_index;

  const SpMat Src = build_eigen_sparse(mtx_src_file).transpose().eval();
  const SpMat Tgt = build_eigen_sparse(mtx_tgt_file).transpose().eval();

  const int knn = search_knn(SrcSparseRowsT(Src),  //
                             TgtSparseRowsT(Tgt),  //
                             KNN(argv[5]),         //
                             BILINKS(argv[6]),     //
                             NNLIST(argv[7]),      //
                             out_index);

  CHK_ERR_RET(knn, "Failed to search kNN");

  /////////////////////////////
  // fliter out zero columns //
  /////////////////////////////

  auto valid_src = find_nz_cols(mtx_src_file);
  auto valid_tgt = find_nz_cols(mtx_tgt_file);

  TLOG("Filter out total zero columns");

  ///////////////////////////////
  // give names to the columns //
  ///////////////////////////////

  const std::string out_file(argv[8]);

  std::vector<std::tuple<std::string, std::string, Scalar> > out_named;

  for (auto tt : out_index) {
    Index i, j;
    Scalar d;
    std::tie(i, j, d) = tt;
    if (valid_src.count(i) > 0 && valid_tgt.count(j) > 0) {
      out_named.push_back(std::make_tuple(col_src_names.at(i), col_tgt_names.at(j), d));
    }
  }

  write_tuple_file(out_file, out_named);

  TLOG("Wrote the matching file: " << out_file);

  return EXIT_SUCCESS;
}