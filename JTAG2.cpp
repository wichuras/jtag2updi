/*
   STK500.cpp

   Created: 08-12-2017 19:47:27
    Author: JMR_2
*/

#include "JTAG2.h"
#include "JICE_io.h"
#include "NVM.h"
#include "NVM_v2.h"
#include "crc16.h"
#include "UPDI_hi_lvl.h"
#include "dbg.h"

// *** Writeable Parameter Values ***
uint8_t JTAG2::PARAM_EMU_MODE_VAL;
JTAG2::baud_rate JTAG2::PARAM_BAUD_RATE_VAL;
uint8_t JTAG2::ConnectedTo;
// *** STK500 packet ***
JTAG2::packet_t JTAG2::packet;

// Local objects
namespace {
// *** Local variables ***
uint16_t flash_pagesize;
uint8_t eeprom_pagesize;
uint8_t nvm_version = 1;

// *** Local functions declaration ***
void NVM_fuse_write (uint16_t address, uint8_t data);
void NVM_v2_write (uint32_t address, uint16_t length, uint8_t flash_cmd);
void NVM_buffered_write(uint16_t address, uint16_t length, uint8_t buff_size, uint8_t write_type);

// *** Signature response message ***
FLASH<uint8_t> sgn_resp[29] { JTAG2::RSP_SIGN_ON, 1,
                              1, JTAG2::PARAM_FW_VER_M_MIN_VAL, JTAG2::PARAM_FW_VER_M_MAJ_VAL, JTAG2::PARAM_HW_VER_M_VAL,
                              1, JTAG2::PARAM_FW_VER_S_MIN_VAL, JTAG2::PARAM_FW_VER_S_MAJ_VAL, JTAG2::PARAM_HW_VER_S_VAL,
                              0, 0, 0, 0, 0, 0,
                              'J', 'T', 'A', 'G', 'I', 'C', 'E', ' ', 'm', 'k', 'I', 'I', 0
                            };
}

// *** Packet functions ***
bool JTAG2::receive() {
  while (JICE_io::get() != MESSAGE_START) {
    if ((SYS::checkTimeouts() & WAIT_FOR_HOST)) return false;
  }
  uint16_t crc = CRC::next(MESSAGE_START);
  for (uint16_t i = 0; i < 6; i++) {
    crc = CRC::next(packet.raw[i] = JICE_io::get(), crc);
  }
  if (packet.size_word[0] > sizeof(packet.body)) return false;
  if (JICE_io::get() != TOKEN) return false;
  crc = CRC::next(TOKEN, crc);
  for (uint16_t i = 0; i < packet.size_word[0]; i++) {
    crc = CRC::next(packet.body[i] = JICE_io::get(), crc);
  }
  if ((uint16_t)(JICE_io::get() | (JICE_io::get() << 8)) != crc) return false;
  return true;
}

void JTAG2::answer() {
  uint16_t crc = CRC::next(JICE_io::put(MESSAGE_START));
  for (uint16_t i = 0; i < 6; i++) {
    crc = CRC::next(JICE_io::put(packet.raw[i]), crc);
  }
  crc = CRC::next(JICE_io::put(TOKEN), crc);
  for (uint16_t i = 0; i < packet.size_word[0]; i++) {
    crc = CRC::next(JICE_io::put(packet.body[i]), crc);
  }
  JICE_io::put(crc);
  JICE_io::put(crc >> 8);
}

void JTAG2::delay_exec() {
  // wait for transmission complete
  JICE_io::flush();
  // set baud rate
  JICE_io::set_baud(PARAM_BAUD_RATE_VAL);
}

// *** Set status function ***
void JTAG2::set_status(uint8_t status_code) {
  packet.size_word[0] = 1;
  packet.body[0] = status_code;
}

// *** General command functions ***

void JTAG2::sign_on() {
  // Initialize JTAGICE2 variables
  JTAG2::PARAM_EMU_MODE_VAL = 0x02;
  JTAG2::PARAM_BAUD_RATE_VAL = JTAG2::baud_19200;
  // Send sign on message
  packet.size_word[0] = sizeof(sgn_resp);
  for (uint8_t i = 0; i < sizeof(sgn_resp); i++) {
    packet.body[i] = sgn_resp[i];
  }
  JTAG2::ConnectedTo |= 0x02; //now connected to host
}

void JTAG2::get_parameter() {
  uint8_t & status = packet.body[0];
  uint8_t & parameter = packet.body[1];
  switch (parameter) {
    case PARAM_HW_VER:
      packet.size_word[0] = 3;
      packet.body[1] = PARAM_HW_VER_M_VAL;
      packet.body[2] = PARAM_HW_VER_S_VAL;
      break;
    case PARAM_FW_VER:
      packet.size_word[0] = 5;
      packet.body[1] = PARAM_FW_VER_M_MIN_VAL;
      packet.body[2] = PARAM_FW_VER_M_MAJ_VAL;
      packet.body[3] = PARAM_FW_VER_S_MIN_VAL;
      packet.body[4] = PARAM_FW_VER_S_MAJ_VAL;
      break;
    case PARAM_EMU_MODE:
      packet.size_word[0] = 2;
      packet.body[1] = PARAM_EMU_MODE_VAL;
      break;
    case PARAM_BAUD_RATE:
      packet.size_word[0] = 2;
      packet.body[1] = PARAM_BAUD_RATE_VAL;
      break;
    case PARAM_VTARGET:
      packet.size_word[0] = 3;
      packet.body[1] = PARAM_VTARGET_VAL & 0xFF;
      packet.body[2] = PARAM_VTARGET_VAL >> 8;
      break;
    default:
      set_status(RSP_ILLEGAL_PARAMETER);
      return;
  }
  status = RSP_PARAMETER;
  return;
}

void JTAG2::set_parameter() {
  uint8_t & parameter = packet.body[1];
  switch (parameter) {
    case PARAM_EMU_MODE:
      PARAM_EMU_MODE_VAL = packet.body[2];
      break;
    case PARAM_BAUD_RATE:
      PARAM_BAUD_RATE_VAL = (baud_rate)packet.body[2];
      break;
    default:
      set_status(RSP_ILLEGAL_PARAMETER);
      return;
  }
  set_status(RSP_OK);
}

void JTAG2::set_device_descriptor() {
  flash_pagesize = packet.body[244];
  eeprom_pagesize = packet.body[246];
  // Now they've told us what we're talking to, and we will try to connect to it
  /* Initialize or enable UPDI */
  UPDI_io::put(UPDI_io::double_break);
  UPDI::stcs(UPDI::reg::Control_A, 0x06);
  uint8_t sib[16];
  UPDI::read_sib(sib);
  #if defined(DEBUG_ON)
    DBG::debug(sib, 16, 1);
  #endif
  JTAG2::ConnectedTo |= 0x01; // connected to target
  if (sib[10] == '2') {
    nvm_version = 2;
  } else {
    nvm_version = 1;
  }
  set_status(RSP_OK);
  #ifdef INCLUDE_EXTRA_INFO_JTAG
    packet.size_word[0] = 24;
    packet.body[1] = 'S';
    packet.body[2] = 'I';
    packet.body[3] = 'B';
    for (uint8_t i = 0; i < 16; i++) {
      packet.body[i + 4] = sib[i];
    }
    packet.body[20]= 'N';
    packet.body[21]= 'V';
    packet.body[22]= 'M';
    packet.body[23]= (nvm_version==2?'2':'1');    
  #endif
}

// *** Target mode set functions ***
// *** Sets MCU in program mode, if possibe ***
void JTAG2::enter_progmode() {
  const uint8_t initial_status = UPDI::CPU_mode<0xEF>();
  // reset the MCU now, to prevent the WDT (if active) to reset it at an unpredictable moment
  // but just enter reset state - don't let it out of reset state, because then it will run 
  // the first few instructions of the previous sketch...
  if (initial_status!=0x08){
    UPDI::CPU_reset_on();
    // Now we have time to enter program mode (this mode also disables the WDT)
    // Previously a reset was done here WHICH WOULD GUARANTEE THAT IT WAS 
    // IN NORMAL MODE!! But then we checked anyway, and went into a meaningless
    // switch case statement... because we already KNOW that it would be in 
    // normal mode, because we just reset it into normal mode.... 
    // So, since we know we'd be in normal mode, and we want to be in programming mode
    // Write NVM unlock key (allows read access to all addressing space)
    UPDI::write_key(UPDI::NVM_Prog);
    // Request reset
    const bool reset_ok=UPDI::CPU_reset_off();
    if (reset_ok){
      const uint8_t system_status = UPDI::CPU_mode<0xEF>();
      if (system_status==0x08) { //make sure we're really in programming mode
        if (nvm_version == 1) {
          // For NVM version 1 parts, there's a page buffer.
          // It might have data in it if something else was writing to the flash when
          // we so rudely interrupted, so better clear the page buffer, just in case.
          UPDI::sts_b(NVM::NVM_base | NVM::CTRLA, NVM::PBC);
        } else {
          // NVM v2 devices can have error codes in NVMCTRL.STATUS
          // They also require that NVMCTRLA be set to NOOP/NOCMD before use
          // So we should do this here!
          uint8_t NVM_Status = UPDI::lds_b_l(NVM_v2::NVM_base + NVM_v2::STATUS);
          uint8_t NVM_Cmnd = UPDI::lds_b_l(NVM_v2::NVM_base + NVM_v2::CTRLA);
          if (NVM_Status || NVM_Cmnd ) {
            #if defined(DEBUG_ON)
            DBG::debug('N', NVM_Status,NVM_Cmnd);
            #endif
            UPDI::sts_b_l(NVM_v2::NVM_base + NVM_v2::STATUS, 0);
            UPDI::sts_b_l(NVM_v2::NVM_base + NVM_v2::CTRLA, 0);
          }
        }
          // Turn on LED to indicate program mode
          SYS::setLED();
          #if defined(DEBUG_ON)
          // report the chip revision
          DBG::debug('R',UPDI::lds_b_l(0x0F01));
          #endif
          set_status(RSP_OK);
          #if defined(INCLUDE_EXTRA_INFO_JTAG)
            // get the REV ID - I belive (will be confirming with Microchip support) that this is the silicon revision ID
            // this is particularly important with some of these chips - the tinyAVR and megaAVR parts do have differences
            // between silicon revisions, and AVR-DA-series initial rev is a basket case and will almost certainly be respun
            // before volume availability (take a look at the errata if you haven't already. There are fewer entries than on
            // tinyAVR 1-series, but they're BIG... like "digital input disabled after using analog input" "you know that 
            // flashmap we said could be used with ld and st? We lied, you can only use it with ld, and even then, there are
            // cases where it won't work" "we didn't do even basic testing of 64-pin version, so stuff on those those pins is
            // hosed" (well, they didn't say they didn't test it, but it's bloody obvious that's how it happened))
            const uint8_t sernumlen=nvm_version==2?15:9; 
            packet.size_word[0]=9+sernumlen;
            packet.body[1]='R';
            packet.body[2]='E';
            packet.body[3]='V';
            packet.body[4]=UPDI::lds_b_l(0x0F01);
            //hell, might as well get the chip serial number too!
            packet.body[5]= 'S';
            packet.body[6]= 'E';
            packet.body[7]= 'R';
            if(nvm_version==2){
              UPDI::stptr_l(0x1110);
            } else {
              UPDI::stptr_w(0x1103);
            }
            UPDI::rep(sernumlen);
            packet.body[8]=UPDI::ldinc_b();
            for(uint8_t i=9;i<(9+sernumlen);i++){
              packet.body[i]=UPDI_io::get();
            }
            #ifdef DEBUG_ON
              DBG::debug("Serial Number: ");
              uint8_t *ptr=(uint8_t*)(&packet.body[8]);
              DBG::debug(ptr,sernumlen+1,0,1);
            #endif
          #elif defined(DEBUG_ON) //if we're not adding extended info in JTAG, but debug is on, I guess we shoud still report this...
            uint8_t sernumber[10];
            if(nvm_version==2){
              UPDI::stptr_l(0x1100);
            } else {
              UPDI::stptr(0x1100);
            }
            UPDI::rep(9);
            sernumber[0]=UPDI::ldinc_b();
            for(uint8_t i=1;i<10;i++){
              sernumber[i]=UPDI_io::get();
            }
          #endif
        } else {
          // If we're somehow NOT in programming mode now, that's no good - inform host of this unfortunate state of affairs
          packet.body[0] = RSP_ILLEGAL_MCU_STATE;
          packet.body[1] = system_status; // 0x01;
        }
      } else {
        set_status(RSP_NO_TARGET_POWER); //if it didn't come back from reset, inform the host. 
      }
    } else {
      set_status(RSP_OK);
    }
  }

  // *** Sets MCU in normal runnning mode, if possibe ***
  void JTAG2::leave_progmode() {
    const uint8_t system_status = UPDI::CPU_mode<0xEF>();
    bool reset_ok=0;
    switch (system_status) {
      // in program mode
      case 0x08:
      //clear NVMCTRL.CTRLA
      if (nvm_version==2) {
        UPDI::sts_b_l(NVM_v2::NVM_base + NVM_v2::CTRLA, 0);
      } else {
        UPDI::sts_b(NVM::NVM_base | NVM::CTRLA, 0);
      }
      // Request reset
        reset_ok=UPDI::CPU_reset();
      // Wait for normal mode
      // while (UPDI::CPU_mode<0xEF>() != 0x82);
      // already in normal mode
      /* fall-thru */
      case 0x82:
        // Turn off LED to indicate normal mode
        SYS::clearLED();
        if (reset_ok) {
          set_status(RSP_OK);
        } else {
          set_status(RSP_NO_TARGET_POWER); //this is a strange situation indeed, but tell the host anyway!
        }
        break;
      // in other modes fail and inform host of wrong mode
      default:
        packet.size_word[0] = 2;
        packet.body[0] = RSP_ILLEGAL_MCU_STATE;
        packet.body[1] = system_status; // 0x01;
    }
  }

  // The final command to make the chip go back into normal mode, shudown UPDI, and start running normally
  void JTAG2::go() {
    UPDI::stcs(UPDI::reg::Control_B, 0x04); //set UPDISIS to tell it that we're done and it can stop running the UPDI peripheral.
    JTAG2::ConnectedTo &= ~(0x01); //record that we're no longer talking to the target
    set_status(RSP_OK);
  }


  // *** Read/Write/Erase functions ***

  void JTAG2::read_mem() {
    if (UPDI::CPU_mode() != 0x08) {
      // fail if not in program mode
      packet.size_word[0] = 2;
      packet.body[0] = RSP_ILLEGAL_MCU_STATE;
      packet.body[1] = 0x01;
    }
    else {
      const uint16_t NumBytes = (packet.body[3] << 8) | packet.body[2];
      if (nvm_version == 1) {
        // in program mode
        // Get physical address for reading
        const uint16_t address = (packet.body[7] << 8) | packet.body[6];
        // Set UPDI pointer to address
        if (NumBytes>1) {
          UPDI::stptr_w(address);
          // Read block
          UPDI::rep(NumBytes - 1);
          packet.body[1] = UPDI::ldinc_b();
          for (uint16_t i = 2; i <= NumBytes; i++) {
            packet.body[i] = UPDI_io::get();
          }
        } else {
          packet.body[1]=UPDI::lds_b(address);
        }
        packet.size_word[0] = NumBytes + 1;
        packet.body[0] = RSP_MEMORY;
      } else { //nvm version 2, reading flash
        // Get physical address for reading
        uint32_t address = (((uint32_t)packet.body[8]) << 16) + (((uint16_t)packet.body[7]) << 8) + packet.body[6];
        if ((packet.body[1] == MTYPE_FLASH) || (packet.body[1] == MTYPE_BOOT_FLASH)) {
          // Set UPDI pointer to address
          UPDI::stptr_l(address);
          // Read block
          uint16_t temp;
          UPDI::rep((NumBytes >> 1) - 1);
          temp = UPDI::ldinc_w();
          packet.body[1] = temp & 0xFF;
          packet.body[2] = temp >> 8;
          for (uint16_t i = 3; i <= NumBytes; i++) {
            packet.body[i] = UPDI_io::get();
          }
          packet.size_word[0] = NumBytes + 1;
          packet.body[0] = RSP_MEMORY;
        } else { //NVM v2 non-flash
          // Set UPDI pointer to address
          if (NumBytes>1) {
            UPDI::stptr_l(address);
            // Read block
            UPDI::rep(NumBytes - 1);
            packet.body[1] = UPDI::ldinc_b();
            for (uint16_t i = 2; i <= NumBytes; i++) {
              packet.body[i] = UPDI_io::get();
            }
          } else {
            packet.body[1]=UPDI::lds_b_l(address);
          }
          packet.size_word[0] = NumBytes + 1;
          packet.body[0] = RSP_MEMORY;
        }
      }
    }
  }

  void JTAG2::write_mem() {
    if (UPDI::CPU_mode() != 0x08) {
      // fail if not in program mode
      packet.size_word[0] = 2;
      packet.body[0] = RSP_ILLEGAL_MCU_STATE;
      packet.body[1] = 0x01;
    }
    else {
      // in program mode

      const uint8_t mem_type = packet.body[1];
      const uint16_t length = packet.body[2] | (packet.body[3] << 8);             /* number of bytes to write */
      const bool is_flash = ((mem_type == MTYPE_FLASH) || (mem_type == MTYPE_BOOT_FLASH));
      if (nvm_version == 1) {
        const uint16_t address = packet.body[6] | (packet.body[7] << 8);
        const uint8_t buff_size = is_flash ? flash_pagesize : eeprom_pagesize;
        const uint8_t write_cmnd = is_flash ? NVM::WP : NVM::ERWP;
        switch (mem_type) {
          case MTYPE_FUSE_BITS:
          case MTYPE_LOCK_BITS:
            NVM_fuse_write (address, packet.body[10]);
            break;
          case MTYPE_FLASH:
          case MTYPE_BOOT_FLASH:
          case MTYPE_EEPROM_XMEGA:
          case MTYPE_USERSIG:
            NVM_buffered_write(address, length, buff_size, write_cmnd);
            break;
          default:
            set_status(RSP_ILLEGAL_MEMORY_TYPE);
            return;
        }
      } else {
        const uint32_t address = (((uint32_t)packet.body[8]) << 16) | (((uint16_t)packet.body[7]) << 8) | packet.body[6];
        uint8_t write_cmd = NVM_v2::EEERWR;
        switch (mem_type) {
          case MTYPE_FLASH:
          case MTYPE_BOOT_FLASH:
            write_cmd = NVM_v2::FLWR;
          /* fall-thru */
          case MTYPE_FUSE_BITS:
          case MTYPE_LOCK_BITS:
          case MTYPE_EEPROM_XMEGA:
          case MTYPE_USERSIG:
            NVM_v2_write(address, length, write_cmd);
            break;
          default:
            set_status(RSP_ILLEGAL_MEMORY_TYPE);
            return;
        }
      }
      set_status(RSP_OK);
    }
  }

  void JTAG2::erase() {
    const uint8_t erase_type = packet.body[1];
    bool reset_ok=0;
    switch (erase_type) {
      case 0:
        // Write Chip Erase key
        UPDI::write_key(UPDI::Chip_Erase);
        // Request reset
        reset_ok=UPDI::CPU_reset();
        if (!reset_ok){
          set_status(RSP_NO_TARGET_POWER); //if the reset failed, inform host, break out becuase the rest ain't gonna work
          break;
        }
        // Erase chip process exits program mode, reenter...
        enter_progmode();
        break;
      case 4:
      case 5:
        if (nvm_version == 1) {
          const uint16_t address = packet.body[2] | (packet.body[3] << 8);
          NVM::wait<false>();
          UPDI::sts_b(address, 0xFF);
          NVM::command<false>(NVM::ER);
          set_status(RSP_OK);
        } else {
          const uint32_t address = (((uint32_t)packet.body[4]) << 16) | (((uint16_t) packet.body[3]) << 8) | packet.body[2];
          NVM_v2::wait<false>();
          NVM_v2::command<false>(NVM_v2::FLPER);
          UPDI::sts_b_l(address, 0xFF);
          NVM_v2::command<false>(NVM_v2::NOOP);
          set_status(RSP_OK);
        }
        break;
      case 6:
      case 7:
        break;
      default:
        set_status(RSP_FAILED);
    }
  }

  // *** Local functions definition ***
  namespace {
  void NVM_fuse_write (uint16_t address, uint8_t data) {
    // Setup UPDI pointer
    UPDI::stptr_w(NVM::NVM_base + NVM::DATA_lo);
    // Send data to the NVM controller
    UPDI::stinc_b(data);
    UPDI::stinc_b(0x00);
    // Send address to the NVM controller
    UPDI::stinc_b(address & 0xFF);
    UPDI::stinc_b(address >> 8);
    // Execute fuse write
    NVM::command<false>(NVM::WFU);
  }


  void NVM_v2_write (uint32_t address, uint16_t length, uint8_t write_cmd) {
    uint16_t current_byte_index = 10;         /* Index of the first byte to send inside the JTAG2 command body */
    // Send the write command
    NVM_v2::command<false>(write_cmd);

    if (length == 1) { //if just one byte wrote it with no looping
      // write to memory
      UPDI::sts_b_l(address, JTAG2::packet.body[current_byte_index]);
    } else {
      if (length < 4 || write_cmd != NVM_v2::FLWR ) // byte write for short flash writes and all eeprom writes
      {
        uint8_t bytes_remaining = length - 1;         /* number of bytes to write */
        NVM_v2::command<false>(write_cmd);
        // Set UPDI pointer to address
        UPDI::stptr_l(address);
        UPDI::rep(bytes_remaining);
        UPDI::stinc_b(JTAG2::packet.body[current_byte_index++]);
        for (uint8_t i = bytes_remaining; i; i--) {
          UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
          UPDI_io::get();
        }
      } else { //word write
        int8_t words_remaining= (length >> 1)-1;
        UPDI::stptr_l(address);
#ifndef NO_ACK_WRITE
        uint16_t firstword=JTAG2::packet.body[current_byte_index]|(JTAG2::packet.body[current_byte_index+1] << 8);
        current_byte_index+=2;
        UPDI::rep(words_remaining);
        UPDI::stinc_w(firstword);
        for (uint8_t i = words_remaining;i;i--) {
          UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
          UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
          UPDI_io::get();
        }
        if (length & 0x01) { //in case they send us odd-legnth write command...
          UPDI::stinc_b(JTAG2::packet.body[current_byte_index++]);
        }
#else //writing without ACK
        UPDI::stcs(UPDI::reg::Control_A, 0x0E);
        UPDI::rep(words_remaining);
        UPDI::stinc_b_b_noget(JTAG2::packet.body[current_byte_index],JTAG2::packet.body[current_byte_index+1]);
        current_byte_index+=2;
        for (uint8_t i = words_remaining;i;i--) {
          UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
          UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
        }
        if (length & 0x01) { //in case they send us odd-legnth write command...
          UPDI::stinc_b_noget(JTAG2::packet.body[current_byte_index++]);
        }
        UPDI::stcs(UPDI::reg::Control_A, 0x06);
#endif
      }
    }
    //now the data has been written, so reset the command...
    uint8_t stat = UPDI::lds_b_l(NVM_v2::NVM_base + NVM_v2::STATUS);
    if (stat > 3) {
      #if defined(DEBUG_ON)
      uint8_t cmd=UPDI::lds_b_l(NVM_v2::NVM_base + NVM_v2::CTRLA);
      DBG::debug('f',stat,cmd,0);
      #endif
      UPDI::sts_b_l(NVM_v2::NVM_base + NVM_v2::STATUS, 0);
    }
    //wait until operation completed
    NVM_v2::wait<false>();

    NVM_v2::command<false>(NVM_v2::NOOP);
  }

  /*
    void NVM_v2_word_write_flash(uint32_t address, uint16_t length){
    uint16_t current_byte_index = 10;
    if (length == 2){
      NVM_v2::wait<false>();
      NVM_v2::command<false>(NVM_v2::FLWR);
      // write to flash
      UPDI::sts_w_l(address,JTAG2::packet.body[current_byte_index]|(((uint16_t)JTAG2::packet.body[current_byte_index+1])<<8));
      NVM_v2::command<false>(NVM_v2::NOOP);
    } else {
      uint8_t words_remaining = (length-2)>>1;
      startDebug();
      putDebug(0x6A);
      putDebug(address & 0xFF);
      putDebug((address >> 8) & 0xFF);
      putDebug((address >> 16) & 0xFF);
      putDebug(0xA0);
      putDebug(words_remaining);
      //wait until previous operation completed, if any
      NVM_v2::wait<false>();
      //set the write enable command
      NVM_v2::command<false>(NVM_v2::FLWR);
          // Set UPDI pointer to address

      UPDI::stptr_l(address);
      UPDI::rep(words_remaining);
      uint16_t wordtowrite=JTAG2::packet.body[current_byte_index++];
      wordtowrite+=JTAG2::packet.body[current_byte_index++]<<8;
      UPDI::stinc_w(wordtowrite);
      for (uint8_t i = words_remaining; i; i--) {
        UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
        UPDI_io::put(JTAG2::packet.body[current_byte_index++]);
        UPDI_io::get();
      }
      putDebug(current_byte_index & 0xFF);
      putDebug((current_byte_index >> 8));
      endDebug();
      NVM_v2::command<false>(NVM_v2::NOOP);
    }
    }
  */
  void NVM_buffered_write(const uint16_t address, const uint16_t length, const uint8_t buff_size, const uint8_t write_cmnd) {
    uint8_t current_byte_index = 10;          /* Index of the first byte to send inside the JTAG2 command body */
    uint16_t bytes_remaining = length;          /* number of bytes to write */

    // Sends a block of bytes from the command body to memory, using the UPDI interface
    // On entry, the UPDI pointer must already point to the desired address
    // On exit, the UPDI pointer points to the next byte after the last one written
    // Returns updated index into the command body, pointing to the first unsent byte.
    auto updi_send_block = [] (uint8_t count, uint8_t index) {
      count--;
      NVM::wait<true>();
#ifndef NO_ACK_WRITE
      UPDI::rep(count);
      UPDI::stinc_b(JTAG2::packet.body[index]);
      for (uint8_t i = count; i; i--) {
        UPDI_io::put(JTAG2::packet.body[++index]);
        UPDI_io::get();
      }
#else
      UPDI::stcs(UPDI::reg::Control_A, 0x0E);
      UPDI::rep(count);
      UPDI::stinc_b_noget(JTAG2::packet.body[index]);
      for (uint8_t i = count; i; i--) {
        UPDI_io::put(JTAG2::packet.body[++index]);
      }
      UPDI::stcs(UPDI::reg::Control_A, 0x06);
#endif
      return ++index;
    };

    // Setup UPDI pointer for block transfer
    UPDI::stptr_w(address);
    /* Check address alignment, calculate number of unaligned bytes to send */
    uint8_t unaligned_bytes = (-address & (buff_size - 1));
    if (unaligned_bytes > bytes_remaining) unaligned_bytes = bytes_remaining;
    /* If there are unaligned bytes, they must be sent first */
    if (unaligned_bytes) {
      // Send unaligned block
      current_byte_index = updi_send_block(unaligned_bytes, current_byte_index);
      bytes_remaining -= unaligned_bytes;
      NVM::command<true>(write_cmnd);
    }
    while (bytes_remaining) {
      /* Send a buff_size amount of bytes */
      if (bytes_remaining >= buff_size) {
        current_byte_index = updi_send_block(buff_size, current_byte_index);
        bytes_remaining -= buff_size;
      }
      /* Send a NumBytes amount of bytes */
      else {
        current_byte_index = updi_send_block(bytes_remaining, current_byte_index);
        bytes_remaining = 0;
      }
      NVM::command<true>(write_cmnd);
    }
  }
}