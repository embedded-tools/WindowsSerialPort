#ifndef STATEMACHINE___H
#define STATEMACHINE___H

#define MAGICBYTE 0x38

enum State
{
    sMagicByte,
        sCommand,
        sParam1,
        sParam2,
};


class StateMachine
{
private:    
    State         m_state;
    unsigned char m_command;
    unsigned char m_param1;
    unsigned char m_param2;
    
    void (*m_commandReceivedHandler)(unsigned char command, unsigned char param1, unsigned char param2);
    
public:
    StateMachine();
    void SetEventHandler(void (*commandReceivedHandler)(unsigned char command, unsigned char param1, unsigned char param2));
    void OnByteReceived(unsigned char b);
    
};

#endif