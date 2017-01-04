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

#include "SerialPort.h"
#include <stdio.h>
#include <stdlib.h>

static HANDLE m_portHandle = 0;
static HANDLE m_workingThread = 0;
static DWORD  m_workingThreadId = 0;
static int    m_timeoutMilliSeconds = 0;
static void (*m_OnDataReceivedHandler)(const unsigned char* pData, int dataLength) = NULL;
static void (*m_OnDataSentHandler)(void) = NULL;
static CRITICAL_SECTION m_criticalSectionRead;
static CRITICAL_SECTION m_criticalSectionWrite;

DWORD WINAPI    SerialPort_WaitForData( LPVOID lpParam );
int             SerialPort__WriteBuffer(const unsigned char* pData, int dataLength);
int             SerialPort__ReadBuffer(unsigned char* pData, int dataLength, int timeOutMS);

#define SERIALPORT_INTERNAL_TIMEOUT 1

void SerialPort_Initialize(void)
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

void SerialPort_Uninitialize(void)
{
    if (m_portHandle)
    {
        SerialPort_Close();  
    }
    DeleteCriticalSection(&m_criticalSectionRead);
    DeleteCriticalSection(&m_criticalSectionWrite);
}

int SerialPort_GetMaxTimeout()
{
    return m_timeoutMilliSeconds;
}

BOOL SerialPort_OpenAsync(int comPortNumber, int baudRate,                             
                     void (*OnDataReceivedHandler)(const unsigned char* pData, int dataLength),
                     void (*OnDataSentHandler)(void),                         
                     int timeoutMS
                    )

{
    BOOL result = SerialPort_Open(comPortNumber, baudRate, timeoutMS);
    if (result)
    {
        m_OnDataReceivedHandler = OnDataReceivedHandler;
        m_OnDataSentHandler     = OnDataSentHandler;
        if (m_OnDataSentHandler || m_OnDataReceivedHandler)
        {
            m_workingThread = CreateThread(NULL, 0, SerialPort_WaitForData, NULL, 0, &m_workingThreadId);
        }
    }
    return result;
}

BOOL SerialPort_Open(int comPortNumber, int baudRate, int timeoutMS)
{
	char portName[8];
	int  portNameLength;
    DCB  portSettings;
    COMMTIMEOUTS portTimeOuts;

	if (comPortNumber<0) return FALSE;
	if (comPortNumber>255) return FALSE;
    if (timeoutMS>15000) return FALSE;

	portNameLength = sprintf(portName, "COM%i", comPortNumber );

	m_portHandle = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if ((m_portHandle==0) || ((unsigned long)m_portHandle==0xffffffff))
    {
        m_portHandle = 0;
        return FALSE;
    }

    m_timeoutMilliSeconds = timeoutMS;
	
	memset(&portSettings, 0, sizeof(portSettings));

	portSettings.BaudRate = baudRate;
    portSettings.ByteSize = 8;
    portSettings.Parity   = NOPARITY;
    portSettings.StopBits = ONESTOPBIT;

	if (!SetCommState(m_portHandle, &portSettings))
	{
		return FALSE;
	}
                      
    memset(&portTimeOuts, 0, sizeof(portTimeOuts));
    portTimeOuts.ReadIntervalTimeout = SERIALPORT_INTERNAL_TIMEOUT;
    portTimeOuts.ReadTotalTimeoutMultiplier = 0;    
    portTimeOuts.ReadTotalTimeoutConstant = SERIALPORT_INTERNAL_TIMEOUT;    

    if (!SetCommTimeouts(m_portHandle, &portTimeOuts))
    {
        return FALSE;
    }
	return (m_portHandle!=0);
}

void SerialPort_Close()
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

BOOL SerialPort_IsOpen()
{
    return (m_portHandle!=NULL);
}

int SerialPort__WriteBuffer(const unsigned char* pData, int dataLength)
{
    DWORD bytesWritten = 0;

	if (m_portHandle==NULL)
	{
		return 0;
	}    

    if (!WriteFile(m_portHandle, pData, dataLength, &bytesWritten, NULL))
    {
        return 0;
    }    
	return (int)bytesWritten;   
}
	
int SerialPort__ReadBuffer(unsigned char* pData, int dataLength, int timeOutMS)
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
	return bytesReadTotal;
}

int SerialPort_ReadBuffer(unsigned char* pData, int dataLength, int timeOutMS)
{
    int result;
    EnterCriticalSection(&m_criticalSectionRead);
    result = SerialPort__ReadBuffer(pData, dataLength, timeOutMS);
    LeaveCriticalSection(&m_criticalSectionRead);
    return result;
}


int SerialPort_WriteBuffer(const unsigned char* pData, int dataLength)
{
    int result;
    EnterCriticalSection(&m_criticalSectionWrite);
    result = SerialPort__WriteBuffer(pData, dataLength);
    LeaveCriticalSection(&m_criticalSectionWrite);
    if (m_OnDataSentHandler)
    {
        m_OnDataSentHandler();
    }
    return result;
}


int SerialPort_WriteLine(char* pLine, BOOL addCRatEnd)
{
    int lineLength, result;

	if (m_portHandle==NULL)
	{
		return 0;
	}
    if (pLine==NULL)
    {
        return 0;
    }
    lineLength = strlen(pLine);
    if (lineLength==0)
    {
        return 0;
    }

    EnterCriticalSection(&m_criticalSectionWrite);
    result = SerialPort__WriteBuffer((unsigned char*)pLine, lineLength);
    if (pLine[lineLength-1]!=0x0D)
    {
        char cr = 13;
        result+=SerialPort__WriteBuffer((unsigned char*)&cr, 1);
    }
    LeaveCriticalSection(&m_criticalSectionWrite);
    if (m_OnDataSentHandler)
    {
        m_OnDataSentHandler();
    }
    return result;
}

int SerialPort_ReadLine(char* pLine, int maxBufferSize, int timeOutMS)
{
    int result;
	if (m_portHandle==NULL)
	{
		return 0;
	}
    if (pLine==NULL)
    {
        return 0;
    }

    EnterCriticalSection(&m_criticalSectionRead);
    result = SerialPort__ReadBuffer((unsigned char*)pLine, maxBufferSize-1, timeOutMS);	
    if (result<maxBufferSize)
    {
        pLine[result] = 0;
    }
    LeaveCriticalSection(&m_criticalSectionRead);
    return result;
}

DWORD WINAPI SerialPort_WaitForData( LPVOID lpParam )
{
    static unsigned char packet[64];
    int packetSize = sizeof(packet);
    int bytesRead = 0;

    while(SerialPort_IsOpen())
    {
         bytesRead = SerialPort_ReadBuffer(packet, packetSize, SERIALPORT_INTERNAL_TIMEOUT);
         if (bytesRead)
         {
             if (m_OnDataReceivedHandler)
             {
                 m_OnDataReceivedHandler(packet, bytesRead);
             }
         }
    }    
    return 0;
}

