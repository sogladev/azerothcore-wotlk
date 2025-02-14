#ifndef __VOICECHATSESSION_H__
#define __VOICECHATSESSION_H__

#include "Socket.h"
#include "VoiceChatDefines.h"
#include <boost/asio/ip/tcp.hpp>

using boost::asio::ip::tcp;

class VoiceChatSession : public Socket<VoiceChatSession> {

public:
  explicit VoiceChatSession(tcp::socket &&socket);

  void Start() override;
  bool Update() override;
  // void SendPacket(ByteBuffer &packet);
  void SendPacket(VoiceChatServerPacket pct);

protected:
  void ReadHandler() override;

private:
  bool HandlePing();
  bool ProcessIncomingData();
  // bool HandleChannelCreated();
  // Add other handlers as needed
};

typedef Socket<VoiceChatSession> VoiceChatSocket;

#endif
