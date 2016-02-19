/*
  ax12.cpp - ArbotiX library for AX/RX control.
  Copyright (c) 2008-2011 Michael E. Ferguson.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "ax12Uno.h"
#define ARBOTIX

/******************************************************************************
 * Hardware Serial Level, this uses the same stuff as Serial1, therefore 
 *  you should not use the Arduino Serial1 library.
 */

unsigned char ax_rx_buffer[AX12_BUFFER_SIZE];
unsigned char ax_tx_buffer[AX12_BUFFER_SIZE];
unsigned char ax_rx_int_buffer[AX12_BUFFER_SIZE];

// making these volatile keeps the compiler from optimizing loops of available()
volatile int ax_rx_Pointer;
volatile int ax_tx_Pointer;
volatile int ax_rx_int_Pointer;
#if defined(AX_RX_SWITCHED)
unsigned char dynamixel_bus_config[AX12_MAX_SERVOS];
#endif

/** helper functions to switch direction of comms */
void setTX(int id){
    bitClear(UCSR0B, RXEN0); 
  #if defined(AX_RX_SWITCHED)
    if(dynamixel_bus_config[id-1] > 0)
        SET_RX_WR;
    else
        SET_AX_WR;   
  #else
    // emulate half-duplex on ArbotiX, ArbotiX w/ RX Bridge
    #ifdef ARBOTIX_WITH_RX
      PORTD |= 0x10;
    #endif   
    bitSet(UCSR0B, TXEN0);
    bitClear(UCSR0B, RXCIE0);
  #endif
    ax_tx_Pointer = 0;
}
void setRX(int id){ 

   // delay(2);
   // UCSR0B = 1<<RXCIE0 | 0 <<TXCIE0 | 0<<UDRIE0 | 1 << RXEN0 | 0<TXEN0 | 0<<UCSZ02 | 0<<TXB80;


     bitClear(UCSR0B, TXEN0);
     bitSet(UCSR0B, RXCIE0);
     bitSet(UCSR0B, RXEN0);
    ax_rx_int_Pointer = 0;
    ax_rx_Pointer = 0;
}
// for sync write
void setTXall(){
    bitClear(UCSR0B, RXEN0);    
  #if defined(AX_RX_SWITCHED)
    SET_RX_WR;
    SET_AX_WR;   
  #else
    #ifdef ARBOTIX_WITH_RX
      PORTD |= 0x10;
    #endif
    bitSet(UCSR0B, TXEN0);
    bitClear(UCSR0B, RXCIE0);
  #endif
    ax_tx_Pointer = 0;
}

/** Sends a character out the serial port. */
void ax12write(unsigned char data){
    while (bit_is_clear(UCSR0A, UDRE0));
    UDR0 = data;
}
/** Sends a character out the serial port, and puts it in the tx_buffer */
void ax12writeB(unsigned char data){
    ax_tx_buffer[(ax_tx_Pointer++)] = data; 
    while (bit_is_clear(UCSR0A, UDRE0));
    UDR0 = data;
}
/** We have a one-way recieve buffer, which is reset after each packet is receieved.
    A wrap-around buffer does not appear to be fast enough to catch all bytes at 1Mbps. */
ISR(USART_RX_vect){
    ax_rx_int_buffer[(ax_rx_int_Pointer++)] = UDR0;
}

/** read back the error code for our latest packet read */
int ax12Error;
int ax12GetLastError(){ return ax12Error; }
/** > 0 = success */
int ax12ReadPacket(int length){
    unsigned long ulCounter;
    unsigned char offset, blength, checksum, timeout;
    unsigned char volatile bcount; 

    offset = 0;
    timeout = 0;
    bcount = 0;
    while(bcount < length){
        ulCounter = 0;
        while((bcount + offset) == ax_rx_int_Pointer){
            if(ulCounter++ > 1000L){ // was 3000
                timeout = 1;
                break;
            }
        }
        if(timeout) break;
        ax_rx_buffer[bcount] = ax_rx_int_buffer[bcount + offset];
        if((bcount == 0) && (ax_rx_buffer[0] != 0xff))
            offset++;
        else if((bcount == 2) && (ax_rx_buffer[2] == 0xff))
            offset++;
        else
            bcount++;
    }

    blength = bcount;
    checksum = 0;
    for(offset=2;offset<bcount;offset++)
        checksum += ax_rx_buffer[offset];
    if((checksum%256) != 255){
        return 0;
    }else{
        return 1;
    }
}

/** initializes serial1 transmit at baud, 8-N-1 */
void ax12Init(long baud){
    UBRR0H = (F_CPU / (8 * baud) - 1 ) >> 8;
    UBRR0L = (F_CPU / (8 * baud) - 1 );
    bitSet(UCSR0A, U2X0);
    ax_rx_int_Pointer = 0;
    ax_rx_Pointer = 0;
    ax_tx_Pointer = 0;
#if defined(AX_RX_SWITCHED)
    INIT_AX_RX;
    bitSet(UCSR0B, TXEN0);
    bitSet(UCSR0B, RXEN0);
    bitSet(UCSR0B, RXCIE0);
#else
  #ifdef ARBOTIX_WITH_RX
    DDRD |= 0x10;   // Servo B = output
    PORTD &= 0xEF;  // Servo B low
  #endif
    // set RX as pull up to hold bus to a known level
    PORTD |= (1<<2);
    // enable rx
    setRX(0);
#endif
}

/******************************************************************************
 * Packet Level
 */

/** Read register value(s) */
int ax12GetRegister(int id, int regstart, int length){  
    setTX(id);
    // 0xFF 0xFF ID LENGTH INSTRUCTION PARAM... CHECKSUM    
    int checksum = ~((id + 6 + regstart + length)%256);
    ax12writeB(0xFF);
    ax12writeB(0xFF);
    ax12writeB(id);
    ax12writeB(4);    // length
    ax12writeB(AX_READ_DATA);
    ax12writeB(regstart);
    ax12writeB(length);
    ax12writeB(checksum);  
    setRX(id);    

    if(ax12ReadPacket(length + 6) > 0){
        ax12Error = ax_rx_buffer[4];
        if(length == 1)
            return ax_rx_buffer[5];
        else
            return ax_rx_buffer[5] + (ax_rx_buffer[6]<<8);
    }else{
        return -1;
    }
}

/* Set the value of a single-byte register. */
void ax12SetRegister(int id, int regstart, int data){
    setTX(id);    
    int checksum = ~((id + 4 + AX_WRITE_DATA + regstart + (data&0xff)) % 256);
    ax12writeB(0xFF);
    ax12writeB(0xFF);
    ax12writeB(id);
    ax12writeB(4);    // length
    ax12writeB(AX_WRITE_DATA);
    ax12writeB(regstart);
    ax12writeB(data&0xff);
    // checksum = 
    ax12writeB(checksum);
    setRX(id);
    //ax12ReadPacket();
}
/* Set the value of a double-byte register. */
void ax12SetRegister2(int id, int regstart, int data){
    setTX(id);    
    int checksum = ~((id + 5 + AX_WRITE_DATA + regstart + (data&0xFF) + ((data&0xFF00)>>8)) % 256);
    ax12writeB(0xFF);
    ax12writeB(0xFF);
    ax12writeB(id);
    ax12writeB(5);    // length
    ax12writeB(AX_WRITE_DATA);
    ax12writeB(regstart);
    ax12writeB(data&0xff);
    ax12writeB((data&0xff00)>>8);
    // checksum = 
    ax12writeB(checksum);
    setRX(id);
    //ax12ReadPacket();
}

// general write?
// general sync write?
