#include "SerialPort.h"
#include <stdio.h>


int main(int argc, char* argv[])
{
    char response[64];
    
    TSerialPort com1;
    com1.Open(3, 9600, 1000);
    while(1)
    {        
        com1.WriteLine("050010");
        com1.ReadLine(response, sizeof(response));
        
        printf("Response: %s\r\n", response);
    }
    com1.Close();
    return 0;
}
