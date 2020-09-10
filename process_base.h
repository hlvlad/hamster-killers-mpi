#ifndef PROCESS_BASE_H_
#define PROCESS_BASE_H_

#include <cstdarg>
#include <mpl/mpl.hpp>

#pragma GCC diagnostic ignored "-Wformat-security"  // for log function

struct MessageBase;

class ProcessBase {
 private:
  int lamportClock = 0;
  const char* tag;
  const mpl::communicator& communicator;
  std::vector<int> broadcastScope;
  std::list<std::pair<const MessageBase*, mpl::status>> messageBuffer;

  void setTimestamp(MessageBase& message) const;
  int getTimestamp(const MessageBase& message) const;
  void storeInBuffer(const MessageBase* message, const mpl::status& status);
  const MessageBase* fetchFromBuffer(mpl::status& status, int sourceRank,
                                     std::vector<mpl::tag> tags);

  template <typename T>
  const bool receiveMultiTagHandle(
      int sourceRank, mpl::tag tag,
      std::unordered_map<
          int, std::function<void(const MessageBase*, const mpl::status&)>>
          messageHandlers) {
    T message{};
    const auto& status = communicator.recv(message, sourceRank, tag);
    if (messageHandlers.find((int)status.tag()) != messageHandlers.end()) {
      int timestamp = getTimestamp(message);
      lamportClock = std::max(lamportClock, timestamp) + 1;
      messageHandlers[(int)status.tag()](&message, status);
      return true;
    }
    storeInBuffer(new T(message), status);
    return false;
  }

 protected:
  const int rank;

  int advanceClock();
  void setBroadcastScope(std::vector<int> recipientRanks);

  template <typename... Args>
  void log(char const* const format, Args const&... args) const {
    printf("[Rank: %d] [Clock: %d] [%s] ", rank, lamportClock, tag);
    printf(format, args...);
    printf("\n");
  }

  template <typename T /* extends MessageBase */>
  void send(T& message, int recipientRank, mpl::tag tag) {
    lamportClock++;
    setTimestamp(message);
    communicator.send(message, recipientRank, tag);
  }

  template <typename T /* extends MessageBase */>
  void broadcast(T& message, mpl::tag tag) {
    lamportClock++;
    setTimestamp(message);
    for (int recipientRank : broadcastScope) {
      if (recipientRank == rank) continue;
      communicator.send(message, recipientRank, tag);
    }
  }

  template <typename T /* extends MessageBase */>
  void sendVector(std::vector<T>& message, int recipientRank, mpl::tag tag) {
    lamportClock++;
    for (int i = 0; i < message.size(); i++) {
      setTimestamp(message[i]);
    }
    communicator.send(message.begin(), message.end(), recipientRank, tag);
  }

  template <typename T /* extends MessageBase */>
  void broadcastVector(std::vector<T>& message, mpl::tag tag) {
    lamportClock++;
    for (int i = 0; i < message.size(); i++) {
      setTimestamp(message[i]);
    }
    for (int recipientRank : broadcastScope) {
      if (recipientRank == rank) continue;
      communicator.send(message.begin(), message.end(), recipientRank, tag);
    }
  }

  template <typename T /* extends MessageBase */>
  mpl::status receive(T& message, int sourceRank, mpl::tag tag) {
    mpl::status status;
    const MessageBase* bufferedMessage =
        fetchFromBuffer(status, sourceRank, std::vector<mpl::tag>{tag});
    if (bufferedMessage != nullptr) {
      message = *static_cast<const T*>(bufferedMessage);
      delete (bufferedMessage);
    } else {
      status = communicator.recv(message, sourceRank, tag);
    }
    int timestamp = getTimestamp(message);
    lamportClock = std::max(lamportClock, timestamp) + 1;
    return status;
  }

  template <typename T /* extends MessageBase */>
  mpl::status receiveAny(T& message, mpl::tag tag) {
    return receive(message, mpl::any_source, tag);
  }

  template <typename T /* extends MessageBase */>
  mpl::status receiveVector(std::vector<T>& message, int sourceRank,
                            mpl::tag tag) {
    const mpl::status& probe = communicator.probe(sourceRank, tag);
    int size = probe.get_count<T>();
    if ((size == mpl::undefined) || (size == 0)) exit(EXIT_FAILURE);
    message.resize(size);
    mpl::status status =
        communicator.recv(message.begin(), message.end(), sourceRank, tag);
    int timestamp = getTimestamp(message[0]);
    lamportClock = std::max(lamportClock, timestamp) + 1;
    return status;
  }

  template <typename T /* extends MessageBase */>
  mpl::status receiveVectorAny(std::vector<T>& message, mpl::tag tag) {
    return receiveVector(message, mpl::any_source, tag);
  }

  void receiveMultiTag(
      int sourceRank,
      std::unordered_map<
          int, std::function<void(const MessageBase*, const mpl::status&)>>
          messageHandlers);

 public:
  ProcessBase(const mpl::communicator& communicator, const char* tag = "");
  virtual void run(int maxRounds) = 0;
};

#endif  // PROCESS_BASE_H_