#ifndef KATCP_CLIENT_BASE_H
#define KATCP_CLIENT_BASE_H

//System includes
#include <vector>
#include <queue>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>
#endif

//Local includes
#include "../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingTCPSocket.h"

// This KATCP client is implemented manually using a client TCP socket and not the KATCP library.
// This makes portability a bit better especially with Windows.
// This is a base class which provideds the basic socket reading and writing and received text tokenising.
// There is a message send queue with thread safe write access (for adding new messages to send).
// There is a callback interface which can be extended for push-based behaviour to other classes deriving the callback interface.
// Implement the processKATCPMessage function according to requirements. This will typically either involved calling functions
// in the callback interface or storing values to member and providing accessor function (pull-based behavior).
// Note that a vector of callback handler pointers is provided. When deriving the callback interface, the callback handler pointer
// will need to be dynamically cast to the derived callback interface type before calling new derived callback functions as the
// vector stores pointers of the this base class type and not your new derived type.
// The send and receive are handled by seperate threads. This is because KATCP typically responds with a message type identifier
// and a reponse therefore needn't be tied to a initial message for most use cases.
// If this is not desired behavior the thread functions will need to be altered. They are left virtual for this reason.

class cKATCPClientBase
{
public:
    //Generic callback interface class (can be extended with other callback functions)
    class cCallbackInterface
    {
    public:
        virtual void                                                connected_callback(bool bConnected, const std::string &strHostAddress, uint16_t u16Port, const std::string &strDescription) = 0;
    };

    //Callback interface specifically for handling only connection / disconnections
    class cConnectionCallbackInterface
    {
    public:
        virtual void                                                connected_callback(bool bConnected, const std::string &strHostAddress, uint16_t u16Port, const std::string &strDescription) = 0;
    };

    cKATCPClientBase();
    virtual ~cKATCPClientBase();

    bool                                                            connect(const std::string &strServerAddress, uint16_t u16Port, const std::string &strDescription = std::string(""),
                                                                            bool bAutoReconnect = false);
    void                                                            disconnect();

    //Client requests
    void                                                            sendKATCPMessage(const std::string &strMessage); //Send a custom KATCP message to the connected peer
    virtual void                                                    onConnected(){;} //Overload with things to do once to connection

    //Callback handler registration
    void                                                            registerCallbackHandler(cCallbackInterface *pNewHandler);
    void                                                            registerCallbackHandler(boost::shared_ptr<cCallbackInterface> pNewHandler);
    void                                                            deregisterCallbackHandler(cCallbackInterface *pHandler);
    void                                                            deregisterCallbackHandler(boost::shared_ptr<cCallbackInterface> pHandler);

    void                                                            registerConnectionCallbackHandler(cConnectionCallbackInterface *pNewHandler);
    void                                                            registerConnectionCallbackHandler(boost::shared_ptr<cConnectionCallbackInterface> pNewHandler);
    void                                                            deregisterConnectionCallbackHandler(cConnectionCallbackInterface *pHandler);
    void                                                            deregisterConnectionCallbackHandler(boost::shared_ptr<cConnectionCallbackInterface> pHandler);

    //Functions used by the reading thread by left public as they may be usefull externally
    std::vector<std::string>                                        tokeniseString(const std::string &strInputString, const std::string &strSeperators);
    std::vector<std::string>                                        readNextKATCPMessage(uint32_t u32Timeout_ms = 0);

protected:
    virtual void                                                    threadReadFunction();
    virtual void                                                    threadWriteFunction();
    virtual void                                                    processKATCPMessage(const std::vector<std::string> &vstrMessageTokens) = 0;

    void                                                            threadAutoReconnectFunction();
    bool                                                            socketConnectFunction();

    //Send calls to all callback handlers:
    void                                                            sendConnected(bool bConnected, const std::string &strHostAddress = std::string(""),
                                                                                  uint16_t u16Port = 0, const std::string &strDescription = std::string(""));

    //Threads
    boost::scoped_ptr<boost::thread>                                m_pSocketReadThread;
    boost::scoped_ptr<boost::thread>                                m_pSocketWriteThread;
    boost::scoped_ptr<boost::thread>                                m_pConnectThread;

    //Sockets
    boost::scoped_ptr<cInterruptibleBlockingTCPSocket>              m_pSocket;

    //Members description operation state
    std::string                                                     m_strServerAddress;
    uint16_t                                                        m_u16Port;
    std::string                                                     m_strDescription;
    bool                                                            m_bAutoReconnect;

    //Other variables
    bool                                                            m_bDisconnectFlag;
    boost::shared_mutex                                             m_oFlagMutex;
    bool                                                            disconnectRequested();

    std::queue<std::string>                                         m_qstrWriteQueue;
    boost::condition_variable                                       m_oConditionWriteQueueNoLongerEmpty;
    boost::mutex                                                    m_oWriteQueueMutex;

    //Callback handlers
    std::vector<cCallbackInterface*>                                m_vpCallbackHandlers;
    std::vector<boost::shared_ptr<cCallbackInterface> >             m_vpCallbackHandlers_shared;
    std::vector<cConnectionCallbackInterface*>                      m_vpConnectionCallbackHandlers;
    std::vector<boost::shared_ptr<cConnectionCallbackInterface> >   m_vpConnectionCallbackHandlers_shared;
    boost::shared_mutex                                             m_oCallbackHandlersMutex;

    boost::mutex                                                    m_oKATCPMutex;
};

#endif // KATCP_CLIENT_BASE_H
