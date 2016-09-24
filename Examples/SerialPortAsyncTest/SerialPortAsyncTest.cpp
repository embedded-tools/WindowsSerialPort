#include "SerialPort.h"
#include <stdio.h>
#include "StateMachine.h"

StateMachine stateMachine;

void CommandReceived(unsigned char command, unsigned char param1, unsigned char param2)
{
    printf("Info: Command received (Cmd: %i, Param1: %i, Param2: %i)\r\n", command, param1, param2);
}

void DataReceived (const unsigned char* pData, int dataLength)
{
    for(int i = 0; i<dataLength; i++)
    {
        stateMachine.OnByteReceived(pData[i]);
    }
}

void DataSent()
{
    printf("Info: Data sent\r\n");
}


int main(int argc, char* argv[])
{
    stateMachine.SetEventHandler(CommandReceived);
    
    TSerialPort com1;
    com1.OpenAsync(3, 9600, DataReceived, DataSent);
    while(1)
    {        
        com1.WriteLine("050010");
        Sleep(1000);
    }
    com1.Close();
    return 0;
}