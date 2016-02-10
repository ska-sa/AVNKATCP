
//System includes
#include <iostream>
#include <stdlib.h>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/make_shared.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#endif

//Local includes
#include "KATCPClientBase.h"

using namespace std;

cKATCPClientBase::cKATCPClientBase() :
    m_bDisconnectFlag(false)
{
    //Note don't call connect in base constructor as it may call derived versions of virtual thread functions which may be
    //correctly populated at the time of the call here.

    //Calling code should call connect after construction of derived class.
}

cKATCPClientBase::~cKATCPClientBase()
{
    disconnect();
}

void cKATCPClientBase::connect(const string &strServerAddress, uint16_t u16Port, const string &strDescription)
{
    cout << "cKATCPClientBase::connect() Connecting to KATCP server: " << strServerAddress << ":" << u16Port << endl;

    //Store config parameters in members
    m_strServerAddress      = strServerAddress;
    m_u16Port               = u16Port;
    m_strDescription        = strDescription;

    //Connect the socket
    m_pSocket.reset(new cInterruptibleBlockingTCPSocket());

    while(!disconnectRequested())
    {
        if(m_pSocket->openAndConnect(m_strServerAddress, m_u16Port, 500))
            break;

        cout << "cKATCPClientBase::connect() Reached timeout attempting to connect to server " << m_strServerAddress << ":" << m_u16Port << ". Retrying in 0.5 seconds..." << endl;
        boost::this_thread::sleep(boost::posix_time::milliseconds(500));
    }

    sendConnected(true, m_strServerAddress, m_u16Port, m_strDescription);

    cout << "cKATCPClientBase::connect() successfully connected KATCP server " << m_strServerAddress << ":" << m_u16Port << "." << endl;

    //Launch KATCP client processing. A thead for sending from the send queue and another for receiving and processing
    m_pSocketReadThread.reset(new boost::thread(&cKATCPClientBase::threadReadFunction, this));
    m_pSocketWriteThread.reset(new boost::thread(&cKATCPClientBase::threadWriteFunction, this));
}

void cKATCPClientBase::disconnect()
{
    cout << "cKATCPClientBase::disconnect() Disconnecting KATCP client..." << endl;

    {
        boost::unique_lock<boost::shared_mutex> oLock(m_oFlagMutex);

        m_bDisconnectFlag = true;
        m_pSocket->cancelCurrrentOperations();

        m_oConditionWriteQueueNoLongerEmpty.notify_all();
    }

    m_pSocketReadThread->join();
    m_pSocketReadThread.reset();

    m_pSocketWriteThread->join();
    m_pSocketWriteThread.reset();

    sendConnected(false);

    cout << "cKATCPClientBase::disconnect() KATCP disconnected." << endl;
}

bool cKATCPClientBase::disconnectRequested()
{
    //Thread safe function to check the disconnect flag
    boost::shared_lock<boost::shared_mutex> oLock(m_oFlagMutex);

    return m_bDisconnectFlag;
}

vector<string> cKATCPClientBase::readNextKATCPMessage(uint32_t u32Timeout_ms)
{
    //Read KATCP message from connected socket and return
    //a vector of tokens making up the string
    //tokens are space delimted in the KATCP telnet-like protocol

    bool bFullMessage = false;
    string strKATCPMessage;

    do
    {
        bFullMessage = m_pSocket->readUntil( strKATCPMessage, string("\n"), u32Timeout_ms);

        //Return an empty vector if disconnect requested or if no characters have been received.
        if(disconnectRequested() || !strKATCPMessage.length())
            return vector<string>();

        //readUntil function will append to the message string if each iteration if the stop character is not reached.
    }
    while(!bFullMessage);

    return tokeniseString(strKATCPMessage, string(" "));
}

void cKATCPClientBase::threadReadFunction()
{
    cout << "cKATCPClientBase::threadReadFunction(): Entered thread read function." << endl;

    vector<string> vstrMessageTokens;

    while(!disconnectRequested())
    {
        vstrMessageTokens = readNextKATCPMessage();

        if(!vstrMessageTokens.size())
            continue;

        processKATCPMessage(vstrMessageTokens);
    }

    cout << "cKATCPClientBase::threadReadFunction(): Leaving thread read function." << endl;
}

void cKATCPClientBase::threadWriteFunction()
{
    cout << "cKATCPClientBase::threadReadFunction(): Entered thread write function." << endl;

    string strMessageToSend;

    while(!disconnectRequested())
    {
        {
            boost::unique_lock<boost::mutex> oLock(m_oWriteQueueMutex);

            //If the queue is empty wait for data
            if(!m_qstrWriteQueue.size())
            {
                if(!m_oConditionWriteQueueNoLongerEmpty.timed_wait(oLock, boost::posix_time::milliseconds(500)) )
                {
                    //Timeout after 500 ms then check again (Loop restarts)
                    continue;
                }
            }

            //Check size again
            if(!m_qstrWriteQueue.size())
                continue;

            //Make a of copy of the string and pop it from the queue
            strMessageToSend = m_qstrWriteQueue.back();
            m_qstrWriteQueue.pop();
        }

        //Write the data to the socket
        //reattempt to send on timeout or failure
        while(!m_pSocket->write(strMessageToSend, 500));
        {
            //Check for shutdown in between attempts
            if(disconnectRequested())
                return;
        }
    }

    cout << "cKATCPClientBase::threadReadFunction(): Leaving thread write function." << endl;
}

void cKATCPClientBase::sendConnected(bool bConnected, const string &strHostAddress, uint16_t u16Port, const string &strDescription)
{
    boost::shared_lock<boost::shared_mutex> oLock;

    for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size(); ui++)
    {
        m_vpCallbackHandlers[ui]->connected_callback(bConnected, strHostAddress, u16Port, strDescription);
    }

    for(uint32_t ui = 0; ui < m_vpCallbackHandlers_shared.size(); ui++)
    {
        m_vpCallbackHandlers_shared[ui]->connected_callback(bConnected, strHostAddress, u16Port, strDescription);
    }
}

void cKATCPClientBase::registerCallbackHandler(cCallbackInterface *pNewHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpCallbackHandlers.push_back(pNewHandler);

    cout << "cKATCPClientBase::registerCallbackHandler(): Successfully registered callback handler: " << pNewHandler << endl;
}

void cKATCPClientBase::registerCallbackHandler(boost::shared_ptr<cCallbackInterface> pNewHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpCallbackHandlers_shared.push_back(pNewHandler);

    cout << "cKATCPClientBase::registerCallbackHandler(): Successfully registered callback handler: " << pNewHandler.get() << endl;
}

void cKATCPClientBase::deregisterCallbackHandler(cCallbackInterface *pHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size();)
    {
        if(m_vpCallbackHandlers[ui] == pHandler)
        {
            m_vpCallbackHandlers.erase(m_vpCallbackHandlers.begin() + ui);

            cout << "cKATCPClientBase::deregisterCallbackHandler(): Deregistered callback handler: " << pHandler << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cKATCPClientBase::deregisterCallbackHandler(): Warning: Deregistering callback handler: " << pHandler << " failed. Object instance not found." << endl;
    }
}

void cKATCPClientBase::deregisterCallbackHandler(boost::shared_ptr<cCallbackInterface> pHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpCallbackHandlers_shared.size();)
    {
        if(m_vpCallbackHandlers_shared[ui].get() == pHandler.get())
        {
            m_vpCallbackHandlers_shared.erase(m_vpCallbackHandlers_shared.begin() + ui);

            cout << "cKATCPClientBase::deregisterCallbackHandler(): Deregistered callback handler: " << pHandler.get() << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cKATCPClientBase::deregisterCallbackHandler(): Warning: Deregistering callback handler: " << pHandler.get() << " failed. Object instance not found." << endl;
    }
}

std::vector<std::string> cKATCPClientBase::tokeniseString(const std::string &strInputString, const std::string &strSeparators)
{
    //This funciton is not complete efficient due to extra memory copies of filling the std::vector
    //It will also be copied again on return.
    //It does simply the calling code and should be adequate in the context of most KATCP control clients.

    boost::char_separator<char> oSeparators(strSeparators.c_str());
    boost::tokenizer< boost::char_separator<char> > oTokens(strInputString, oSeparators);

    vector<string> vstrTokens;

    for(boost::tokenizer< boost::char_separator<char> >::iterator it = oTokens.begin(); it != oTokens.end(); ++it)
    {
        vstrTokens.push_back(*it);
    }

    return vstrTokens;
}
