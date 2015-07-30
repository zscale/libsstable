/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <sstable/binaryformat.h>
#include <sstable/fileheaderreader.h>
#include <stx/exception.h>
#include <stx/fnv.h>

namespace stx {
namespace sstable {

FileHeader FileHeaderReader::readMetaPage(InputStream* is) {
  FileHeader hdr;

  auto magic_bytes = is->readUInt32();
  if (magic_bytes != BinaryFormat::kMagicBytes) {
    RAISE(kIllegalStateError, "not a valid sstable");
  }

  hdr.version_ = is->readUInt16();
  hdr.userdata_offset_ = 6;

  switch (hdr.version_) {

    case 0x1:
      hdr.flags_ = 0;
      break;

    case 0x2:
      hdr.flags_ = is->readUInt64();
      hdr.userdata_offset_ += 8;
      break;

    default:
      RAISE(kIllegalStateError, "unsupported sstable version");

  }

  hdr.body_size_ = is->readUInt64();
  hdr.userdata_checksum_ = is->readUInt32();
  hdr.userdata_size_ = is->readUInt32();
  hdr.userdata_offset_ += 16;

  /* pre version 0x02 body_size > 0 implied that the table is finalized */
  if (hdr.version_ == 0x01 && hdr.body_size_ > 0) {
    hdr.flags_ |= (uint64_t) FileHeaderFlags::FINALIZED;
  }

  return hdr;
}

FileHeaderReader::FileHeaderReader(
    void* buf,
    size_t buf_size) :
    file_size_(buf_size) {
  MemoryInputStream is(buf, buf_size);
  hdr_ = FileHeaderReader::readMetaPage(&is);
}

bool FileHeaderReader::verify() {
  if (hdr_.userdataOffset() + hdr_.userdataSize() > file_size_) {
    return false;
  }

  if (hdr_.userdataSize() == 0) {
    return true;
  }

  const void* userdata;
  size_t userdata_size;
  readUserdata(&userdata, &userdata_size);

  FNV<uint32_t> fnv;
  uint32_t userdata_checksum = fnv.hash(userdata, userdata_size);

  return userdata_checksum == hdr_.userdataChecksum();
}

size_t FileHeaderReader::headerSize() const {
  return hdr_.headerSize();
}

size_t FileHeaderReader::bodySize() const {
  return hdr_.bodySize();
}

bool FileHeaderReader::isFinalized() const {
  return hdr_.isFinalized();
}

size_t FileHeaderReader::userdataSize() const {
  return hdr_.userdataSize();
}

void FileHeaderReader::readUserdata(
    const void** userdata,
    size_t* userdata_size) {
  *userdata_size = hdr_.userdataSize();
  //seekTo(hdr.userdataOffset());
  //*userdata = read(*userdata_size);
  // : public stx::util::BinaryMessageReader 
}

}
}

