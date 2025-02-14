#include "VoiceChatSession.h"
#include "Log.h"

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

void VoiceChatSession::SendPacket(ByteBuffer& packet)
{
    if (!IsOpen())
        return;

    MessageBuffer buffer(packet.size());
    buffer.Write(packet.contents(), packet.size());
    QueuePacket(std::move(buffer));
}

void VoiceChatSession::ReadHandler()
{
    MessageBuffer& packet = GetReadBuffer();
    while (packet.GetActiveSize() > 0)
    {
        // Handle incoming packets
        if (!HandlePing())
            break;
    }

    AsyncRead();
}

bool VoiceChatSession::HandlePing()
{
    // Handle ping packet
    return true;
}