/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is derived from deviationTx project for Arduino.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 see <http://www.gnu.org/licenses/>
*/

#ifndef _PROTOCOL_SYMA_H_
#define _PROTOCOL_SYMA_H_

#include "DeviceNRF24L01.h"
#include "RFProtocol.h"

class RFProtocolSyma : public RFProtocol
{
#define MAX_PACKET_SIZE     16  // X11,X12,X5C-1 10-byte, X5C 16-byte
#define ADDR_BUF_SIZE        5
#define MAX_RF_CHANNELS     17

public:
    RFProtocolSyma(u32 id):RFProtocol(id) { }
    ~RFProtocolSyma() { close(); }

// for protocol
    virtual int  init(u8 bind);
    virtual int  close(void);
    virtual int  reset(void);
    virtual int  getInfo(s8 id, u8 *data);
    virtual u16  callState(u32 now, u32 expected);

private:
    u8   getCheckSum(u8 *data);
    u8   checkStatus(void);
    u8   getChannel(u8 id);
    void getControls(u8* throttle, u8* rudder, u8* elevator, u8* aileron, u8* flags);
    void buildPacketX5C(u8 bind);
    void buildPacket(u8 bind);
    void sendPacket(u8 bind);
    void initRxTxAddr(void);
    void init1(void);
    void init2(void);
    void init3(void);
    void setRFChannel(u8 address);

// variables
    DeviceNRF24L01  mDev;
    u32  mPacketCtr;
    u16  mBindCtr;
    u8   mRFChanBufs[MAX_RF_CHANNELS];
    u8   mPacketBuf[MAX_PACKET_SIZE];
    u8   mRxTxAddrBuf[ADDR_BUF_SIZE];

    u8   mCurRFChan;
    u8   mRFChanCnt;
    u8   mPacketSize;
    u8   mState;

protected:

};

#endif
