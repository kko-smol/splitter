/*(*)Пусть количество буферов (максимальная задержка) равно 2.
Мы положили в сплиттер буфера 0,1,2,3,4,5,6,7,8,9 (с интервалом в 100 msec, максимальное время ожидания в SplitterPut - 50 msec). 
- Клиент 1 сразу получил 0,1,2 а затем 500 msec “спал”, то
  после того как проснется он должен получить 6,7,8,9 (3, 4, 5 будут потеряны) 
- Остальные клиенты должны в это время получить все буфера 0,1,2,3,4,5,6,7,8,9 с максимальной задержкой 50 msec (для буферов 5, 6, 7,).
*/
#include <gtest/gtest.h>

#include "Splitter.h"

/*
t     push  pull_c1 pull_c2
0   0.000 0 
1   0.050       0       0
2   0.100 1
3   0.150       1       1
4   0.200 2 
5   0.250       2       2
6   0.300 3 
7   0.350       3
8   0.400 4 
9   0.450       4
10  0.500 5 
11  0.550       5
12  0.600 6
13  0.650       6
14  0.700 7
15  0.750       7       6
16  0.800 8
17  0.850       8       7
18  0.900 9
19  0.950       9       8
20  1.000 -
21  1.050       -       9
*/

TEST(SlowClient, MultipleClients) {
    ISplitter s(2, 2);

    ClientID client1;
    ClientID client2;
    EXPECT_TRUE(s.ClientAdd(&client1));
    EXPECT_TRUE(s.ClientAdd(&client2));

    std::vector<FrameBuffer> bufs;
    for (int i = 0; i < 10; ++i) {
        bufs.push_back(std::make_shared<std::vector<uint8_t>>(100));
    }

    // steps 0 - 5, bufs [0-2]
    for (int i = 0; i < 3; ++i) {
        {
            auto res = s.Put(bufs[i], 50);
            EXPECT_EQ(res, ISplitterError::NO_ERROR);
        }
        
        FrameBuffer fb1;
        auto res1 = s.Get(client1, fb1, 100);
        FrameBuffer fb2;
        auto res2 = s.Get(client2, fb2, 100);
        EXPECT_EQ(res1, ISplitterError::NO_ERROR);
        EXPECT_EQ(res2, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb1, bufs[i]);
        EXPECT_EQ(fb2, bufs[i]);
    }

    // steps 6 - 14, bufs [3-6]
    for (int i = 3; i < 7; ++i) {
        {
            auto res = s.Put(bufs[i], 50);
            if ((i>=5) && (i<=7)) {
                EXPECT_EQ(res, ISplitterError::TIMEOUT);
            } else {
                EXPECT_EQ(res, ISplitterError::NO_ERROR);
            }
        }
        
        FrameBuffer fb1;
        auto res1 = s.Get(client1, fb1, 100);
        EXPECT_EQ(res1, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb1, bufs[i]);
    }

    // steps 15 - 19, bufs [7-9]
    for (int i = 7; i < 10; ++i) {
        {
            auto res = s.Put(bufs[i], 50);
            if (i==7) {
                EXPECT_EQ(res, ISplitterError::TIMEOUT);
            } else {
                EXPECT_EQ(res, ISplitterError::NO_ERROR);
            }
        }

        FrameBuffer fb1;
        auto res1 = s.Get(client1, fb1, 100);
        FrameBuffer fb2;
        auto res2 = s.Get(client2, fb2, 100);
        EXPECT_EQ(res1, ISplitterError::NO_ERROR);
        EXPECT_EQ(res2, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb1, bufs[i]);
        EXPECT_EQ(fb2, bufs[i-1]);
    }
    // steps 20 - 21
    {
        FrameBuffer fb1;
        auto res1 = s.Get(client1, fb1, 100);
        FrameBuffer fb2;
        auto res2 = s.Get(client2, fb2, 100);
        EXPECT_EQ(res1, ISplitterError::TIMEOUT);
        EXPECT_EQ(res2, ISplitterError::NO_ERROR);
        EXPECT_EQ(fb2, bufs[9]);
    }

    {
        auto lock = s.BeginClientsIteration();

        ClientID id;
        size_t latency;
        size_t drops;
        size_t count;
        EXPECT_TRUE(s.ClientGetCount(&count, lock));
        EXPECT_EQ(count, 2);

        EXPECT_TRUE(s.ClientGetByIndex(0, &id, &latency, &drops, lock));
        EXPECT_EQ(drops, 0);

        EXPECT_TRUE(s.ClientGetByIndex(1, &id, &latency, &drops, lock));
        EXPECT_EQ(drops, 3);
    }
}
