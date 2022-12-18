#include <gtest/gtest.h>

#include "Splitter.h"
#include <thread>

TEST(Add, Clients) {

    ISplitter s(1, 2);

    ClientID client1;
    ClientID client2;
    ClientID client3;

    size_t client_count = 0;

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 0);

    EXPECT_TRUE(s.ClientAdd(&client1));
    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 1);
    EXPECT_TRUE(s.ClientAdd(&client2));
    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);
    EXPECT_FALSE(s.ClientAdd(&client3));
    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);

    EXPECT_NE(client1, client2);
}


TEST(Delete, Clients) {

    ISplitter s(1, 2);

    ClientID client1;
    ClientID client2;
    ClientID client3;

    size_t client_count = 0;

    EXPECT_TRUE(s.ClientAdd(&client1));
    EXPECT_TRUE(s.ClientAdd(&client2));

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);

    EXPECT_TRUE(s.ClientRemove(client1));

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 1);

    EXPECT_FALSE(s.ClientRemove(client1));

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 1);

    EXPECT_TRUE(s.ClientAdd(&client3));
    
    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);
}

TEST(Close, Clients) {
    ISplitter s(1, 2);

    ClientID client1;
    ClientID client2;

    size_t client_count;
    EXPECT_TRUE(s.ClientAdd(&client1));
    EXPECT_TRUE(s.ClientAdd(&client2));

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);

    s.Close();

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 0);
}


TEST(ClosePull, Clients) {
    ISplitter s(1, 2);

    ClientID client1;
    ClientID client2;

    size_t client_count;
    EXPECT_TRUE(s.ClientAdd(&client1));
    EXPECT_TRUE(s.ClientAdd(&client2));

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 2);

    s.Close();

    EXPECT_TRUE(s.ClientGetCount(&client_count));
    EXPECT_EQ(client_count, 0);
}


TEST(StopUntillPull, Clients) {
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
    s.Close();

    pull_thread.join();
}

TEST(StopUntillPush, Clients) {
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
        EXPECT_EQ(res, ISplitterError::CLOSED);
    });

    FrameBuffer fb;
    auto res = s.Get(client1, fb, std::numeric_limits<int32_t>::max());
    EXPECT_EQ(res, ISplitterError::NO_ERROR);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); //test may be flaky
    s.Close();

    push_thread.join();
}