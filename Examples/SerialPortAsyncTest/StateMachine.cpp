#include "StateMachine.h"
#include <stdlib.h>

StateMachine::StateMachine()
{
    m_commandReceivedHandler = NULL;
    m_state = sMagicByte;
    m_command = 0;     
    m_param1 = 0;
    m_param2 = 0;
}

void StateMachine::SetEventHandler(void (*commandReceivedHandler)(unsigned char command, unsigned char param1, unsigned char param2))
{
    m_commandReceivedHandler = commandReceivedHandler;
}

void StateMachine::OnByteReceived(unsigned char b)
{
    switch (m_state)
    {
    case sMagicByte:
        {
            if (b==MAGICBYTE)
            {
                m_state = sCommand;
            }
        }
        break;
        
    case sCommand:
        {
            m_command = b;
            m_state = sParam1;
        }
        break;
        
    case sParam1:
        {
            m_param1 = b;
            m_state = sParam2;
        }
        break;
        
    case sParam2:
        {
            m_param2 = b;
            m_state = sMagicByte;
            
            if (m_commandReceivedHandler)
            {
                m_commandReceivedHandler(m_command, m_param1, m_param2);
            }
        }
        break;
        
    }
    
}