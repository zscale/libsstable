/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef _FNORD_SSTABLE_SSTABLEREADER_H
#define _FNORD_SSTABLE_SSTABLEREADER_H
#include <stdlib.h>
#include <string>
#include <vector>
#include <memory>
#include <stx/buffer.h>
#include <stx/exception.h>
#include <stx/io/file.h>
#include <stx/io/mmappedfile.h>
#include <sstable/binaryformat.h>
#include <sstable/fileheaderreader.h>
#include <sstable/cursor.h>
#include <sstable/index.h>
#include <sstable/indexprovider.h>

namespace stx {
namespace sstable {

class SSTableReader {
public:
  class SSTableReaderCursor : public sstable::Cursor {
  public:
    SSTableReaderCursor(
        RefPtr<VFSFile> file,
        size_t begin,
        size_t limit);

    void seekTo(size_t body_offset) override;
    bool trySeekTo(size_t body_offset) override;
    bool next() override;
    bool valid() override;
    void getKey(void** data, size_t* size) override;
    void getData(void** data, size_t* size) override;
    size_t position() const override;
    size_t nextPosition() override;
  protected:
    RefPtr<VFSFile> mmap_;
    size_t pos_;
    size_t begin_;
    size_t limit_;
  };

  SSTableReader(const String& filename);
  SSTableReader(File&& file);
  SSTableReader(RefPtr<VFSFile> vfs_file);
  SSTableReader(const SSTableReader& other) = delete;
  SSTableReader& operator=(const SSTableReader& other) = delete;

  /**
   * Get an sstable cursor for the body of this sstable
   */
  std::unique_ptr<SSTableReaderCursor> getCursor();

  void readHeader(const void** data, size_t* size);
  Buffer readHeader();
  void readFooter(uint32_t type, void** data, size_t* size);
  Buffer readFooter(uint32_t type);

  /**
   * Returns the body size in bytes
   */
  size_t bodySize() const;

  /**
   * Returns true iff the table is finalized
   */
  bool isFinalized() const;

  /**
   * Returns the body offset (the position of the first body byte in the file)
   */
  size_t bodyOffset() const;

  /**
   * Returns the size of the userdata blob stored in the sstable header in bytes
   **/
  size_t headerSize() const;

  /**
   * Returns the number of rows in thos table
   */
  size_t countRows();

private:
  RefPtr<VFSFile> mmap_;
  MemoryInputStream is_;
  uint64_t file_size_;
  MetaPage header_;
};


}
}

#endif
