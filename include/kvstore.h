#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <rocksdb/db.h>

/**
 * Value stored in the KV store.
 * bus:      1 byte  (uint8_t)
 * device:   2 bytes (uint16_t)
 * function: 1 byte  (uint8_t)
 */
struct KVValue {
    uint8_t  bus;
    uint16_t device;
    uint8_t  function;

    bool operator==(const KVValue &o) const {
        return bus == o.bus && device == o.device && function == o.function;
    }
};

/**
 * A single record returned by range / scan queries.
 */
struct KVRecord {
    uint32_t key;
    KVValue  value;
};

/**
 * RocksDB-backed key-value store.
 *
 * Key encoding  : 4-byte big-endian uint32 (ensures lexicographic == numeric ordering).
 * Value encoding: [bus(1)] [device_lo(1)] [device_hi(1)] [function(1)] – little-endian device.
 */
class KVStore {
public:
    /**
     * Open (or create) the database at the given path.
     * Throws std::runtime_error on failure.
     */
    explicit KVStore(const std::string &db_path);
    ~KVStore();

    // Non-copyable
    KVStore(const KVStore &) = delete;
    KVStore &operator=(const KVStore &) = delete;

    // -----------------------------------------------------------------------
    // Feature 1 – Query value by key
    // -----------------------------------------------------------------------
    /**
     * Look up the value associated with `key`.
     * Returns std::nullopt if the key does not exist.
     */
    std::optional<KVValue> get(uint32_t key) const;

    // -----------------------------------------------------------------------
    // Feature 2 – Range query with 64-alignment
    // -----------------------------------------------------------------------
    /**
     * Return all records whose key lies in
     *   [align_down(key, 64), align_up(key, 64)]
     * where align_down(k,n) = (k / n) * n
     * and   align_up(k,n)   = align_down(k,n) == k ? k : align_down(k,n) + n
     */
    std::vector<KVRecord> range_query_aligned64(uint32_t key) const;

    // -----------------------------------------------------------------------
    // Feature 3 – Delete / modify all entries matching bus + device
    // -----------------------------------------------------------------------
    /**
     * Delete every record whose value has bus == `bus` AND device == `device`.
     */
    void delete_by_bus_device(uint8_t bus, uint16_t device);

    /**
     * Update the function field of every record whose value has
     * bus == `bus` AND device == `device`.
     */
    void modify_by_bus_device(uint8_t bus, uint16_t device, uint8_t new_function);

    // -----------------------------------------------------------------------
    // Feature 4 – Modify / delete all entries matching bus + device + function
    // -----------------------------------------------------------------------
    /**
     * Delete every record whose value matches bus, device, AND function exactly.
     */
    void delete_by_bus_device_function(uint8_t bus, uint16_t device, uint8_t function);

    /**
     * Replace the entire value of every record that matches bus, device, AND
     * function with `new_value`.
     */
    void modify_by_bus_device_function(uint8_t bus, uint16_t device, uint8_t function,
                                       const KVValue &new_value);

    // -----------------------------------------------------------------------
    // Feature 5 – Batch add
    // -----------------------------------------------------------------------
    /**
     * Insert (or overwrite) all records in `records` atomically.
     */
    void batch_put(const std::vector<KVRecord> &records);

    // -----------------------------------------------------------------------
    // Convenience single-record write / delete
    // -----------------------------------------------------------------------
    void put(uint32_t key, const KVValue &value);
    void remove(uint32_t key);

private:
    rocksdb::DB *db_{nullptr};

    // Encoding helpers
    static std::string encode_key(uint32_t key);
    static uint32_t    decode_key(const rocksdb::Slice &s);
    static std::string encode_value(const KVValue &v);
    static KVValue     decode_value(const rocksdb::Slice &s);

    // Iterate all records and invoke callback; stop if callback returns false
    void scan_all(const std::function<bool(uint32_t, KVValue &)> &cb) const;
};
