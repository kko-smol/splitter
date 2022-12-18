#include <gtest/gtest.h>

#include "Splitter.h"
#include <thread>

TEST(PullTimeout, SingleClient) {

    ISplitter s(1, 2);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    FrameBuffer fb;
    auto res = s.Get(client1, fb, 1000);
    EXPECT_EQ(res, ISplitterError::TIMEOUT);
}


TEST(PushTimeout, SingleClient) {
    ISplitter s(1, 2);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    FrameBuffer fb = std::make_shared<std::vector<uint8_t>>(100);
    
    auto res = s.Put(fb, 1000);
    EXPECT_EQ(res, ISplitterError::NO_ERROR);

    res = s.Put(fb, 1000);
    EXPECT_EQ(res, ISplitterError::TIMEOUT);
}

TEST(FifoTestNoDrop, SingleClient) {
    int num_bufs = 4;
    ISplitter s(num_bufs, 2);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

   
    std::vector<FrameBuffer> fbs;

    for (int i = 0; i < num_bufs; ++i) {
        FrameBuffer fb = std::make_shared<std::vector<uint8_t>>(100);
        auto res = s.Put(fb, 1000);
        EXPECT_EQ(res, ISplitterError::NO_ERROR);
        fbs.push_back(fb);
    }

    for (int i = 0; i < num_bufs; ++i) {
        FrameBuffer fb;
        FrameBuffer expected_fb = fbs[i];
        auto res = s.Get(client1, fb, 1000);
        EXPECT_EQ(res, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb, expected_fb);
    }
}


TEST(FifoTestDrop, SingleClient) {
    int num_bufs = 4;
    ISplitter s(2, 2);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    std::vector<FrameBuffer> fbs;

    for (int i = 0; i < num_bufs; ++i) {
        FrameBuffer fb = std::make_shared<std::vector<uint8_t>>(100);
        auto res = s.Put(fb, 1000);
        if (i < 2) {
            EXPECT_EQ(res, ISplitterError::NO_ERROR);
        } else {
            EXPECT_EQ(res, ISplitterError::TIMEOUT);
        }

        fbs.push_back(fb);
    }

    // from 2 - first 2 bufs will dropped
    for (int i = 2; i < num_bufs; ++i) {
        FrameBuffer fb;
        FrameBuffer expected_fb = fbs[i];
        auto res = s.Get(client1, fb, 1000);
        EXPECT_EQ(res, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb, expected_fb);
    }
    {
        ClientID id;
        size_t latency;
        size_t drops;
        auto lock = s.BeginClientsIteration();
        s.ClientGetByIndex(0, &id, &latency, &drops, lock);
        EXPECT_EQ(id, client1);
        EXPECT_EQ(drops, 2);
    }
}

TEST(DeleteUntillPull, SingleClient) {
    ISplitter s(1, 1);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    FrameBuffer fb0 = std::make_shared<std::vector<uint8_t>>(100);
    FrameBuffer fb1 = std::make_shared<std::vector<uint8_t>>(100);

    std::thread pull_thread([&]() {
        FrameBuffer fb;
        auto res = s.Get(client1, fb, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(fb, fb0);
        EXPECT_EQ(res, ISplitterError::NO_ERROR);

        res = s.Get(client1, fb, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(res, ISplitterError::NO_ERROR);

        res = s.Get(client1, fb, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(res, ISplitterError::EOS);
    });

    auto res = s.Put(fb0, std::numeric_limits<int32_t>::max());
    EXPECT_EQ(res, ISplitterError::NO_ERROR);

    res = s.Put(fb1, std::numeric_limits<int32_t>::max());
    EXPECT_EQ(res, ISplitterError::NO_ERROR);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); //test may be flaky
    EXPECT_TRUE(s.ClientRemove(client1));

    pull_thread.join();
}

TEST(DeleteUntillPush, SingleClient) {
    ISplitter s(1, 1);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    std::thread push_thread([&]() {
        FrameBuffer fb0 = std::make_shared<std::vector<uint8_t>>(100);
        FrameBuffer fb1 = std::make_shared<std::vector<uint8_t>>(100);
        FrameBuffer fb2 = std::make_shared<std::vector<uint8_t>>(100);
        auto res = s.Put(fb0, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(res, ISplitterError::NO_ERROR);

        res = s.Put(fb1, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(res, ISplitterError::NO_ERROR);

        res = s.Put(fb2, std::numeric_limits<int32_t>::max());
        EXPECT_EQ(res, ISplitterError::NO_ERROR);
    });

    FrameBuffer fb;
    auto res = s.Get(client1, fb, std::numeric_limits<int32_t>::max());
    EXPECT_EQ(res, ISplitterError::NO_ERROR);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); //test may be flaky
    EXPECT_TRUE(s.ClientRemove(client1));

    push_thread.join();
}

TEST(FifoTestNoDropThreaded, SingleClient) {
    int num_bufs = 10;
    ISplitter s(2, 2);

    ClientID client1;
    EXPECT_TRUE(s.ClientAdd(&client1));

    std::vector<FrameBuffer> fbs;
    for (int i = 0; i < num_bufs; ++i) {
        FrameBuffer fb = std::make_shared<std::vector<uint8_t>>(100);
        fbs.push_back(fb);
    }

    std::thread pop_thread([&]() {
        for (int i = 0; i < num_bufs; ++i) {
            FrameBuffer fb;
            FrameBuffer expected_fb = fbs[i];
            auto res = s.Get(client1, fb, 1000);
            EXPECT_EQ(res, ISplitterError::NO_ERROR);
            EXPECT_EQ(fb, expected_fb);
        }
    });

    for (int i = 0; i < num_bufs; ++i) {
        auto res = s.Put(fbs[i], 1000);
        EXPECT_EQ(res, ISplitterError::NO_ERROR);
    }

    pop_thread.join();
}