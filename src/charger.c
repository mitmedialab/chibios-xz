#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "shell.h"
#include "chprintf.h"
#include "i2c.h"
#include "charger.h"
#include "orchard-ui.h"

#define NL SHELL_NEWLINE_STR

virtual_timer_t chg_vt;
event_source_t chg_keepalive_event;

static int keepalive_mod = 0;

int16_t ggStateofCharge(void) {
  uint8_t tx[4], rx[3];
  msg_t retval;

  tx[0] = 0x0f; // ITE register
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, LC709203_ADDR, tx, 1, rx, 3, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  if( retval != MSG_OK ) {
    chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
  }
  //chprintf(stream, "State of charge: %.1f%%"NL, (rx[0] | (rx[1] << 8)) / 10.0);
  return (int16_t) (rx[0] | (rx[1] << 8)) / 10;
}

int16_t ggVoltage(void) {
  uint8_t tx[4], rx[3];
  msg_t retval;
  
  tx[0] = 0x09; // voltage register
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, LC709203_ADDR, tx, 1, rx, 3, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  if( retval != MSG_OK ) {
    chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
  }
  //  chprintf(stream, "Voltage: %dmV"NL, (rx[0] | (rx[1] << 8)));
  return (rx[0] | (rx[1] << 8));
}

static uint8_t crc_update(uint8_t incrc, uint8_t indata) {
  uint8_t i;
  uint8_t data;

  data = incrc ^ indata;
  for( i = 0; i < 8; i++ ) {
    if(( data & 0x80) != 0) {
      data <<= 1;
      data ^= 0x07;
    } else {
      data <<= 1;
    }
  }
  
  return data;
}

void comp_crc8(uint8_t *tx) {
  int i;
  uint8_t data[5]; // need to include the I2C address

  // pre-pend the I2C address to the tx buffer in a local copy
  data[0] = 0x16; // they use the left-shifted version for computations
  // note for reads this changes to 0x17
  for( i = 1; i < 4; i++ ) {
    data[i] = tx[i-1];
  }
  
  data[4] = 0x00;
  for( i = 0; i < 4; i++ ) {
    data[4] = crc_update(data[4], data[i]);
  }
  
  tx[3] = data[4];
}

void ggOn(void) {
  uint8_t tx[4];
  
  tx[0] = 0x15; // power mode
  i2cAcquireBus(&I2CD1);
  tx[1] = 0x1;
  tx[2] = 0x0;
  comp_crc8(tx);
  i2cMasterTransmitTimeout(&I2CD1, LC709203_ADDR, tx, 4, NULL, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
}


void chgKeepaliveHandler(eventid_t id) {
  (void) id;
  uint8_t tx[2];
  uint8_t rx[2];

  tx[0] = FAN5421_CTL0_ADR;
  tx[1] = 0xC0; // 32sec timer reset, enable stat pin

  i2cAcquireBus(&I2CD1);
  i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, NULL, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);

  // read gg voltage register
  tx[0] = 0x09; // voltage register
  i2cAcquireBus(&I2CD1);
  i2cMasterTransmitTimeout(&I2CD1, LC709203_ADDR, tx, 1, rx, 3, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  
  uibat.batt_mv = rx[0] | (rx[1] << 8);
#if 0   // for debug only
  if( (keepalive_mod++ % 10) == 0 )
    chprintf((BaseSequentialStream *)&SD4, " %dmV"NL, uibat.batt_mv);
#endif

  tx[0] = 0x0f; // ITE register
  i2cAcquireBus(&I2CD1);
  i2cMasterTransmitTimeout(&I2CD1, LC709203_ADDR, tx, 1, rx, 3, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  uibat.batt_soc = (rx[0] | (rx[1] << 8));
}

void chgSetSafety(void) {
  uint8_t tx[2];

  tx[0] = FAN5421_SAFE_ADR;
  tx[1] = 0x62; // 1150mA, 4.24V
  //  tx[1] = 0x02; // 550mA, 4.24V
  i2cAcquireBus(&I2CD1);
  i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, NULL, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
}

void chgAutoParams(void) {
  uint8_t tx[2];
  uint8_t rx[1];
  msg_t retval;
  
  // now set current targets
  tx[0] = FAN5421_IBAT_ADR;
  tx[1] = 0x3 << 3 | 0x2; // 850mA, termination at 146mA (~C/10)
  // tx[1] = 0x0 << 3 | 0x1; // 550mA, termination at 97mA (~C/5)
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, rx, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  if( retval != MSG_OK ) {
    chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
  }
    
  tx[0] = FAN5421_SPCHG_ADR;
  tx[1] = 0x04; // use IOCHARGE to set target current, VSP set to 4.52V
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, rx, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  if( retval != MSG_OK ) {
    chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
  }
    
  // target "float" voltage
  tx[0] = FAN5421_OREG_ADR;
  tx[1] = (0x22 << 2); // target 4.18 float voltage
  // tx[1] = (0x19 << 2); // target 4.00 float voltage
  // tx[1] = (0x1E << 2); // target 4.10v float voltage
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, rx, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);
  if( retval != MSG_OK ) {
    chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
  }
    
}

void chg_cb(void *arg) {
  (void) arg;
  chSysLockFromISR();
  chEvtBroadcastI(&chg_keepalive_event);
  chVTSetI(&chg_vt, MS2ST(1000), chg_cb, NULL);
  chSysUnlockFromISR();
}

void chgStart(int force) {
  uint8_t tx[2], rx[1];
  msg_t retval;

  if( force ) {
    // force initiate charging
    tx[0] = FAN5421_CTL1_ADR;
    tx[1] = 0x18; // weak battery: 3.5V; charge current termination; charger enabled
    i2cAcquireBus(&I2CD1);
    retval = i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, rx, 0, TIME_INFINITE);
    i2cReleaseBus(&I2CD1);
    if( retval != MSG_OK ) {
      chprintf((BaseSequentialStream *)&SD4, " I2C transaction error: %d"NL, i2cGetErrors(&I2CD1));
    }
  } else {
    tx[0] = FAN5421_CTL1_ADR;
    i2cAcquireBus(&I2CD1);
    i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 1, rx, 1, TIME_INFINITE);
    i2cReleaseBus(&I2CD1);
    
    tx[1] = rx[0] & 0xFB; // bit 2 low to start charging
    tx[0] = FAN5421_CTL1_ADR;
    i2cAcquireBus(&I2CD1);
    i2cMasterTransmitTimeout(&I2CD1, FAN5421_ADDR, tx, 2, rx, 0, TIME_INFINITE);
    i2cReleaseBus(&I2CD1);
  }
    
  chVTSet(&chg_vt, MS2ST(1000), chg_cb, NULL);
}

void chargerShipMode(void) {
  palClearPad(IOPORT2, 1); // force ship mode, should shut the whole thing down...
}
