#include "kvstore.h"

#include <stdexcept>
#include <cassert>
#include <rocksdb/write_batch.h>
#include <rocksdb/options.h>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

KVStore::KVStore(const std::string &db_path) {
    rocksdb::Options opts;
    opts.create_if_missing = true;
    rocksdb::Status s = rocksdb::DB::Open(opts, db_path, &db_);
    if (!s.ok()) {
        throw std::runtime_error("KVStore: failed to open DB at '" + db_path +
                                 "': " + s.ToString());
    }
}

KVStore::~KVStore() {
    delete db_;
}

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

/*
 * Big-endian 4-byte encoding of the key ensures that RocksDB's default
 * lexicographic order matches the natural numeric order of uint32_t keys.
 */
std::string KVStore::encode_key(uint32_t key) {
    char buf[4];
    buf[0] = static_cast<char>((key >> 24) & 0xFF);
    buf[1] = static_cast<char>((key >> 16) & 0xFF);
    buf[2] = static_cast<char>((key >>  8) & 0xFF);
    buf[3] = static_cast<char>( key        & 0xFF);
    return std::string(buf, 4);
}

uint32_t KVStore::decode_key(const rocksdb::Slice &s) {
    assert(s.size() == 4);
    const auto *p = reinterpret_cast<const uint8_t *>(s.data());
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}

/*
 * Value layout (4 bytes):
 *   [0] bus       (1 byte)
 *   [1] device_lo (low byte  of device, little-endian)
 *   [2] device_hi (high byte of device, little-endian)
 *   [3] function  (1 byte)
 */
std::string KVStore::encode_value(const KVValue &v) {
    char buf[4];
    buf[0] = static_cast<char>(v.bus);
    buf[1] = static_cast<char>(v.device & 0xFF);
    buf[2] = static_cast<char>((v.device >> 8) & 0xFF);
    buf[3] = static_cast<char>(v.function);
    return std::string(buf, 4);
}

KVValue KVStore::decode_value(const rocksdb::Slice &s) {
    assert(s.size() == 4);
    const auto *p = reinterpret_cast<const uint8_t *>(s.data());
    KVValue v;
    v.bus      = p[0];
    v.device   = static_cast<uint16_t>(p[1]) | (static_cast<uint16_t>(p[2]) << 8);
    v.function = p[3];
    return v;
}

// ---------------------------------------------------------------------------
// Internal scan helper
// ---------------------------------------------------------------------------

void KVStore::scan_all(const std::function<bool(uint32_t, KVValue &)> &cb) const {
    rocksdb::ReadOptions ro;
    auto *it = db_->NewIterator(ro);
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        uint32_t k = decode_key(it->key());
        KVValue  v = decode_value(it->value());
        if (!cb(k, v)) break;
    }
    delete it;
}

// ---------------------------------------------------------------------------
// Feature 1 – Query by key
// ---------------------------------------------------------------------------

std::optional<KVValue> KVStore::get(uint32_t key) const {
    std::string val_str;
    rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), encode_key(key), &val_str);
    if (s.IsNotFound()) return std::nullopt;
    if (!s.ok()) throw std::runtime_error("KVStore::get failed: " + s.ToString());
    return decode_value(rocksdb::Slice(val_str));
}

// ---------------------------------------------------------------------------
// Feature 2 – Range query with 64-alignment
// ---------------------------------------------------------------------------

std::vector<KVRecord> KVStore::range_query_aligned64(uint32_t key) const {
    // align_down: largest multiple of 64 that is <= key
    uint32_t lo = (key / 64u) * 64u;
    // align_up:   smallest multiple of 64 that is >= key
    uint32_t hi = (lo == key) ? key : lo + 64u;

    std::string lo_key = encode_key(lo);
    std::string hi_key = encode_key(hi);

    rocksdb::ReadOptions ro;
    auto *it = db_->NewIterator(ro);

    std::vector<KVRecord> result;
    for (it->Seek(lo_key); it->Valid(); it->Next()) {
        uint32_t k = decode_key(it->key());
        if (k > hi) break;
        result.push_back({k, decode_value(it->value())});
    }
    delete it;
    return result;
}

// ---------------------------------------------------------------------------
// Feature 3 – Delete / modify by bus + device
// ---------------------------------------------------------------------------

void KVStore::delete_by_bus_device(uint8_t bus, uint16_t device) {
    rocksdb::WriteBatch batch;
    scan_all([&](uint32_t k, KVValue &v) -> bool {
        if (v.bus == bus && v.device == device) {
            batch.Delete(encode_key(k));
        }
        return true;
    });
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) throw std::runtime_error("KVStore::delete_by_bus_device failed: " + s.ToString());
}

void KVStore::modify_by_bus_device(uint8_t bus, uint16_t device, uint8_t new_function) {
    rocksdb::WriteBatch batch;
    scan_all([&](uint32_t k, KVValue &v) -> bool {
        if (v.bus == bus && v.device == device) {
            v.function = new_function;
            batch.Put(encode_key(k), encode_value(v));
        }
        return true;
    });
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) throw std::runtime_error("KVStore::modify_by_bus_device failed: " + s.ToString());
}

// ---------------------------------------------------------------------------
// Feature 4 – Modify / delete by bus + device + function
// ---------------------------------------------------------------------------

void KVStore::delete_by_bus_device_function(uint8_t bus, uint16_t device, uint8_t function) {
    rocksdb::WriteBatch batch;
    scan_all([&](uint32_t k, KVValue &v) -> bool {
        if (v.bus == bus && v.device == device && v.function == function) {
            batch.Delete(encode_key(k));
        }
        return true;
    });
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) throw std::runtime_error("KVStore::delete_by_bus_device_function failed: " + s.ToString());
}

void KVStore::modify_by_bus_device_function(uint8_t bus, uint16_t device, uint8_t function,
                                             const KVValue &new_value) {
    rocksdb::WriteBatch batch;
    scan_all([&](uint32_t k, KVValue &v) -> bool {
        if (v.bus == bus && v.device == device && v.function == function) {
            batch.Put(encode_key(k), encode_value(new_value));
        }
        return true;
    });
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) throw std::runtime_error("KVStore::modify_by_bus_device_function failed: " + s.ToString());
}

// ---------------------------------------------------------------------------
// Feature 5 – Batch add
// ---------------------------------------------------------------------------

void KVStore::batch_put(const std::vector<KVRecord> &records) {
    rocksdb::WriteBatch batch;
    for (const auto &r : records) {
        batch.Put(encode_key(r.key), encode_value(r.value));
    }
    rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
    if (!s.ok()) throw std::runtime_error("KVStore::batch_put failed: " + s.ToString());
}

// ---------------------------------------------------------------------------
// Convenience helpers
// ---------------------------------------------------------------------------

void KVStore::put(uint32_t key, const KVValue &value) {
    rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), encode_key(key), encode_value(value));
    if (!s.ok()) throw std::runtime_error("KVStore::put failed: " + s.ToString());
}

void KVStore::remove(uint32_t key) {
    rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), encode_key(key));
    if (!s.ok()) throw std::runtime_error("KVStore::remove failed: " + s.ToString());
}
