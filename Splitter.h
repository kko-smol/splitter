#pragma once

#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>

using FrameBuffer = std::shared_ptr<std::vector<uint8_t>>;
using ClientID = uint32_t;

enum class ISplitterError {
    NO_ERROR = 0,
    TIMEOUT,
    EOS,
    CLOSED,
    FLUSHED,
    UNKNOWN_CLIENT,
};

class ISplitter {
public:
    ISplitter(size_t max_buffers, size_t max_clients);
    virtual ~ISplitter() = default;

    bool InfoGet(size_t* _pzMaxBuffers, size_t* _pzMaxClients) const;

    ISplitterError Put(const FrameBuffer& _pVecPut, int32_t _nTimeOutMsec);
    ISplitterError Get(ClientID _nClientID, FrameBuffer& _pVecGet, int32_t _nTimeOutMsec);

    bool ClientAdd(ClientID* _unClientID);
    bool ClientRemove(ClientID _unClientID);
    bool ClientGetCount(size_t* _pnCount) const;

    std::unique_lock<std::mutex> BeginClientsIteration();
    bool ClientGetCount(size_t* _pnCount, std::unique_lock<std::mutex>& lock) const;
    bool ClientGetByIndex(size_t _zIndex, ClientID* _punClientID, size_t* _pzLatency, size_t* _pzDropped, std::unique_lock<std::mutex>& lock) const;

    ISplitterError Flush();
    void Close();
private:
    ClientID GenerateClinetId() const;
    
    std::atomic<ISplitterError> state_;
    mutable std::mutex mtx_;
    std::condition_variable push_cv_;
    std::map<ClientID, std::shared_ptr<class ClientCtx>> clients_;
    const size_t max_buffers_;
    const size_t max_clients_;
};

inline std::shared_ptr<ISplitter> SplitterCreate(size_t _zMaxBuffers, size_t _zMaxClients) {
    return std::make_shared<ISplitter>(_zMaxBuffers, _zMaxClients);
}