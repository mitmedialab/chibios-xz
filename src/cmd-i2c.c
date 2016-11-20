#include "hal.h"
#include "shell.h"
#include "chprintf.h"
#include <stdlib.h>

// 010_1000
#define CAP1208_ADDR  0x28
// 111_1000
#define RH6010_ADDR  0xf0

#define STC3115_ADDR 0x70
#define FAN5421_ADDR 0x6A
#define LC709203_ADDR 0x0B

void i2cCommand(BaseSequentialStream *chp, int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  uint8_t tx[1], rx[32];
  msg_t retval;
  int i;

  tx[0] = 0;
  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, CAP1208_ADDR, tx, 1, rx, 32, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);

  for( i = 0; i < 32; i++ ) {
    if( (i % 8) == 0 )
      chprintf(chp, SHELL_NEWLINE_STR"%02x: ", i );
    
    chprintf(chp, "%02x ", rx[i]);
  }
  chprintf(chp, SHELL_NEWLINE_STR );
  return;
}

void capWCommand(BaseSequentialStream *chp, int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  uint8_t tx[2], rx[1];
  msg_t retval;

  if( argc != 2 ) {
    chprintf(chp, "Usage: c <addr> <data>"SHELL_NEWLINE_STR);
  }
  tx[0] = (uint8_t) (strtol(argv[0], NULL, 0) & 0xFF);
  tx[1] = (uint8_t) (strtol(argv[1], NULL, 0) & 0xFF);
  

  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, CAP1208_ADDR, tx, 2, rx, 0, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);

  chprintf(chp, "retval: %d"SHELL_NEWLINE_STR, retval == MSG_OK ? 1 : 0);
  return;
}

void capRCommand(BaseSequentialStream *chp, int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  uint8_t tx[1], rx[1];
  msg_t retval;

  if( argc != 1 ) {
    chprintf(chp, "Usage: cr <addr>"SHELL_NEWLINE_STR);
  }
  tx[0] = (uint8_t) (strtol(argv[0], NULL, 0) & 0xFF);
  

  i2cAcquireBus(&I2CD1);
  retval = i2cMasterTransmitTimeout(&I2CD1, CAP1208_ADDR, tx, 1, rx, 1, TIME_INFINITE);
  i2cReleaseBus(&I2CD1);

  chprintf(chp, "read: %02x, retval: %d"SHELL_NEWLINE_STR, rx[0], retval == MSG_OK ? 1 : 0);
  return;
}

