/*
* Windows Serial Port for Windows API
*
* Copyright (c) 2016 Ondrej Sterba <osterba@inbox.com>
*
* https://github.com/embedded-tools/WindowsSerialPort
*
* Permission to use, copy, modify, distribute and sell this software
* and its documentation for any purpose is hereby granted without fee
* provided that the above copyright notice appear in all copies and
* that both that copyright notice and this permission notice appear
* in supporting documentation.
* It is provided "as is" without express or implied warranty.
*
*/

#include "SerialPort.hpp"
#include <stdio.h>
#include <stdlib.h>

#define SERIALPORT_INTERNAL__TIMEOUT 1

TSerialPort::TSerialPort()
{
    m_portHandle = NULL;
    m_OnDataReceivedHandler = NULL;	
    m_OnDataSentHandler = NULL;	
    m_timeoutMilliSeconds = 0;
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

bool TSerialPort::OpenAsync(int comPortNumber, 
                            int baudRate,                             
                            void (*OnDataReceivedHandler)(const unsigned char* pData, int dataLength),
                            void (*OnDataSentHandler)(void),                         
                            int timeoutMS 
                            )                            
{
    bool result = Open(comPortNumber, baudRate, timeoutMS);
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

bool TSerialPort::Open(int comPortNumber, int baudRate, int timeoutMS)
{
    char portName[12];
    int  portNameLength;
    if (comPortNumber<0) return false;
    if (comPortNumber>255) return false;
    if (timeoutMS>15000) return false;
    
    portNameLength = sprintf(portName, "\\\\.\\COM%i", comPortNumber );
    
    m_portHandle = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if ((m_portHandle==0) || ((unsigned long)m_portHandle==0xffffffff))
    {
        m_portHandle = 0;
        return false;
    }

    m_timeoutMilliSeconds = timeoutMS;
    
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
    portTimeOuts.ReadIntervalTimeout = SERIALPORT_INTERNAL_TIMEOUT;
    portTimeOuts.ReadTotalTimeoutMultiplier = 0;    
    portTimeOuts.ReadTotalTimeoutConstant = SERIALPORT_INTERNAL_TIMEOUT;    

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
        Sleep(SERIALPORT_INTERNAL_TIMEOUT*4);
        m_workingThread = NULL;
        m_workingThreadId = 0;      
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

int TSerialPort::__ReadBuffer(unsigned char* pData, int dataLength, int timeOutMS)
{
    DWORD bytesRead,bytesReadTotal;
    int   timeOutCounter, bytesLeft;

	if (m_portHandle==NULL)
	{
		return 0;
	}
    if (timeOutMS<0)
    {
        timeOutMS = m_timeoutMilliSeconds;
    }
    
    bytesRead = 0;
    bytesReadTotal = 0;
    timeOutCounter = timeOutMS;
    bytesLeft = dataLength;

    if (timeOutMS<SERIALPORT_INTERNAL_TIMEOUT*2)
    {
        Sleep(timeOutMS);
        ReadFile(m_portHandle, pData, dataLength, &bytesRead, NULL);
        bytesReadTotal += bytesRead;
    } else {
        while(timeOutCounter>0)
        {            
            bytesRead = 0;
            ReadFile(m_portHandle, pData+bytesReadTotal, dataLength, &bytesRead, NULL);
            if (bytesRead)
            {
                dataLength     -= bytesRead;
                bytesReadTotal += bytesRead;
                if (dataLength==0) break;
                timeOutCounter = timeOutMS;
            } else {
                timeOutCounter -= SERIALPORT_INTERNAL_TIMEOUT;
            }            
        }
    }
    if (bytesReadTotal)
    {
        if (m_OnDataReceivedHandler)
        {
            m_OnDataReceivedHandler(pData, bytesReadTotal);
        }
    }
	return bytesReadTotal;
}

int TSerialPort::ReadBuffer(unsigned char* pData, int dataLength, int timeOutMS)
{
    EnterCriticalSection(&m_criticalSectionRead);
    int result = __ReadBuffer(pData, dataLength, timeOutMS);
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

int TSerialPort::ReadLine(char* pLine, int maxBufferSize, int timeOutMS)
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
    int result = __ReadBuffer((unsigned char*)pLine, maxBufferSize-1, timeOutMS);	
    if (result<maxBufferSize)
    {
        pLine[result] = 0;
    }
    LeaveCriticalSection(&m_criticalSectionRead);
    return result;
}

DWORD WINAPI SerialPort_WaitForData( LPVOID lpParam )
{
    TSerialPort* serialPort = (TSerialPort*)lpParam;
    
    static unsigned char packet[64];
    int packetSize = sizeof(packet);   
    while(serialPort->IsOpen())
    {
        serialPort->ReadBuffer(packet, packetSize, SERIALPORT_INTERNAL_TIMEOUT);
    }    
    return 0;
}

