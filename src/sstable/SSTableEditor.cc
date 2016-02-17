/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <string.h>
#include <stx/exception.h>
#include <stx/fnv.h>
#include <sstable/binaryformat.h>
#include <sstable/fileheaderwriter.h>
#include <sstable/fileheaderreader.h>
#include <sstable/SSTableEditor.h>
#include <sstable/SSTableColumnWriter.h>

namespace stx {
namespace sstable {

std::unique_ptr<SSTableEditor> SSTableEditor::create(
    const std::string& filename,
    IndexProvider index_provider,
    void const* header,
    size_t header_size) {
  auto file = File::openFile(
      filename,
      File::O_READ | File::O_WRITE | File::O_CREATE);

  auto file_size = file.size();
  if (file_size > 0) {
    RAISE(kIllegalStateError, "file size must be 0");
  }

  auto sstable = new SSTableEditor(
      filename,
      file_size,
      index_provider.popIndexes());

  sstable->writeHeader(header, header_size);
  return std::unique_ptr<SSTableEditor>(sstable);
}

std::unique_ptr<SSTableEditor> SSTableEditor::reopen(
    const std::string& filename,
    IndexProvider index_provider) {
  auto file = File::openFile(filename, File::O_READ);
  auto file_size = file.size();

  auto sstable = new SSTableEditor(
      filename,
      file_size,
      index_provider.popIndexes());

  sstable->reopen(file_size);
  return std::unique_ptr<SSTableEditor>(sstable);
}

SSTableEditor::SSTableEditor(
    const std::string& filename,
    size_t file_size,
    std::vector<Index::IndexRef>&& indexes) :
    indexes_(std::move(indexes)),
    mmap_(new io::MmapPageManager(filename, file_size)),
    header_size_(0),
    body_size_(0),
    finalized_(false) {}

SSTableEditor::~SSTableEditor() {
}

// FIXPAUL lock
uint64_t SSTableEditor::appendRow(
    void const* key,
    size_t key_size,
    void const* data,
    size_t data_size) {
  if (finalized_) {
    RAISE(kIllegalStateError, "table is immutable (alread finalized)");
  }
  // FIXPAUL assert that key is monotonically increasing...

  if (data_size == 0) {
    RAISE(kIllegalArgumentError, "can't append empty row");
  }

  size_t page_size = sizeof(BinaryFormat::RowHeader) + key_size + data_size;
  auto alloc = mmap_->allocPage(page_size);
  auto page = mmap_->getPage(alloc);

  auto header = page->structAt<BinaryFormat::RowHeader>(0);
  header->key_size = key_size;
  header->data_size = data_size;

  auto key_dst = page->structAt<void>(sizeof(BinaryFormat::RowHeader));
  memcpy(key_dst, key, key_size);

  auto data_dst = page->structAt<void>(
      sizeof(BinaryFormat::RowHeader) + key_size);
  memcpy(data_dst, data, data_size);

  FNV<uint32_t> fnv;
  header->checksum = fnv.hash(
      page->structAt<void>(sizeof(uint32_t)),
      page_size - sizeof(uint32_t));

  page->sync();

  auto row_body_offset = body_size_;
  body_size_ += page_size;

  for (const auto& idx : indexes_) {
    idx->addRow(row_body_offset, key, key_size, data, data_size);
  }

  return row_body_offset;
}

uint64_t SSTableEditor::appendRow(
    const std::string& key,
    const std::string& value) {
  return appendRow(key.data(), key.size(), value.data(), value.size());
}

uint64_t SSTableEditor::appendRow(
    const std::string& key,
    const SSTableColumnWriter& value) {
  return appendRow(key.data(), key.size(), value.data(), value.size());
}

// FIXPAUL lock
void SSTableEditor::writeHeader(void const* userdata, size_t userdata_size) {
  if (header_size_ > 0) {
    RAISE(kIllegalStateError, "header already written");
  }

  header_size_ = FileHeaderWriter::calculateSize(userdata_size);
  auto alloc = mmap_->allocPage(header_size_);
  auto page = mmap_->getPage(alloc);

  if (alloc.offset != 0) {
    RAISE(kIllegalStateError, "header page offset must be 0");
  }

  FileHeaderWriter header(
      page->ptr(),
      page->size(),
      0,
      userdata,
      userdata_size);

  page->sync();
}

void SSTableEditor::writeIndex(uint32_t index_type, const Buffer& buf) {
  writeIndex(index_type, buf.data(), buf.size());
}

void SSTableEditor::writeIndex(uint32_t index_type, void* data, size_t size) {
  if (finalized_) {
    RAISE(kIllegalStateError, "table is immutable (alread finalized)");
  }

  if (size == 0) {
    return;
  }

  auto alloc = mmap_->allocPage(sizeof(BinaryFormat::FooterHeader) + size);
  auto page = mmap_->getPage(alloc, io::MmapPageManager::kNoPadding{});

  auto header = page->structAt<BinaryFormat::FooterHeader>(0);
  header->magic = BinaryFormat::kMagicBytes;
  header->type = index_type;
  header->footer_size = size;

  FNV<uint32_t> fnv;
  header->footer_checksum = fnv.hash(data, size);

  if (size > 0) {
    auto dst = page->structAt<void>(sizeof(BinaryFormat::FooterHeader));
    memcpy(dst, data, size);
  }

  page->sync();
  mmap_->shrinkFile();
}

void SSTableEditor::reopen(size_t file_size) {
  auto page = mmap_->getPage(io::PageManager::Page(0, file_size));

  FileHeaderReader header(page->ptr(), page->size());

  if (!header.verify()) {
    RAISE(kIllegalStateError, "corrupt sstable header");
  }

  if (header.bodySize() != 0) {
    RAISE(kIllegalStateError, "finalized sstable can't be re-opened");
  }

  if (header.headerSize() + header.bodySize() > file_size) {
    RAISE(kIllegalStateError, "file metadata offsets exceed file bounds");
  }

  header_size_ = header.headerSize();
  body_size_ = file_size - header_size_;
}

// FIXPAUL lock
void SSTableEditor::finalize() {
  finalized_ = true;

  auto page = mmap_->getPage(
      io::PageManager::Page(0, FileHeaderWriter::calculateSize(0)));

  FileHeaderWriter header(page->ptr(), page->size());
  header.updateBodySize(body_size_);
  header.setFlag(FileHeaderFlags::FINALIZED);

  page->sync();
  mmap_->shrinkFile();
}

// FIXPAUL lock
std::unique_ptr<SSTableEditor::SSTableEditorCursor> SSTableEditor::getCursor() {
  return std::unique_ptr<SSTableEditorCursor>(
      new SSTableEditor::SSTableEditorCursor(this, mmap_.get()));
}

// FIXPAUL lock
size_t SSTableEditor::bodySize() const {
  return body_size_;
}

size_t SSTableEditor::headerSize() const {
  return header_size_;
}

SSTableEditor::SSTableEditorCursor::SSTableEditorCursor(
    SSTableEditor* table,
    io::MmapPageManager* mmap) :
    table_(table),
    mmap_(mmap),
    pos_(0) {}

bool SSTableEditor::SSTableEditorCursor::trySeekTo(size_t body_offset) {
  if (body_offset >= table_->bodySize()) {
    return false;
  } else {
    pos_ = body_offset;
    return true;
  }
}

void SSTableEditor::SSTableEditorCursor::seekTo(size_t body_offset) {
  if (body_offset >= table_->bodySize()) {
    RAISE(kIndexError, "seekTo() out of bounds position");
  }

  pos_ = body_offset;
}

bool SSTableEditor::SSTableEditorCursor::next() {
  auto page = getPage();
  auto header = page->structAt<BinaryFormat::RowHeader>(0);

  size_t page_size = page->page_.size;
  size_t row_size = sizeof(BinaryFormat::RowHeader) + header->key_size +
      header->data_size;

  if (row_size >= page_size) {
    return false;
  } else {
    pos_ += row_size;
    return true;
  }
}

bool SSTableEditor::SSTableEditorCursor::valid() {
  return pos_ < table_->bodySize();
}

void SSTableEditor::SSTableEditorCursor::getKey(void** data, size_t* size) {
  auto page = getPage();
  size_t page_size = page->page_.size;

  auto header = page->structAt<BinaryFormat::RowHeader>(0);
  if (header->key_size == 0) {
    RAISE(kIllegalStateError, "empty key");
  }

  if (sizeof(BinaryFormat::RowHeader) + header->key_size > page_size) {
    RAISE(kIllegalStateError, "key exceeds page boundary");
  }

  *data = page->structAt<void>(sizeof(BinaryFormat::RowHeader));
  *size = header->key_size;
}

size_t SSTableEditor::SSTableEditorCursor::position() const {
  return pos_;
}

size_t SSTableEditor::SSTableEditorCursor::nextPosition() {
  auto page = getPage();
  auto header = page->structAt<BinaryFormat::RowHeader>(0);
  size_t row_size = sizeof(BinaryFormat::RowHeader) + header->key_size +
      header->data_size;

  return pos_ + row_size;
}

void SSTableEditor::SSTableEditorCursor::getData(void** data, size_t* size) {
  auto page = getPage();
  auto header = page->structAt<BinaryFormat::RowHeader>(0);

  size_t page_size = page->page_.size;
  size_t row_size = sizeof(BinaryFormat::RowHeader) + header->key_size +
      header->data_size;
  if (row_size > page_size) {
    RAISE(kIllegalStateError, "row exceeds page boundary");
  }

  *data = page->structAt<void>(
      sizeof(BinaryFormat::RowHeader) + header->key_size);
  *size = header->data_size;
}

std::unique_ptr<io::PageManager::PageRef>
SSTableEditor::SSTableEditorCursor::getPage() {
  return mmap_->getPage(io::PageManager::Page(
      table_->headerSize() + pos_,
      table_->bodySize() - pos_));
}

}
}
