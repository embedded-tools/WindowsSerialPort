/*
* Windows Serial Port for Windows API
*
* Copyright (c) 2016 Ondrej Sterba <osterba@inbox.com>
*
* https://github.com/bezbot/WindowsSerialPort
*
* Permission to use, copy, modify, distribute and sell this software
* and its documentation for any purpose is hereby granted without fee
* provided that the above copyright notice appear in all copies and
* that both that copyright notice and this permission notice appear
* in supporting documentation.
* It is provided "as is" without express or implied warranty.
*
*/

#include "SerialPort.h"
#include <stdio.h>
#include <stdlib.h>

TSerialPort::TSerialPort()
{
    m_portHandle = NULL;
    m_OnDataReceivedHandler = NULL;	
    m_OnDataSentHandler = NULL;	
    m_timeoutMilliSeconds = 0;
    m_maxPacketLength = 0;
    m_workingThread = NULL;
    m_workingThreadId = 0;
    InitializeCriticalSection(&m_criticalSectionRead);
    InitializeCriticalSection(&m_criticalSectionWrite);
}

TSerialPort::~TSerialPort()
{
    if (m_portHandle)
    {
        Close();        
    }
    DeleteCriticalSection(&m_criticalSectionRead);
    DeleteCriticalSection(&m_criticalSectionWrite);
}

int TSerialPort::GetMaxPacketSize()
{
    return m_maxPacketLength;  
}

int TSerialPort::GetMaxTimeout()
{
    return m_timeoutMilliSeconds;
}

void* TSerialPort::GetDataReceivedHandler()
{
    return m_OnDataReceivedHandler;
}

void* TSerialPort::GetDataSentHandler()
{
    return m_OnDataSentHandler;
}

bool TSerialPort::OpenAsync(int comPortNumber, int baudRate,                             
                            void (*OnDataReceivedHandler)(const unsigned char* pData, int dataLength),
                            void (*OnDataSentHandler)(void),                         
                            int timeoutMS, int maxPacketLength 
                            )
                            
{
    bool result = Open(comPortNumber, baudRate, timeoutMS, maxPacketLength);
    if (result)
    {
        m_OnDataReceivedHandler = OnDataReceivedHandler;
        m_OnDataSentHandler     = OnDataSentHandler;
        if (m_OnDataSentHandler || m_OnDataReceivedHandler)
        {
            m_workingThread = CreateThread(NULL, 0, SerialPort_WaitForData, this, 0, &m_workingThreadId);
        }
    }
    return result;
}

bool TSerialPort::Open(int comPortNumber, int baudRate, int timeoutMS, int maxPacketLength)
{
    char portName[8];
    int  portNameLength;
    if (comPortNumber<0) return false;
    if (comPortNumber>255) return false;
    if (timeoutMS>15000) return false;
    
    portNameLength = sprintf(portName, "COM%i", comPortNumber );
    
    m_portHandle = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    m_timeoutMilliSeconds = timeoutMS;
    m_maxPacketLength = maxPacketLength;
    
    DCB portSettings;
    memset(&portSettings, 0, sizeof(portSettings));
    
    portSettings.BaudRate = baudRate;
    portSettings.ByteSize = 8;
    portSettings.Parity   = NOPARITY;
    portSettings.StopBits = ONESTOPBIT;
    
    if (!SetCommState(m_portHandle, &portSettings))
    {
        return false;
    }
    
    COMMTIMEOUTS portTimeOuts;                   
    memset(&portTimeOuts, 0, sizeof(portTimeOuts));
    portTimeOuts.ReadTotalTimeoutConstant = timeoutMS;
    portTimeOuts.WriteTotalTimeoutConstant = MAXDWORD;  
    if (!SetCommTimeouts(m_portHandle, &portTimeOuts))
    {
        return false;
    }
    return (m_portHandle!=0);
}

void TSerialPort::Close()
{    
    EnterCriticalSection(&m_criticalSectionRead);  //prevents port closing if ReadBuffer  is not complete
    EnterCriticalSection(&m_criticalSectionWrite); //prevents port closing if WriteBuffer is not complete
    if (m_portHandle)
    {
        CloseHandle(m_portHandle);			
        m_portHandle = NULL;
        if (m_workingThread)
        {
            //wait max. 30 sec for thread termination
            for(int i =0; i<=300; i++)
            {
                Sleep(100);
                if (!m_workingThread)
                {
                    break;
                }
            }
            m_workingThread = NULL;
            m_workingThreadId = 0;
        }        
    }
    LeaveCriticalSection(&m_criticalSectionRead);
    LeaveCriticalSection(&m_criticalSectionWrite);
}

bool TSerialPort::IsOpen()
{
    return (m_portHandle!=0);
}

int TSerialPort::__WriteBuffer(const unsigned char* pData, int dataLength)
{
    if (m_portHandle==NULL)
    {
        return 0;
    }
    DWORD bytesWritten = 0;
    
    if (!WriteFile(m_portHandle, pData, dataLength, &bytesWritten, NULL))
    {
        return 0;
    }    
    return (int)bytesWritten;   
}

int TSerialPort::__ReadBuffer(unsigned char* pData, int dataLength)
{
    if (m_portHandle==NULL)
    {
        return 0;
    }
    
    DWORD portState;
    DWORD bytesRead = 0;
    
    BOOL res = ReadFile(m_portHandle, pData, dataLength, &bytesRead, NULL);
    if ((!res) && (!bytesRead))
    {
        SetCommMask(m_portHandle, EV_RXCHAR | EV_ERR);
        WaitCommEvent(m_portHandle, &portState, 0);
        
        res = ReadFile(m_portHandle, pData, dataLength, &bytesRead, NULL);
    }
    if (bytesRead)
    {
        if (m_OnDataReceivedHandler)
        {
            m_OnDataReceivedHandler(pData, bytesRead);
        }
    }
    
    return bytesRead;
}

int TSerialPort::ReadBuffer(unsigned char* pData, int dataLength)
{
    EnterCriticalSection(&m_criticalSectionRead);
    int result = __ReadBuffer(pData, dataLength);
    LeaveCriticalSection(&m_criticalSectionRead);
    return result;
}


int TSerialPort::WriteBuffer(const unsigned char* pData, int dataLength)
{
    EnterCriticalSection(&m_criticalSectionWrite);
    int result = __WriteBuffer(pData, dataLength);
    LeaveCriticalSection(&m_criticalSectionWrite);
    if (m_OnDataSentHandler)
    {
        m_OnDataSentHandler();
    }
    return result;
}


int TSerialPort::WriteLine(char* pLine, bool addCRatEnd)
{
    
    if (m_portHandle==NULL)
    {
        return 0;
    }
    if (pLine==NULL)
    {
        return 0;
    }
    int lineLength = strlen(pLine);
    if (lineLength==0)
    {
        return 0;
    }
    
    EnterCriticalSection(&m_criticalSectionWrite);
    int result = __WriteBuffer((unsigned char*)pLine, lineLength);
    if (pLine[lineLength-1]!=0x0D)
    {
        char cr = 13;
        result+=__WriteBuffer((unsigned char*)&cr, 1);
    }
    LeaveCriticalSection(&m_criticalSectionWrite);
    if (m_OnDataSentHandler)
    {
        m_OnDataSentHandler();
    }
    return result;
}

int TSerialPort::ReadLine(char* pLine, int maxLineLength)
{
    if (m_portHandle==NULL)
    {
        return 0;
    }
    if (pLine==NULL)
    {
        return 0;
    }
    
    EnterCriticalSection(&m_criticalSectionRead);
    int result = __ReadBuffer((unsigned char*)pLine, maxLineLength);	
    if (result<maxLineLength)
    {
        pLine[result] = 0;
    }
    LeaveCriticalSection(&m_criticalSectionRead);
    return result;
}

DWORD WINAPI SerialPort_WaitForData( LPVOID lpParam )
{
    TSerialPort* serialPort = (TSerialPort*)lpParam;
    int packetSize = serialPort->GetMaxPacketSize();
    if (packetSize<0) return -1;
    if (packetSize>0x10000) return -1;
    int bytesRead = 0;
    
    unsigned char* buffer = (unsigned char*)malloc(packetSize);
    while(serialPort->IsOpen())
    {
        bytesRead = serialPort->ReadBuffer(buffer, packetSize);
    }    
    return 0;
}

