#include "Splitter.h"

#include <atomic>
#include <deque>
#include <iterator>
#include <cassert>

struct ClientCtx {
    ClientCtx(size_t max_buffers):
      to_delete_(false),
      drop_counter_(0),
      max_buffers_(max_buffers) {

    }
    
    void PrepareDelete() {
        to_delete_ = true;
        pull_cv_.notify_all();
    }

    bool WillDelete() const {
        return to_delete_;
    }

    bool QueueFull() const {
        return bufs_.size() == max_buffers_;
    }

    void PushBuffer(FrameBuffer fb) {
        bufs_.push_back(fb);
        while (bufs_.size() > max_buffers_) {
            bufs_.pop_front();
            ++drop_counter_;
        };
        pull_cv_.notify_all();
    }

    FrameBuffer PopBuffer() {
        auto res = *bufs_.begin();
        bufs_.pop_front();
        return res;
    }

    void Flush() {
        drop_counter_ += bufs_.size();
        bufs_.clear();
        pull_cv_.notify_all();
    }

    bool IsQueueEmpty() const {
        return bufs_.empty();
    }

    size_t GetLatency() const {
        return bufs_.size();
    }
    size_t GetDropped() const {
        return drop_counter_;
    }

    std::condition_variable pull_cv_;
private:
    std::deque<FrameBuffer> bufs_;
    std::atomic_bool to_delete_;
    size_t drop_counter_;
    size_t max_buffers_;
};

ISplitter::ISplitter(size_t max_buffers, size_t max_clients):
  state_(ISplitterError::NO_ERROR),
  max_buffers_(max_buffers),
  max_clients_(max_clients) {
}

bool ISplitter::InfoGet(size_t* _pzMaxBuffers, size_t* _pzMaxClients) const {
    std::lock_guard lck(mtx_);
    *_pzMaxBuffers = max_buffers_;
    *_pzMaxClients = max_clients_;
    return true;
}

ClientID ISplitter::GenerateClinetId() const {
    // this ID generator have an issue that ID will repeated after 4e9 client connections
    static std::atomic<uint32_t> id_counter_;
    return ++id_counter_;
}

bool ISplitter::ClientAdd(ClientID* _unClientID) {
    std::lock_guard lck(mtx_);
    if (clients_.size() < max_clients_) {
        auto id = GenerateClinetId();
        auto ctx = std::make_shared<ClientCtx>(max_buffers_);
        clients_.insert({id, ctx});
        *_unClientID = id;
        return true;
    } else {
        return false;
    }
}

bool ISplitter::ClientRemove(ClientID _unClientID) {
    std::lock_guard lck(mtx_);
    auto client_iter = clients_.find(_unClientID);
    if (client_iter != clients_.end()) {
        auto client = *client_iter;
        client.second->PrepareDelete();
        clients_.erase(client_iter);
        push_cv_.notify_all();
        return true;
    } else {
        return false;
    }
}

ISplitterError ISplitter::Put(const FrameBuffer& _pVecPut, int32_t _nTimeOutMsec) {
    auto exit_time = std::chrono::high_resolution_clock().now() + std::chrono::milliseconds(_nTimeOutMsec);
    std::unique_lock lck(mtx_);

    ISplitterError res = ISplitterError::NO_ERROR;

    std::deque<std::shared_ptr<ClientCtx>> stall;

    // first pass: put buffers to clients than doesn't stall and collect stalled
    for (auto client = clients_.begin(); client != clients_.end(); ++client) {
        if (client->second->WillDelete()) {
            continue;
        } else if (client->second->QueueFull()) {
            stall.push_back(client->second);
            continue;
        }
        client->second->PushBuffer(_pVecPut);
        client->second->pull_cv_.notify_all();
    }

    // until we have time - try to put buffers to clients
    while (stall.size() && (res != ISplitterError::TIMEOUT)) {
        res = push_cv_.wait_until(lck, exit_time) == std::cv_status::timeout ? ISplitterError::TIMEOUT : res;

        if (state_ != ISplitterError::NO_ERROR) {
            res = state_;
            stall.clear();
            state_ = ISplitterError::NO_ERROR;
            break;
        }

        for (auto client = stall.begin(); client < stall.end(); ++client) {
            if ((*client)->WillDelete()) {
                client = stall.erase(client);
            } else if (!(*client)->QueueFull()) {
                (*client)->PushBuffer(_pVecPut);
                client = stall.erase(client);
            }
        }
    }

    // now we can't wait - force push buffer to FIFO. it will drop old buffers
    for (auto client = stall.begin(); client != stall.end(); ++client) {
        if (!(*client)->WillDelete()) {
            (*client)->PushBuffer(_pVecPut);
        }
    }

    return res;
}

ISplitterError ISplitter::Get(ClientID _nClientID, FrameBuffer& _pVecGet, int32_t _nTimeOutMsec) {
    std::unique_lock lck(mtx_);
    ISplitterError res = ISplitterError::NO_ERROR;

    auto client_iter = clients_.find(_nClientID);
    if (client_iter == clients_.end()) {
        return ISplitterError::UNKNOWN_CLIENT;
    }
    auto client = client_iter->second;

    if (client->IsQueueEmpty()) {
        if (client->pull_cv_.wait_for(lck, std::chrono::milliseconds(_nTimeOutMsec)) == std::cv_status::timeout) {
            return ISplitterError::TIMEOUT;
        }
    }

    if (client->WillDelete()) {
        push_cv_.notify_all();
        return ISplitterError::EOS;
    }

    _pVecGet = client->PopBuffer();
    push_cv_.notify_all();
    return res;
}

std::unique_lock<std::mutex> ISplitter::BeginClientsIteration() {
    return std::unique_lock(mtx_);
}

bool ISplitter::ClientGetCount(size_t* _pnCount, std::unique_lock<std::mutex>& lock) const {
    assert(lock.owns_lock());
    *_pnCount = clients_.size();
    return true;
}

bool ISplitter::ClientGetCount(size_t* _pnCount) const {
    std::lock_guard lck(mtx_);
    *_pnCount = clients_.size();
    return true;
}

bool ISplitter::ClientGetByIndex(size_t _zIndex,
        ClientID* _punClientID, size_t* _pzLatency, size_t* _pzDropped,
        std::unique_lock<std::mutex>& lock) const {
    assert(lock.owns_lock());

    auto client_iter = clients_.begin();
    std::advance(client_iter, _zIndex);
    *_punClientID = client_iter->first;
    *_pzLatency = client_iter->second->GetLatency();
    *_pzDropped = client_iter->second->GetDropped();
    return true;
}

void ISplitter::Close() {
    std::lock_guard lck(mtx_);
    for (auto client = clients_.begin(); client != clients_.end(); ++client) {
        client->second->PrepareDelete();
    }
    clients_.clear();
    state_ = ISplitterError::CLOSED;
    push_cv_.notify_all();
}

ISplitterError ISplitter::Flush() {
    std::lock_guard lck(mtx_);
    for (auto client = clients_.begin(); client != clients_.end(); ++client) {
        client->second->Flush();
    }
    state_ = ISplitterError::FLUSHED;
    push_cv_.notify_all();

    return ISplitterError::NO_ERROR;
}