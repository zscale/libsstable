/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <unistd.h>
#include "stx/application.h"
#include "stx/cli/flagparser.h"
#include "stx/logging.h"
#include "stx/inspect.h"
#include "sstable/sstablereader.h"
#include "sstable/SSTableScan.h"

using namespace stx;

int main(int argc, const char** argv) {
  stx::Application::init();
  stx::Application::logToStderr();

  stx::cli::FlagParser flags;

  flags.defineFlag(
      "file",
      stx::cli::FlagParser::T_STRING,
      true,
      "f",
      NULL,
      "input sstable file",
      "<file>");

  flags.defineFlag(
      "limit",
      stx::cli::FlagParser::T_INTEGER,
      false,
      NULL,
      NULL,
      "limit",
      "<num>");

  flags.defineFlag(
      "offset",
      stx::cli::FlagParser::T_INTEGER,
      false,
      NULL,
      NULL,
      "offset",
      "<num>");

  flags.defineFlag(
      "order_by",
      stx::cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "order by",
      "<column>");

  flags.defineFlag(
      "order_fn",
      stx::cli::FlagParser::T_STRING,
      false,
      NULL,
      "STRASC",
      "one of: STRASC, STRDSC, NUMASC, NUMDSC",
      "<fn>");

  flags.defineFlag(
      "loglevel",
      stx::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.parseArgv(argc, argv);

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  /* open input sstable */
  auto input_file = flags.getString("file");
  sstable::SSTableReader reader(File::openFile(input_file, File::O_READ));
  if (reader.bodySize() == 0) {
    stx::logWarning("fnord.sstablescan", "sstable is unfinished");
  }

  sstable::SSTableColumnSchema schema;
  schema.loadIndex(&reader);

  /* set up scan */
  sstable::SSTableScan scan(&schema);
  if (flags.isSet("limit")) {
    scan.setLimit(flags.getInt("limit"));
  }

  if (flags.isSet("offset")) {
    scan.setOffset(flags.getInt("offset"));
  }

  if (flags.isSet("order_by")) {
    scan.setOrderBy(flags.getString("order_by"), flags.getString("order_fn"));
  }

  /* execute scan */
  auto headers = scan.columnNames();
  stx::iputs("$0", StringUtil::join(headers, ";"));

  auto cursor = reader.getCursor();
  scan.execute(cursor.get(), [] (const Vector<String> row) {
    stx::iputs("$0", StringUtil::join(row, ";"));
  });

  return 0;
}

