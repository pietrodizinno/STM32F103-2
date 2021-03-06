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

#ifndef _PROTOCOL_DSM_H
#define _PROTOCOL_DSM_H

#include "DeviceCYRF6936.h"
#include "RFProtocol.h"

class RFProtocolDSM : public RFProtocol
{
#define MAX_PACKET_SIZE        16
#define ADDR_BUF_SIZE           5
#define MFG_ID_SIZE             6
#define MAX_RF_CHANNELS        23


public:
    RFProtocolDSM(u32 id):RFProtocol(id) { }
    ~RFProtocolDSM() { close(); }

// for protocol
    virtual int  init(u8 bind);
    virtual int  close(void);
    virtual int  reset(void);
    virtual int  getInfo(s8 id, u8 *data);
    virtual u16  callState(u32 now, u32 expected);

private:
    void build_bind_packet(void);
    void build_data_packet(u8 upper);
    u8   get_pn_row(u8 channel);
    void cyrf_config(void);
    void initialize_bind_state(void);
    void cyrf_configdata(void);
    void set_sop_data_crc(void);
    void calc_dsmx_channel(void);
    u16  dsm2_cb(u32 now, u32 expected);
    void initialize(u8 bind);
    void buildPacket(void);
	u16  getChValue(s16 val, u16 max);

// variables
    DeviceCYRF6936  mDev;
    u8   mRFChanBufs[MAX_RF_CHANNELS];
    u8   mPacketBuf[MAX_PACKET_SIZE];
    u8   mMfgIDBuf[MFG_ID_SIZE];

    u8   mChanIdx;
    u8   mSOPCol;
    u8   mDataCol;
    u16  mCRC;
    u16  mState;
    u8   mChanCnt;
    u8   mIsBinding;

    u32  mFixedID;

protected:

};

#endif
