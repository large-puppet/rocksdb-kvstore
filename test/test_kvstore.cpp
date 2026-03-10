#include "kvstore.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Fixture: fresh temporary DB for every test
// ---------------------------------------------------------------------------

class KVStoreTest : public ::testing::Test {
protected:
    std::string db_path_;
    KVStore *store_{nullptr};

    void SetUp() override {
        db_path_ = std::string("/tmp/kvstore_test_") + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
                   + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name();
        // Ensure the path is clean
        std::filesystem::remove_all(db_path_);
        store_ = new KVStore(db_path_);
    }

    void TearDown() override {
        delete store_;
        std::filesystem::remove_all(db_path_);
    }
};

// ---------------------------------------------------------------------------
// Feature 1 – get() by key
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, GetExistingKey) {
    store_->put(42, {0x01, 0x0200, 0x03});
    auto v = store_->get(42);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->bus,      0x01);
    EXPECT_EQ(v->device,   0x0200);
    EXPECT_EQ(v->function, 0x03);
}

TEST_F(KVStoreTest, GetMissingKey) {
    auto v = store_->get(999);
    EXPECT_FALSE(v.has_value());
}

TEST_F(KVStoreTest, GetAfterOverwrite) {
    store_->put(1, {0x01, 0x0001, 0x01});
    store_->put(1, {0x02, 0x0002, 0x02});
    auto v = store_->get(1);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->bus,      0x02);
    EXPECT_EQ(v->device,   0x0002);
    EXPECT_EQ(v->function, 0x02);
}

// ---------------------------------------------------------------------------
// Feature 2 – range_query_aligned64()
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, RangeQueryAligned64_KeyAlreadyAligned) {
    // key=64 is already aligned -> range [64, 64]
    store_->put(64,  {1, 100, 0});
    store_->put(63,  {1, 100, 1});  // outside
    store_->put(65,  {1, 100, 2});  // outside (hi == lo == 64)
    auto res = store_->range_query_aligned64(64);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].key, 64u);
}

TEST_F(KVStoreTest, RangeQueryAligned64_InsideBlock) {
    // key=70 -> align_down=64, align_up=128
    store_->put(63,  {0, 0, 0});   // below range
    store_->put(64,  {1, 1, 1});   // in range
    store_->put(70,  {2, 2, 2});   // in range
    store_->put(128, {3, 3, 3});   // in range (inclusive hi)
    store_->put(129, {4, 4, 4});   // above range
    auto res = store_->range_query_aligned64(70);
    ASSERT_EQ(res.size(), 3u);
    EXPECT_EQ(res[0].key, 64u);
    EXPECT_EQ(res[1].key, 70u);
    EXPECT_EQ(res[2].key, 128u);
}

TEST_F(KVStoreTest, RangeQueryAligned64_EmptyRange) {
    // nothing in DB
    auto res = store_->range_query_aligned64(100);
    EXPECT_TRUE(res.empty());
}

TEST_F(KVStoreTest, RangeQueryAligned64_BoundaryWraparound) {
    // key=0 -> align_down=0, align_up=0 (already aligned)
    store_->put(0,  {5, 5, 5});
    store_->put(1,  {6, 6, 6});
    auto res = store_->range_query_aligned64(0);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].key, 0u);
}

// ---------------------------------------------------------------------------
// Feature 3 – delete_by_bus_device / modify_by_bus_device
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, DeleteByBusDevice) {
    store_->put(1, {0xAA, 0x1234, 0x01});
    store_->put(2, {0xAA, 0x1234, 0x02});
    store_->put(3, {0xBB, 0x1234, 0x03});  // different bus
    store_->put(4, {0xAA, 0x9999, 0x04});  // different device

    store_->delete_by_bus_device(0xAA, 0x1234);

    EXPECT_FALSE(store_->get(1).has_value());
    EXPECT_FALSE(store_->get(2).has_value());
    EXPECT_TRUE(store_->get(3).has_value());
    EXPECT_TRUE(store_->get(4).has_value());
}

TEST_F(KVStoreTest, ModifyByBusDevice) {
    store_->put(10, {0x01, 0x0100, 0x00});
    store_->put(11, {0x01, 0x0100, 0x01});
    store_->put(12, {0x02, 0x0100, 0x02});

    store_->modify_by_bus_device(0x01, 0x0100, 0xFF);

    auto v10 = store_->get(10);
    ASSERT_TRUE(v10.has_value());
    EXPECT_EQ(v10->function, 0xFF);
    EXPECT_EQ(v10->bus,      0x01);
    EXPECT_EQ(v10->device,   0x0100);

    auto v11 = store_->get(11);
    ASSERT_TRUE(v11.has_value());
    EXPECT_EQ(v11->function, 0xFF);

    // key=12 has different bus, must not be changed
    auto v12 = store_->get(12);
    ASSERT_TRUE(v12.has_value());
    EXPECT_EQ(v12->function, 0x02);
}

// ---------------------------------------------------------------------------
// Feature 4 – delete_by_bus_device_function / modify_by_bus_device_function
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, DeleteByBusDeviceFunction) {
    store_->put(20, {0x01, 0x0100, 0x05});
    store_->put(21, {0x01, 0x0100, 0x05});
    store_->put(22, {0x01, 0x0100, 0x06});  // different function
    store_->put(23, {0x02, 0x0100, 0x05});  // different bus

    store_->delete_by_bus_device_function(0x01, 0x0100, 0x05);

    EXPECT_FALSE(store_->get(20).has_value());
    EXPECT_FALSE(store_->get(21).has_value());
    EXPECT_TRUE(store_->get(22).has_value());
    EXPECT_TRUE(store_->get(23).has_value());
}

TEST_F(KVStoreTest, ModifyByBusDeviceFunction) {
    store_->put(30, {0x01, 0x0100, 0x05});
    store_->put(31, {0x01, 0x0100, 0x05});
    store_->put(32, {0x01, 0x0100, 0x06});

    KVValue new_val{0xAA, 0xBBBB, 0xCC};
    store_->modify_by_bus_device_function(0x01, 0x0100, 0x05, new_val);

    auto v30 = store_->get(30);
    ASSERT_TRUE(v30.has_value());
    EXPECT_EQ(*v30, new_val);

    auto v31 = store_->get(31);
    ASSERT_TRUE(v31.has_value());
    EXPECT_EQ(*v31, new_val);

    // key=32 has different function, must not be changed
    auto v32 = store_->get(32);
    ASSERT_TRUE(v32.has_value());
    EXPECT_EQ(v32->function, 0x06);
}

// ---------------------------------------------------------------------------
// Feature 5 – batch_put()
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, BatchPutInsertsAll) {
    std::vector<KVRecord> records = {
        {100, {0x01, 0x0001, 0x01}},
        {200, {0x02, 0x0002, 0x02}},
        {300, {0x03, 0x0003, 0x03}},
    };
    store_->batch_put(records);

    for (const auto &r : records) {
        auto v = store_->get(r.key);
        ASSERT_TRUE(v.has_value()) << "key=" << r.key << " not found";
        EXPECT_EQ(*v, r.value) << "key=" << r.key << " value mismatch";
    }
}

TEST_F(KVStoreTest, BatchPutOverwrites) {
    store_->put(50, {0x01, 0x0001, 0x01});
    store_->batch_put({{50, {0xFF, 0xFFFF, 0xFF}}});
    auto v = store_->get(50);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->bus,      0xFF);
    EXPECT_EQ(v->device,   0xFFFF);
    EXPECT_EQ(v->function, 0xFF);
}

TEST_F(KVStoreTest, BatchPutEmpty) {
    EXPECT_NO_THROW(store_->batch_put({}));
}

// ---------------------------------------------------------------------------
// remove() helper
// ---------------------------------------------------------------------------

TEST_F(KVStoreTest, RemoveKey) {
    store_->put(77, {1, 2, 3});
    store_->remove(77);
    EXPECT_FALSE(store_->get(77).has_value());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
