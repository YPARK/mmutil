#include "mmutil.hh"

#ifndef MMUTIL_MERGE_COL_HH_
#define MMUTIL_MERGE_COL_HH_

int
run_merge_col(const std::string glob_row_file,           //
              const Index column_threshold,              //
              const std::string output,                  //
              const std::vector<std::string> mtx_files,  //
              const std::vector<std::string> row_files,  //
              const std::vector<std::string> col_files);

////////////////////////////////
// lightweight column counter //
////////////////////////////////

struct col_counter_on_valid_rows_t {
  using index_t     = Index;
  using scalar_t    = Scalar;
  using index_map_t = std::unordered_map<index_t, index_t>;

  explicit col_counter_on_valid_rows_t(const index_map_t& _valid_rows)
      : valid_rows(_valid_rows) {
    max_row  = 0;
    max_col  = 0;
    max_elem = 0;
  }

  void set_dimension(const index_t r, const index_t c, const index_t e) {
    std::tie(max_row, max_col, max_elem) = std::make_tuple(r, c, e);
    Col_N.resize(max_col);
    std::fill(Col_N.begin(), Col_N.end(), 0);
  }

  void eval(const index_t row, const index_t col, const scalar_t weight) {
    if (row < max_row && col < max_col && is_valid(row) &&
        std::abs(weight) > EPS) {
      Col_N[col]++;
    }
  }

  void eval_end() {
    // TLOG("Found " << Col_N.sum() << std::endl);
  }

  static constexpr scalar_t EPS = 1e-8;
  const index_map_t& valid_rows;

  Index max_row;
  Index max_col;
  Index max_elem;

  std::vector<index_t> Col_N;

  inline bool is_valid(const index_t row) { return valid_rows.count(row) > 0; }
};

///////////////////
// global copier //
///////////////////

struct glob_triplet_copier_t {

  using index_t     = Index;
  using scalar_t    = Scalar;
  using index_map_t = std::unordered_map<index_t, index_t>;

  explicit glob_triplet_copier_t(
      ogzstream& _ofs,                // output stream
      const index_map_t& _remap_row,  // row mapper
      const index_map_t& _remap_col)  // column mapper
      : ofs(_ofs),
        remap_row(_remap_row),
        remap_col(_remap_col) {
    max_row  = 0;
    max_col  = 0;
    max_elem = 0;
    ASSERT(remap_row.size() > 0, "Empty Remap");
    ASSERT(remap_col.size() > 0, "Empty Remap");
  }

  void set_dimension(const index_t r, const index_t c, const index_t e) {
    // nothing
  }

  void eval(const index_t row, const index_t col, const scalar_t weight) {
    if (remap_col.count(col) > 0 && remap_row.count(row) > 0) {
      const index_t i = remap_row.at(row) + 1;  // fix zero-based to one-based
      const index_t j = remap_col.at(col) + 1;  // fix zero-based to one-based
      ofs << i << FS << j << FS << weight << std::endl;
    }
  }

  void eval_end() {
    // nothing
  }

  static constexpr char FS = ' ';

  ogzstream& ofs;
  const index_map_t& remap_row;
  const index_map_t& remap_col;

  Index max_row;
  Index max_col;
  Index max_elem;
};

#include "mmutil_merge_col.hh"

int
run_merge_col(const std::string glob_row_file, const Index column_threshold,
              const std::string output,
              const std::vector<std::string> mtx_files,
              const std::vector<std::string> row_files,
              const std::vector<std::string> col_files) {

  using Str       = std::string;
  using Str2Index = std::unordered_map<Str, Index>;
  using Index2Str = std::vector<Str>;

  const Index num_batches = mtx_files.size();
  ASSERT(row_files.size() == num_batches, "different # of row files");
  ASSERT(col_files.size() == num_batches, "different # of col files");

  ERR_RET(!file_exists(glob_row_file), "missing the global row file");
  ERR_RET(!all_files_exist(mtx_files), "missing in the mtx files");
  ERR_RET(!all_files_exist(row_files), "missing in the row files");
  ERR_RET(!all_files_exist(col_files), "missing in the col files");

  ////////////////////////////
  // first read global rows //
  ////////////////////////////

  Index2Str glob_rows(0);
  CHK_ERR_RET(read_vector_file(glob_row_file, glob_rows),
              "unable to read the rows");
  TLOG("Read the global row names: " << glob_row_file << " " << glob_rows.size()
                                     << " rows");

  Str2Index glob_positions;
  const Index glob_max_row = glob_rows.size();

  for (Index r = 0; r < glob_rows.size(); ++r) {
    glob_positions[glob_rows.at(r)] = r;
  }

  auto is_glob_row = [&glob_positions](const Str& s) {
    return glob_positions.count(s) > 0;
  };
  auto _glob_row_pos = [&glob_positions](const Str& s) {
    return glob_positions.at(s);
  };

  ///////////////////////////////
  // Figure out dimensionality //
  ///////////////////////////////

  using index_map_t     = col_counter_on_valid_rows_t::index_map_t;
  using index_pair_t    = std::pair<Index, Index>;
  using index_map_ptr_t = std::shared_ptr<index_map_t>;

  std::vector<index_map_ptr_t> remap_to_glob_row_vec;
  std::vector<index_map_ptr_t> remap_to_glob_col_vec;
  // std::vector<index_map_ptr_t> remap_to_local_col_vec;

  Index glob_max_col  = 0;
  Index glob_max_elem = 0;

  const Str output_column = output + ".columns.gz";
  TLOG("Output columns first: " << output_column);
  ogzstream ofs_columns(output_column.c_str(), std::ios::out);

  for (int batch_index = 0; batch_index < num_batches; ++batch_index) {

    const Str mtx_file = mtx_files.at(batch_index);
    const Str row_file = row_files.at(batch_index);
    const Str col_file = col_files.at(batch_index);

    TLOG("MTX : " << mtx_file);
    TLOG("ROW : " << row_file);
    TLOG("COL : " << col_file);

    ////////////////////////////////
    // What are overlapping rows? //
    ////////////////////////////////

    remap_to_glob_row_vec.push_back(std::make_shared<index_map_t>());
    index_map_t& remap_to_glob_row = *(remap_to_glob_row_vec.back().get());

    {
      std::vector<Str> row_names(0);
      CHECK(read_vector_file(row_file, row_names));

      std::vector<Index> local_index(row_names.size());  // original
      std::vector<Index> rel_local_index;  // relevant local indexes
      std::iota(local_index.begin(), local_index.end(), 0);
      std::copy_if(local_index.begin(), local_index.end(),
                   std::back_inserter(rel_local_index),
                   [&](const Index i) { return is_glob_row(row_names.at(i)); });

      std::vector<index_pair_t> local_glob;  // local -> glob mapping

      std::transform(rel_local_index.begin(), rel_local_index.end(),
                     std::back_inserter(local_glob), [&](const Index _local) {
                       const Index _glob = _glob_row_pos(row_names.at(_local));
                       return std::make_pair(_local, _glob);
                     });

      remap_to_glob_row.insert(local_glob.begin(), local_glob.end());
    }

    // for (auto pp : remap_to_glob_row) {
    //   std::cout << pp.first << " " << pp.second << std::endl;
    // }

    ////////////////////////////////
    // What are relevant columns? //
    ////////////////////////////////

    remap_to_glob_col_vec.push_back(std::make_shared<index_map_t>());
    index_map_t& remap_to_glob_col = *(remap_to_glob_col_vec.back().get());
    // remap_to_local_col_vec.push_back(std::make_shared<index_map_t>());
    // index_map_t& remap_to_local_col = *(remap_to_local_col_vec.back().get());

    {
      col_counter_on_valid_rows_t counter(remap_to_glob_row);
      visit_matrix_market_file(mtx_file, counter);
      const std::vector<Index>& nnz_col = counter.Col_N;

      std::vector<Str> column_names(0);
      CHECK(read_vector_file(col_file, column_names));

      ASSERT(column_names.size() >= counter.max_col,
             "Insufficient number of columns");

      std::vector<Index> cols(counter.max_col);
      std::iota(std::begin(cols), std::end(cols), 0);
      std::vector<Index> valid_cols;
      std::copy_if(
          cols.begin(), cols.end(), std::back_inserter(valid_cols),
          [&](const Index j) { return nnz_col.at(j) >= column_threshold; });

      TLOG("Found " << valid_cols.size()
                    << " (with the nnz >=" << column_threshold << ")");

      std::vector<Index> nnz_col_valid;
      std::transform(valid_cols.begin(), valid_cols.end(),
                     std::back_inserter(nnz_col_valid),
                     [&](const Index j) { return nnz_col.at(j); });

      glob_max_elem +=
          std::accumulate(nnz_col_valid.begin(), nnz_col_valid.end(), 0);

      std::vector<Index> idx(valid_cols.size());
      std::iota(idx.begin(), idx.end(), 0);
      std::vector<Index> glob_cols(valid_cols.size());
      std::iota(glob_cols.begin(), glob_cols.end(), glob_max_col);

      auto fun_local2glob = [&](const Index i) {
        return std::make_pair(valid_cols.at(i), glob_cols.at(i));
      };

      std::vector<index_pair_t> local2glob;
      std::transform(idx.begin(), idx.end(), std::back_inserter(local2glob),
                     fun_local2glob);
      remap_to_glob_col.insert(local2glob.begin(), local2glob.end());

      // auto fun_glob2local = [&](const Index i) {
      //   return std::make_pair(glob_cols.at(i), valid_cols.at(i));
      // };
      // std::vector<index_pair_t> glob2local;
      // std::transform(idx.begin(), idx.end(), std::back_inserter(glob2local),
      // fun_glob2local); remap_to_local_col.insert(glob2local.begin(),
      // glob2local.end());

      glob_max_col += glob_cols.size();  // cumulative

      for (Index v : valid_cols) {
        ofs_columns << column_names.at(v) << " " << (batch_index + 1)
                    << std::endl;
      }
    }

    TLOG("Created valid column names");
  }

  ofs_columns.close();

  TLOG("[" << std::setw(10) << glob_max_row                  //
           << " x " << std::setw(10) << glob_max_col << "]"  //
           << std::setw(20) << glob_max_elem);

  // TLOG(remap_to_glob_col.size());

  //////////////////////////////
  // create merged data files //
  //////////////////////////////

  TLOG("Start writing the merged data set");
  const Str output_mtx = output + ".mtx.gz";

  TLOG("Output matrix market file: " << output_mtx);
  ogzstream ofs(output_mtx.c_str(), std::ios::out);

  // write the header
  {
    ofs << "%%MatrixMarket matrix coordinate integer general" << std::endl;
    ofs << glob_max_row << " " << glob_max_col << " " << glob_max_elem
        << std::endl;
  }

  for (int batch_index = 0; batch_index < num_batches; ++batch_index) {

    const Str mtx_file = mtx_files.at(batch_index);
    TLOG("MTX : " << mtx_file);

    const index_map_t& remap_to_glob_row =
        *(remap_to_glob_row_vec.at(batch_index).get());
    const index_map_t& remap_to_glob_col =
        *(remap_to_glob_col_vec.at(batch_index).get());
    // const index_map_t& remap_to_local_col =
    // *(remap_to_local_col_vec.at(batch_index).get());

    glob_triplet_copier_t copier(ofs, remap_to_glob_row, remap_to_glob_col);
    visit_matrix_market_file(mtx_file, copier);
  }

  ofs.close();

  const Str output_row = output + ".rows.gz";
  write_vector_file(output_row, glob_rows);

  TLOG("Successfully finished");
  return EXIT_SUCCESS;
}

#endif
