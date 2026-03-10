#include "kvstore.h"

#include <iostream>
#include <iomanip>

static void print_record(const KVRecord &r) {
    std::cout << "  key=" << r.key
              << "  bus=" << static_cast<int>(r.value.bus)
              << "  device=" << r.value.device
              << "  function=" << static_cast<int>(r.value.function)
              << "\n";
}

int main() {
    const std::string db_path = "/tmp/kvstore_demo";

    KVStore store(db_path);

    // -------------------------------------------------------------------
    // Feature 5: Batch add
    // -------------------------------------------------------------------
    std::cout << "=== Batch add ===\n";
    std::vector<KVRecord> records = {
        {10,  {0x01, 0x0100, 0x00}},
        {11,  {0x01, 0x0100, 0x01}},
        {64,  {0x02, 0x0200, 0x00}},
        {65,  {0x02, 0x0200, 0x01}},
        {127, {0x03, 0x0300, 0x05}},
        {128, {0x03, 0x0300, 0x06}},
        {200, {0x01, 0x0100, 0x02}},
    };
    store.batch_put(records);
    std::cout << "Inserted " << records.size() << " records.\n";

    // -------------------------------------------------------------------
    // Feature 1: Query by key
    // -------------------------------------------------------------------
    std::cout << "\n=== Query by key ===\n";
    auto v = store.get(64);
    if (v) {
        std::cout << "key=64 -> bus=" << static_cast<int>(v->bus)
                  << " device=" << v->device
                  << " function=" << static_cast<int>(v->function) << "\n";
    }
    auto missing = store.get(999);
    std::cout << "key=999 -> " << (missing ? "found" : "not found") << "\n";

    // -------------------------------------------------------------------
    // Feature 2: Range query with 64-alignment
    // -------------------------------------------------------------------
    std::cout << "\n=== Range query aligned64 (key=70) ===\n";
    // align_down(70,64)=64, align_up(70,64)=128
    auto range = store.range_query_aligned64(70);
    std::cout << "Records in [64, 128]:\n";
    for (const auto &r : range) print_record(r);

    std::cout << "\n=== Range query aligned64 (key=64) ===\n";
    // align_down(64,64)=64, align_up(64,64)=64  (already aligned)
    range = store.range_query_aligned64(64);
    std::cout << "Records in [64, 64]:\n";
    for (const auto &r : range) print_record(r);

    // -------------------------------------------------------------------
    // Feature 3: Modify / delete by bus + device
    // -------------------------------------------------------------------
    std::cout << "\n=== Modify by bus=1, device=0x0100 -> set function=0xFF ===\n";
    store.modify_by_bus_device(0x01, 0x0100, 0xFF);
    for (uint32_t k : {10u, 11u, 200u}) {
        auto r = store.get(k);
        if (r) {
            std::cout << "key=" << k << " function=" << static_cast<int>(r->function) << "\n";
        }
    }

    std::cout << "\n=== Delete by bus=2, device=0x0200 ===\n";
    store.delete_by_bus_device(0x02, 0x0200);
    std::cout << "key=64 -> " << (store.get(64) ? "found" : "deleted") << "\n";
    std::cout << "key=65 -> " << (store.get(65) ? "found" : "deleted") << "\n";

    // -------------------------------------------------------------------
    // Feature 4: Modify / delete by bus + device + function
    // -------------------------------------------------------------------
    std::cout << "\n=== Delete by bus=3, device=0x0300, function=5 ===\n";
    store.delete_by_bus_device_function(0x03, 0x0300, 0x05);
    std::cout << "key=127 -> " << (store.get(127) ? "found" : "deleted") << "\n";
    std::cout << "key=128 -> " << (store.get(128) ? "found" : "found (different function)") << "\n";

    std::cout << "\n=== Modify by bus=3, device=0x0300, function=6 -> new {4, 0x0400, 7} ===\n";
    store.modify_by_bus_device_function(0x03, 0x0300, 0x06, {0x04, 0x0400, 0x07});
    auto r128 = store.get(128);
    if (r128) {
        std::cout << "key=128 -> bus=" << static_cast<int>(r128->bus)
                  << " device=" << r128->device
                  << " function=" << static_cast<int>(r128->function) << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
