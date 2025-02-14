#ifndef VoiceChatSocketMgr_h__
#define VoiceChatSocketMgr_h__

// #include "VoiceChatSession.h"
#include "Config.h"
#include "SocketMgr.h"
#include "VoiceChatSession.h"

class VoiceChatSocketMgr : public SocketMgr<VoiceChatSession>
{
    typedef SocketMgr<VoiceChatSession> BaseSocketMgr;

public:
    static VoiceChatSocketMgr& Instance()
    {
        static VoiceChatSocketMgr instance;
        return instance;
    }

    bool StartNetwork(Acore::Asio::IoContext& ioContext, std::string const& bindIp, uint16 port, int threadCount = 1) override
    {
        if (!BaseSocketMgr::StartNetwork(ioContext, bindIp, port, threadCount))
            return false;

        _acceptor->AsyncAcceptWithCallback<&VoiceChatSocketMgr::OnSocketAccept>();
        return true;
    }

protected:
    NetworkThread<VoiceChatSession>* CreateThreads() const override
    {
        NetworkThread<VoiceChatSession>* threads = new NetworkThread<VoiceChatSession>[1];
        return threads;
    }

    static void OnSocketAccept(tcp::socket&& sock, uint32 threadIndex)
    {
        Instance().OnSocketOpen(std::forward<tcp::socket>(sock), threadIndex);
    }
};

#define sVoiceChatSocketMgr VoiceChatSocketMgr::Instance()

#endif // VoiceChatSocketMgr_h__
