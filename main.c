/*
  Copyright (C) 2014 Jan Rychter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

/* This is an example that demonstrates the use of I2C code. It is only intended to be run under a debugger. */

#include "derivative.h"			/* include peripheral declarations */
#include "i2c.h"

/* We need this function to enable an irq at runtime. */
static void enable_irq(uint32_t irq) {
  uint32_t div;
  div = irq/32;
  switch(div) {
  case 0:
	NVICICPR0 |= 1 << (irq%32);
	NVICISER0 |= 1 << (irq%32);
	break;
  case 1:
	NVICICPR1 |= 1 << (irq%32);
	NVICISER1 |= 1 << (irq%32);
	break;
  case 2:
	NVICICPR2 |= 1 << (irq%32);
	NVICISER2 |= 1 << (irq%32);
	break;
  }
}

void my_callback(void *data) {
  /* This callback function gets called once the sequence has been processed. Note that this gets called from an ISR, so
	 it should do as little as possible. */
}

/* This example attempts to read the device id of the MMA8451Q accelerometer that is included on the Kinetis K20 eval
   boards. */
int main(void)
{
  int counter = 0;
  uint32_t status;
  uint16_t init_sequence[] = {0x3a, 0x0d, I2C_RESTART, 0x3b, I2C_READ};
  uint8_t device_id = 0;		/* will contain the device id after sequence has been processed */

  enable_irq(INT_I2C0 - 16);

  SIM_SCGC4 |= SIM_SCGC4_I2C0_MASK;
  SIM_SCGC5 |= SIM_SCGC5_PORTB_MASK;

  PORTB_PCR0 = PORT_PCR_MUX(0x02) | PORT_PCR_ODE_MASK;
  PORTB_PCR1 = PORT_PCR_MUX(0x02) | PORT_PCR_ODE_MASK;

  status = i2c_init(0, 0x01, 0x20);
  status = i2c_send_sequence(0, init_sequence, 5, &device_id, my_callback, (void*)0x1234);
  
  /* This endless loop is here so that you can check the result of the I2C transmission. It is performed asynchronously,
	 so without the loop the program would terminate before the transmission ended. */
  for(;;) {
	counter++;
  }

  return 0;
}
