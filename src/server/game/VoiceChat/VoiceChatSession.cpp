#include "VoiceChatSession.h"
#include "Log.h"
#include "VoiceChatDefines.h"
#include "VoiceChatMgr.h"

struct VoiceChatServerPktHeader
{
    uint16 cmd;
    uint16 size;

    const char* data() const {
        return reinterpret_cast<const char*>(this);
    }

    std::size_t headerSize() const {
        return sizeof(VoiceChatServerPktHeader);
    }
};

VoiceChatSession::VoiceChatSession(tcp::socket&& socket) :
    Socket<VoiceChatSession>(std::move(socket))
{
}

void VoiceChatSession::Start()
{
    // Initialize connection
    std::string ip_address = GetRemoteIpAddress().to_string();
    // LOG_TRACE("session", "Accepted connection from {}", ip_address);
    LOG_ERROR("sql.sql", "Accepted connection from {}", ip_address);
    LOG_DEBUG("session", "Accepted connection from {}", ip_address);
    // AsyncRead();
}

bool VoiceChatSession::Update()
{
    if (!Socket<VoiceChatSession>::Update())
        return false;

    // _queryProcessor.ProcessReadyCallbacks();

    // Add session-specific update logic
    return true;
}

void VoiceChatSession::SendPacket(VoiceChatServerPacket packet)
{
    if (!IsOpen())
        return;

    MessageBuffer buffer(packet.size());
    buffer.Write(packet.contents(), packet.size());
    QueuePacket(std::move(buffer));
}

bool VoiceChatSession::HandlePing()
{
    // Handle ping packet
    return true;
}

bool VoiceChatSession::ProcessIncomingData()
{
    // Structured similar to VoiceChatServerSocket
    if (!IsOpen())
        return false;

    // Read header
    if (GetReadBuffer().GetActiveSize() < sizeof(VoiceChatServerPktHeader))
        return false;

    VoiceChatServerPktHeader header;
    std::memcpy(&header, GetReadBuffer().GetReadPointer(), sizeof(header));
    GetReadBuffer().ReadCompleted(sizeof(header));

    if (header.size < 2 || header.size > 0x2800)
    {
        // sLog.outError("VoiceChatServerSocket::ProcessIncomingData: client sent malformed packet size = %u , cmd = %u", header->size, header->cmd);
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
    auto packet = std::make_unique<VoiceChatServerPacket>((VoiceChatServerOpcodes)header.cmd, header.size);
    packet->append(payload.data(), payload.size());
    sVoiceChatMgr.QueuePacket(std::move(packet));

    return true;
}

void VoiceChatSession::ReadHandler()
{
    MessageBuffer& packet = GetReadBuffer();
    while (packet.GetActiveSize() > 0)  // Loop while there's data in the buffer
    {
        if (!ProcessIncomingData())     // Process data until buffer is exhausted
            break;                       // Break if processing fails
    }

    AsyncRead();                        // Queue the next async read
}