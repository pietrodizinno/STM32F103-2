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

#include <SPI.h>
#include "RFProtocolDevo.h"
#include "utils.h"

#define CHANNEL_CNT          8
#define PKTS_PER_CHANNEL     4
#define MAX_BIND_COUNT       0x1388
#define TELEMETRY_ENABLE     0x30

#define INITIAL_WAIT_uS      2400

enum PktState {
    DEVO_BIND,
    DEVO_BIND_SENDCH,
    DEVO_BOUND,
    DEVO_BOUND_1,
    DEVO_BOUND_2,
    DEVO_BOUND_3,
    DEVO_BOUND_4,
    DEVO_BOUND_5,
    DEVO_BOUND_6,
    DEVO_BOUND_7,
    DEVO_BOUND_8,
    DEVO_BOUND_9,
    DEVO_BOUND_10,
};

static const PROGMEM u8 SOPCODES[][8] = {
    /* Note these are in order transmitted (LSB 1st) */
    /* 0 */ {0x3C,0x37,0xCC,0x91,0xE2,0xF8,0xCC,0x91}, //0x91CCF8E291CC373C
    /* 1 */ {0x9B,0xC5,0xA1,0x0F,0xAD,0x39,0xA2,0x0F}, //0x0FA239AD0FA1C59B
    /* 2 */ {0xEF,0x64,0xB0,0x2A,0xD2,0x8F,0xB1,0x2A}, //0x2AB18FD22AB064EF
    /* 3 */ {0x66,0xCD,0x7C,0x50,0xDD,0x26,0x7C,0x50}, //0x507C26DD507CCD66
    /* 4 */ {0x5C,0xE1,0xF6,0x44,0xAD,0x16,0xF6,0x44}, //0x44F616AD44F6E15C
    /* 5 */ {0x5A,0xCC,0xAE,0x46,0xB6,0x31,0xAE,0x46}, //0x46AE31B646AECC5A
    /* 6 */ {0xA1,0x78,0xDC,0x3C,0x9E,0x82,0xDC,0x3C}, //0x3CDC829E3CDC78A1
    /* 7 */ {0xB9,0x8E,0x19,0x74,0x6F,0x65,0x18,0x74}, //0x7418656F74198EB9
    /* 8 */ {0xDF,0xB1,0xC0,0x49,0x62,0xDF,0xC1,0x49}, //0x49C1DF6249C0B1DF
    /* 9 */ {0x97,0xE5,0x14,0x72,0x7F,0x1A,0x14,0x72}, //0x72141A7F7214E597
};

void RFProtocolDevo::scramblePacket(void)
{
    for(u8 i = 0; i < 15; i++) {
        mPacketBuf[i + 1] ^= mMfgIDBuf[i % 4];
    }
}

void RFProtocolDevo::addPacketSuffix(void)
{
    u8 bind_state;

    if (mBoolFixedID) {
        if (mBindCtr > 0) {
            bind_state = 0xc0;
        } else {
            bind_state = 0x80;
        }
    } else {
        bind_state = 0x00;
    }

    mPacketBuf[10] = bind_state | (PKTS_PER_CHANNEL - mPacketCtr - 1);
    mPacketBuf[11] = *(mCurRFChPtr + 1);
    mPacketBuf[12] = *(mCurRFChPtr + 2);
    mPacketBuf[13] = mFixedID  & 0xff;
    mPacketBuf[14] = (mFixedID >> 8) & 0xff;
    mPacketBuf[15] = (mFixedID >> 16) & 0xff;
}

void RFProtocolDevo::buildBeaconPacket(int upper)
{
    u8  enable = 0;
    int max    = 8;
    int offset = 0;

    mPacketBuf[0] = ((CHANNEL_CNT << 4) | 0x07);
    if (upper) {
        mPacketBuf[0] += 1;
        max = 4;
        offset = 8;
    }

    for(int i = 0; i < max; i++) {
#ifdef BOGUS
        if (i + offset < Model.CHANNEL_CNT && Model.limits[i+offset].flags & CH_FAILSAFE_EN) {
            enable |= 0x80 >> i;
            mPacketBuf[i+1] = Model.limits[i+offset].failsafe;
        } else {
#endif /* BOGUS */
            mPacketBuf[i + 1] = 0;
#ifdef BOGUS
        }
#endif /* BOGUS */
    }
    mPacketBuf[9] = enable;
    addPacketSuffix();
}

void RFProtocolDevo::buildBindPacket(void)
{
    mPacketBuf[0] = (CHANNEL_CNT << 4) | 0x0a;
    mPacketBuf[1] = mBindCtr & 0xff;
    mPacketBuf[2] = (mBindCtr >> 8);
    mPacketBuf[3] = *mCurRFChPtr;
    mPacketBuf[4] = *(mCurRFChPtr + 1);
    mPacketBuf[5] = *(mCurRFChPtr + 2);
    mPacketBuf[6] = mMfgIDBuf[0];
    mPacketBuf[7] = mMfgIDBuf[1];
    mPacketBuf[8] = mMfgIDBuf[2];
    mPacketBuf[9] = mMfgIDBuf[3];
    addPacketSuffix();
    //The fixed-id portion is scrambled in the bind mPacketBuf
    //I assume it is ignored
    mPacketBuf[13] ^= mMfgIDBuf[0];
    mPacketBuf[14] ^= mMfgIDBuf[1];
    mPacketBuf[15] ^= mMfgIDBuf[2];
}

void RFProtocolDevo::buildDataPacket(void)
{
    s16 value;
    u8 i;
    u8 sign = 0x0b;

    mPacketBuf[0] = (CHANNEL_CNT << 4) | (0x0b + mConChanIdx);
    for (i = 0; i < 4; i++) {
        //value = (s32)getControlByOrder(mConChanIdx * 4 + i) * 0x640 / CHAN_MAX_VALUE;
        value = getControlByOrder(mConChanIdx * 4 + i);
        value = map(value, CHAN_MIN_VALUE, CHAN_MAX_VALUE, -2100, 2100);
        if (value < 0) {
            value = -value;
            sign |= 1 << (7 - i);
        }
        mPacketBuf[2 * i + 1] = value & 0xff;
        mPacketBuf[2 * i + 2] = (value >> 8) & 0xff;
    }
    mPacketBuf[9] = sign;

    mConChanIdx++;
    if (mConChanIdx * 4 >= CHANNEL_CNT)
        mConChanIdx = 0;
    addPacketSuffix();
}

s32 RFProtocolDevo::convFloatStr2Int(u8 *ptr)
{
    s32 value = 0;
    int seen_decimal = 0;
    for(int i = 0; i < 7; i++) {
        if(ptr[i] == '.') {
            value *= 1000;
            seen_decimal = 100;
            continue;
        }
        if(ptr[i] == 0)
            break;
        if(seen_decimal) {
            value += (ptr[i] - '0') * seen_decimal;
            seen_decimal /= 10;
            if(! seen_decimal)
                break;
        } else {
            value = value * 10 + (ptr[i] - '0');
        }
    }
    return value;
}

void RFProtocolDevo::parseTelemetryPacket(u8 *packet)
{
    if((packet[0] & 0xF0) != TELEMETRY_ENABLE)
        return;

    scramblePacket(); //This will unscramble the packet

    if (packet[13] != (mFixedID  & 0xff) ||
        packet[14] != ((mFixedID >> 8) & 0xff) ||
        packet[15] != ((mFixedID >> 16) & 0xff)) {
        return;
    }

    switch (packet[0]) {
        case 0x30:
            getTM().setVolt(0, packet[1], 10);     //In 1/10 of Volts
            getTM().setVolt(1, packet[3], 10);
            getTM().setVolt(2, packet[5], 10);
            getTM().setRPM(0, packet[7] * 120);    //In RPM
            getTM().setRPM(1, packet[9] * 120);
            break;

        case 0x31:
            getTM().setTemp(0, packet[1] == 0xff ? 0 : packet[1] - 20); //In degrees-C
            getTM().setTemp(0, packet[2] == 0xff ? 0 : packet[2] - 20);
            getTM().setTemp(0, packet[3] == 0xff ? 0 : packet[3] - 20);
            getTM().setTemp(0, packet[4] == 0xff ? 0 : packet[4] - 20);
            break;

        /* GPS Data
           32: 30333032302e3832373045fb  = 030°20.8270E
           33: 353935342e373737364e0700  = 59°54.776N
           34: 31322e380000004d4d4e45fb  = 12.8 MMNE (altitude maybe)?
           35: 000000000000302e30300000  = 0.00 (probably speed)
           36: 313832353532313531303132  = 2012-10-15 18:25:52 (UTC)
        */
        case 0x32:
            getTM().setLon(((packet[1]-'0') * 100 + (packet[2]-'0') * 10 + (packet[3]-'0')) * 3600000
                            + ((packet[4]-'0') * 10 + (packet[5]-'0')) * 60000
                            + ((packet[7]-'0') * 1000 + (packet[8]-'0') * 100
                            + (packet[9]-'0') * 10 + (packet[10]-'0')) * 6);
            break;

        case 0x33:
            getTM().setLat(((packet[1]-'0') * 10 + (packet[2]-'0')) * 3600000
                            + ((packet[3]-'0') * 10 + (packet[4]-'0')) * 60000
                            + ((packet[6]-'0') * 1000 + (packet[7]-'0') * 100
                            + (packet[8]-'0') * 10 + (packet[9]-'0')) * 6);
            if (packet[10] == 'S')
                getTM().setLat(getTM().getLat() * -1);
            break;

        case 0x34:
            getTM().setAlt(convFloatStr2Int(packet + 1));
            break;

        case 0x35:
            getTM().setVel(convFloatStr2Int(packet + 7));
            break;

        case 0x36:
            u8 hour  = (packet[1]-'0') * 10 + (packet[2]-'0');
            u8 min   = (packet[3]-'0') * 10 + (packet[4]-'0');
            u8 sec   = (packet[5]-'0') * 10 + (packet[6]-'0');
            u8 day   = (packet[7]-'0') * 10 + (packet[8]-'0');
            u8 month = (packet[9]-'0') * 10 + (packet[10]-'0');
            u8 year  = (packet[11]-'0') * 10 + (packet[12]-'0'); // + 2000
            getTM().setTime(((year & 0x3F) << 26)
                               | ((month & 0x0F) << 22)
                               | ((day & 0x1F) << 17)
                               | ((hour & 0x1F) << 12)
                               | ((min & 0x3F) << 6)
                               | ((sec & 0x3F) << 0));
            break;
    }
}

void RFProtocolDevo::setBoundSOPCodes(void)
{
    /* crc == 0 isn't allowed, so use 1 if the math results in 0 */

    u8 crc    = (mMfgIDBuf[0] + (mMfgIDBuf[1] >> 6) + mMfgIDBuf[2]);
    u8 sopidx = (0xff & ((mMfgIDBuf[0] << 2) + mMfgIDBuf[1] + mMfgIDBuf[2])) % 10;

    if(!crc)
        crc = 1;
    mDev.setRFMode(RF_TX);
    mDev.setCRCSeed((crc << 8) + crc);
    mDev.setSOPCode_P(SOPCODES[sopidx], 8);
    mDev.setRFPower(getRFPower());
}

static const PROGMEM u8 TBL_INIT_REGS[] = {
    CYRF_1D_MODE_OVERRIDE,  0x38,
    CYRF_03_TX_CFG,         0x08,
    CYRF_06_RX_CFG,         0x4A,
    CYRF_0B_PWR_CTRL,       0x00,
    CYRF_0D_IO_CFG,         0x04,   // PACTL as GPIO
    CYRF_0E_GPIO_CTRL,      0x20,
    CYRF_10_FRAMING_CFG,    0xA4,
    CYRF_11_DATA32_THOLD,   0x05,
    CYRF_12_DATA64_THOLD,   0x0E,
    CYRF_1B_TX_OFFSET_LSB,  0x55,
    CYRF_1C_TX_OFFSET_MSB,  0x05,
    CYRF_32_AUTO_CAL_TIME,  0x3C,
    CYRF_35_AUTOCAL_OFFSET, 0x14,
    CYRF_39_ANALOG_CTRL,    0x01,
    CYRF_1E_RX_OVERRIDE,    0x10,
    CYRF_1F_TX_OVERRIDE,    0x00,
    CYRF_01_TX_LENGTH,      0x10,
    CYRF_0C_XTAL_CTRL,      0xC0,   // XOUT as GPIO
    CYRF_0F_XACT_CFG,       0x10,   //
    CYRF_27_CLK_OVERRIDE,   0x02,
    CYRF_28_CLK_EN,         0x02,
    CYRF_0F_XACT_CFG,       0x28,   // force tx end
};

void RFProtocolDevo::init1(void)
{
    /* Initialise CYRF chip */
//    mDev.initialize();
//    mDev.reset();

    u8 reg, val;
    for (u8 i = 0; i < sizeof(TBL_INIT_REGS) / 2; i++) {
        reg = pgm_read_byte(TBL_INIT_REGS + i * 2);
        val = pgm_read_byte(TBL_INIT_REGS + i * 2 + 1);
        if (reg == CYRF_03_TX_CFG) {
            val |= getRFPower();
            mDev.writeReg(reg, val);
        } else {
            mDev.writeReg(reg, val);
        }
    }
}

void RFProtocolDevo::setRadioChannels(void)
{
    mDev.findBestChannels(mRFChanBufs, 3, 4, 4, 80);

    LOG(F("Radio Channels:"));
    for (u8 i = 0; i < 3; i++) {
        LOG(F(" %02d"), mRFChanBufs[i]);
    }
    LOG(F("\n"));

    //Makes code a little easier to duplicate these here
    mRFChanBufs[3] = mRFChanBufs[0];
    mRFChanBufs[4] = mRFChanBufs[1];
}

void RFProtocolDevo::buildPacket(void)
{
    switch(mState) {
        case DEVO_BIND:
            if (mBindCtr > 0)
                mBindCtr--;
            buildBindPacket();
            mState = DEVO_BIND_SENDCH;
            break;

        case DEVO_BIND_SENDCH:
            if (mBindCtr > 0)
                mBindCtr--;
            buildDataPacket();
            scramblePacket();
            if (mBindCtr == 0) {
                mState = DEVO_BOUND;
//                PROTOCOL_SetBindState(0);
            } else {
                mState = DEVO_BIND;
            }
            break;

        case DEVO_BOUND:
        case DEVO_BOUND_1:
        case DEVO_BOUND_2:
        case DEVO_BOUND_3:
        case DEVO_BOUND_4:
        case DEVO_BOUND_5:
        case DEVO_BOUND_6:
        case DEVO_BOUND_7:
        case DEVO_BOUND_8:
        case DEVO_BOUND_9:
            buildDataPacket();
            scramblePacket();
            mState++;
            if (mBindCtr > 0) {
                mBindCtr--;
                if (mBindCtr == 0) {
//                    PROTOCOL_SetBindState(0);
                }
            }
            break;

        case DEVO_BOUND_10:
            buildBeaconPacket(CHANNEL_CNT > 8 ? failsafe_pkt : 0);
            failsafe_pkt = !failsafe_pkt;
            scramblePacket();
            mState = DEVO_BOUND_1;
            break;
    }

    mPacketCtr++;
    if(mPacketCtr == PKTS_PER_CHANNEL)
        mPacketCtr = 0;
}


#define NUM_WAIT_LOOPS (100 / 5) //each loop is ~5us.  Do not wait more than 100us

u16 RFProtocolDevo::callStateTelemetry(void)
{
    u8  irq;
    int i;
    u16 delay;

    switch (mTxState) {
        case 0:
            buildPacket();
            mDev.writePayload(mPacketBuf, MAX_PACKET_SIZE);
            mTxState = 1;
            return 900;

        case 1:
            i = 0;

            do {
                irq = mDev.readReg(CYRF_04_TX_IRQ_STATUS);
                if(++i > NUM_WAIT_LOOPS) {
                    LOG(F("MAX WAIT IRQ : %x\n"), irq);
                    break;
                }
            } while (!(irq & 0x02));

            if ((irq & 0x22) == 0x20 || (mDev.readReg(CYRF_02_TX_CTRL) & 0x80) ) {
                LOG(F("Abnormal!! RESET!!!\n"));
                mDev.reset();
                init1();
                setBoundSOPCodes();
                mDev.setRFChannel(*mCurRFChPtr);
            } else {
                if (mState == DEVO_BOUND) {
                    /* exit binding mState */
                    mState = DEVO_BOUND_3;
                    setBoundSOPCodes();
                }
                if (mPacketCtr != 0 && mBindCtr == 0) {
                    mDev.setRFMode(RF_RX);                  // Receive mode
                    mDev.writeReg(CYRF_05_RX_CTRL, 0x80);   // Prepare to receive
                    mTxState = 2;
                    return 1300;
                }
            }
            if (mPacketCtr == 0) {
                //Keep tx power updated
                mDev.setRFPower(getRFPower());
                mCurRFChPtr = (mCurRFChPtr == &mRFChanBufs[2]) ? mRFChanBufs : (mCurRFChPtr + 1);
                mDev.setRFChannel(*mCurRFChPtr);
            }
            delay = 1500;
            break;

        case 2:
            irq = mDev.readReg(CYRF_07_RX_IRQ_STATUS);
            if ((irq & 0x03) == 0x02) {
                irq |= mDev.readReg(CYRF_07_RX_IRQ_STATUS);
            }
            if ((irq & 0x07) == 0x02) {
                mDev.writeReg(CYRF_07_RX_IRQ_STATUS, 0x80); // Prepare to receive
                mDev.readPayload(mPacketBuf, mDev.readReg(CYRF_09_RX_COUNT));
                parseTelemetryPacket(mPacketBuf);
            }
            mDev.setRFMode(RF_TX);
            delay = 200;
            break;
    }
    mTxState = 0;

    return delay;
}

u16 RFProtocolDevo::callStateNormal(void)
{
    if (mTxState == 0) {
        buildPacket();
        mDev.writePayload(mPacketBuf, MAX_PACKET_SIZE);
    } else {
        u8  irq;
        int i = 0;

        do {
            irq = mDev.readReg(CYRF_04_TX_IRQ_STATUS);
            if(++i > NUM_WAIT_LOOPS) {
                LOG(F("MAX WAIT IRQ : %x\n"), irq);
                return 1200;
            }
        } while (!(irq & 0x02));

        if (mState == DEVO_BOUND) {
            /* exit binding mState */
            mState = DEVO_BOUND_3;
            setBoundSOPCodes();
        }
        if (mPacketCtr == 0) {
            //Keep tx power updated
            mDev.setRFPower(getRFPower());
            mCurRFChPtr = (mCurRFChPtr == &mRFChanBufs[2]) ? mRFChanBufs : (mCurRFChPtr + 1);
            mDev.setRFChannel(*mCurRFChPtr);
        }
    }
    mTxState = !mTxState;

    return 1200;
}

u16 RFProtocolDevo::callState(u32 now, u32 expected)
{
    return callStateTelemetry();
}

int RFProtocolDevo::init(u8 bind)
{
    RFProtocol::registerCallback(this);

    init1();

    mDev.readMfgID(mMfgIDBuf);
    mDev.setRFMode(RF_TX);
    mDev.setCRCSeed(0x0000);
    mDev.setSOPCode_P(SOPCODES[0], 8);
    setRadioChannels();

    mBoolFixedID = 0;
    failsafe_pkt = 0;
    mCurRFChPtr = mRFChanBufs;
    mDev.setRFChannel(*mCurRFChPtr);

    //num_channels = ((Model.num_channels + 3) >> 2) * 4;
    mConChanIdx = 0;
    mPacketCtr  = 0;
    mTxState    = 0;

    if (bind) {  // ! Model.mFixedID
        mFixedID = ((u32)(mRFChanBufs[0] ^ mMfgIDBuf[0] ^ mMfgIDBuf[3]) << 16)
                 | ((u32)(mRFChanBufs[1] ^ mMfgIDBuf[1] ^ mMfgIDBuf[4]) << 8)
                 | ((u32)(mRFChanBufs[2] ^ mMfgIDBuf[2] ^ mMfgIDBuf[5]) << 0);

        mFixedID = mFixedID % 1000000;
        mBindCtr = MAX_BIND_COUNT;
        mState   = DEVO_BIND;
    } else {
        //mFixedID = Model.mFixedID;
        mBoolFixedID = 1;
        mState = DEVO_BOUND_1;
        mBindCtr = 0;
        setBoundSOPCodes();
    }

    startState(INITIAL_WAIT_uS);

    return 0;
}

int RFProtocolDevo::close(void)
{
    mDev.initialize();
    return (mDev.reset() ? 1L : -1L);
}

int RFProtocolDevo::reset(void)
{
    return close();
}

int RFProtocolDevo::getInfo(s8 id, u8 *data)
{
    u8 size;

    size = RFProtocol::getInfo(id, data);
    if (size == 0) {
        switch (id) {
            case INFO_STATE:
                *data = mState;
                size = 1;
                break;

            case INFO_CHANNEL:
                *data = mRFChanBufs[0];
                size = 1;
                break;

            case INFO_PACKET_CTR:
                size = sizeof(mPacketCtr);
                *((u32*)data) = mPacketCtr;
                break;
        }
    }
    return size;
}

