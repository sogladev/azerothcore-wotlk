#include "VoiceChatSocket.h"
#include "Log.h"
#include "VoiceChatDefines.h"
#include "VoiceChatMgr.h"

struct VoiceChatServerPktHeader {
  uint16 cmd;
  uint16 size;

  const char *data() const { return reinterpret_cast<const char *>(this); }

  std::size_t headerSize() const { return sizeof(VoiceChatServerPktHeader); }
};

// ~VoiceChatSocket::VoiceChatSocket()
// {
//     LOG_DEBUG("voice.chat", "VoiceChatSocket destructor called");
//     if (IsOpen())
//     {
//         LOG_DEBUG("voice.chat", "Socket was still open during destruction");
//     }
// }

VoiceChatSocket::VoiceChatSocket(tcp::socket &&socket)
    : Socket<VoiceChatSocket>(std::move(socket)) {}

void VoiceChatSocket::Start() {
  // Initialize connection
  std::string ip_address = GetRemoteIpAddress().to_string();
  // LOG_TRACE("session", "Accepted connection from {}", ip_address);
  LOG_ERROR("sql.sql", "Accepted connection from {}", ip_address);
  LOG_DEBUG("session", "Accepted connection from {}", ip_address);
  AsyncRead();
}

bool VoiceChatSocket::Update() {
  if (!Socket<VoiceChatSocket>::Update())
    return false;

  // _queryProcessor.ProcessReadyCallbacks();

  // Add session-specific update logic
  return true;
}

void VoiceChatSocket::SendPacket(VoiceChatServerPacket pct)
{
    if (!IsOpen())
        return;

    // Create a properly formatted header
    MessageBuffer buffer;

    // Write header - assuming header is 4 bytes (2 bytes opcode + 2 bytes size)
    uint16 opcode = pct.GetOpcode();
    uint16 size = pct.size();

    buffer.Write(&opcode, sizeof(uint16));  // Write 2-byte opcode
    buffer.Write(&size, sizeof(uint16));    // Write 2-byte size

    // Write payload if any
    if (pct.size() > 0)
        buffer.Write(pct.contents(), pct.size());

    LOG_DEBUG("voice.chat", "Sending packet opcode={}, size={}", opcode, size);

    QueuePacket(std::move(buffer));
}

// void VoiceChatSocket::SendPacket(VoiceChatServerPacket packet) {
//   if (!IsOpen())
//     return;
//
//   // Create properly formatted packet
//   MessageBuffer buffer(sizeof(VoiceChatServerPktHeader) + packet.size());
//
//   // Write header
//   VoiceChatServerPktHeader header;
//   header.cmd = packet.GetOpcode();
//   header.size = packet.size();
//
//   buffer.Write(&header, sizeof(header));
//
//   // Write payload if any
//   if (packet.size() > 0)
//     buffer.Write(packet.contents(), packet.size());
//
//   QueuePacket(std::move(buffer));
//
//   // MessageBuffer buffer(packet.size());
//   // buffer.Write(packet.contents(), packet.size());
//   // QueuePacket(std::move(buffer));
// }

bool VoiceChatSocket::HandlePing() {
  // Handle ping packet
  return true;
}

bool VoiceChatSocket::ProcessIncomingData() {
  LOG_INFO("sql.sql", "VoiceChatSocket::ProcessIncomingData() Read Pong packet "
                      "sent from server"); // Log info for pong packets
  // Structured similar to VoiceChatServerSocket
  if (!IsOpen())
    return false;

  // Read header
  if (GetReadBuffer().GetActiveSize() < sizeof(VoiceChatServerPktHeader)) {
    LOG_ERROR("sql.sql", "Not enough data for header ({} < {})",
              GetReadBuffer().GetActiveSize(),
              sizeof(VoiceChatServerPktHeader));
    return false;
  }

  VoiceChatServerPktHeader header;
  std::memcpy(&header, GetReadBuffer().GetReadPointer(), sizeof(header));
  GetReadBuffer().ReadCompleted(sizeof(header));

  LOG_ERROR("sql.sql", "Processing packet: cmd={}, size={}", header.cmd,
            header.size);

  if (header.size < 2 || header.size > 0x2800) {
    // sLog.outError("VoiceChatServerSocket::ProcessIncomingData: client sent
    // malformed packet size = %u , cmd = %u", header->size, header->cmd);
    CloseSocket();
    return false;
  }

  // Read payload
  if (GetReadBuffer().GetActiveSize() < header.size)
    return false;

  std::vector<uint8> payload(header.size);
  std::memcpy(payload.data(), GetReadBuffer().GetReadPointer(), header.size);
  GetReadBuffer().ReadCompleted(header.size);

  // Pass to VoiceChatMgr
  auto packet = std::make_unique<VoiceChatServerPacket>(
      (VoiceChatServerOpcodes)header.cmd, header.size);
  packet->append(payload.data(), payload.size());
  sVoiceChatMgr.QueuePacket(std::move(packet));

  return true;
}

void VoiceChatSocket::ReadHandler() {
  LOG_ERROR("sql.sql", "ReadHandler called with {} bytes available",
            GetReadBuffer().GetActiveSize());

  MessageBuffer &packet = GetReadBuffer();
  while (packet.GetActiveSize() > 0) // Loop while there's data in the buffer
  {
    if (!ProcessIncomingData()) // Process data until buffer is exhausted
      break;                    // Break if processing fails
  }

  AsyncRead(); // Queue the next async read
}