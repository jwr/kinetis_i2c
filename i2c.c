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

#include <stdint.h>
#include "derivative.h"
#include "i2c.h"

volatile I2C_Channel i2c_channels[I2C_NUMBER_OF_DEVICES];

#ifdef I2C_BASE_PTRS
/* For Kinetis L series Freescale provides a convenient array of I2C base pointers. */
I2C_MemMapPtr i2c_base_ptrs[2] = I2C_BASE_PTRS;
#else
/* For the K series, however, the array is missing (why?), so we only initialize the first I2C peripheral. This should
   not be a big problem, because I've yet to find a K-series device with more than one I2C module. */
I2C_MemMapPtr i2c_base_ptrs[1] = { I2C0_BASE_PTR };
#endif

uint32_t i2c_init(uint8_t i2c_number, uint8_t mult, uint8_t icr) {
  I2C_MemMapPtr i2c_base_ptr = i2c_base_ptrs[i2c_number];

  i2c_base_ptr = i2c_base_ptrs[i2c_number];
  I2C_C1_REG(i2c_base_ptr) = 0;
  I2C_C1_REG(i2c_base_ptr) |= I2C_C1_IICEN_MASK;
  I2C_F_REG(i2c_base_ptr) &= ~0xf;
  I2C_F_REG(i2c_base_ptr) |= ((mult << I2C_F_MULT_SHIFT) | icr);

  return i2c_number;
}


/* These are here for readability and correspond to bit 0 of the address byte. */
#define I2C_WRITING 0
#define I2C_READING 1

int32_t i2c_send_sequence(uint32_t channel_number, uint16_t *sequence, uint32_t sequence_length, uint8_t *received_data,
						  void (*callback_fn)(void*), void *user_data) {
  volatile I2C_Channel *channel = &(i2c_channels[channel_number]);
  I2C_MemMapPtr i2c_base_ptr = i2c_base_ptrs[channel_number];
  int32_t result = 0;
  uint8_t status;

  if(channel->status == I2C_BUSY) {
	return -1;
  }

  channel->sequence = sequence;
  channel->sequence_end = sequence + sequence_length;
  channel->received_data = received_data;
  channel->status = I2C_BUSY;
  channel->txrx = I2C_WRITING;
  channel->callback_fn = callback_fn;
  channel->user_data = user_data;

  /* reads_ahead does not need to be initialized */

  I2C_S_REG(i2c_base_ptr) |= I2C_S_IICIF_MASK; /* Acknowledge the interrupt request, just in case */
  I2C_C1_REG(i2c_base_ptr) = (I2C_C1_IICEN_MASK | I2C_C1_IICIE_MASK);

  /* Generate a start condition and prepare for transmitting. */
  I2C_C1_REG(i2c_base_ptr) |= (I2C_C1_MST_MASK | I2C_C1_TX_MASK);

  status = I2C_S_REG(i2c_base_ptr);
  if(status & I2C_S_ARBL_MASK) {
	result = -1;
	goto i2c_send_sequence_cleanup;
  }

  /* Write the first (address) byte. */
  I2C_D_REG(i2c_base_ptr) = *channel->sequence++;

  return result;                /* Everything is OK. */

 i2c_send_sequence_cleanup:
  I2C_C1_REG(i2c_base_ptr) &= ~(I2C_C1_IICIE_MASK | I2C_C1_MST_MASK | I2C_C1_TX_MASK);
  channel->status = I2C_ERROR;
  return result;
}


void i2c_isr(void) {
  volatile I2C_Channel* channel;
  I2C_MemMapPtr i2c_base_ptr;
  uint8_t channel_number;
  uint16_t element;
  uint8_t status;

#ifdef ERRATA_1N96F_WORKAROUND
  uint8_t f_register;
#endif

  /* Loop over I2C modules. For the most common use case where I2C_NUMBER_OF_DEVICES == 1, the compiler should optimize
	 this loop out entirely. */
  for(channel_number = 0; channel_number < I2C_NUMBER_OF_DEVICES; channel_number++) {
	channel = &i2c_channels[channel_number];
	i2c_base_ptr = i2c_base_ptrs[channel_number];

	status = I2C_S_REG(i2c_base_ptr);

	/* Was the interrupt request from the current I2C module? */
	if(!(status & I2C_S_IICIF_MASK)) {
	  continue;                 /* If not, proceed to the next I2C module. */
	}

	I2C_S_REG(i2c_base_ptr) |= I2C_S_IICIF_MASK; /* Acknowledge the interrupt request. */

	if(status & I2C_S_ARBL_MASK) {
	  I2C_S_REG(i2c_base_ptr) |= I2C_S_ARBL_MASK;
	  goto i2c_isr_error;
	}

	if(channel->txrx == I2C_READING) {

	  switch(channel->reads_ahead) {
	  case 0:
		/* All the reads in the sequence have been processed (but note that the final data register read still needs to
		   be done below! Now, the next thing is either a restart or the end of a sequence. In any case, we need to
		   switch to TX mode, either to generate a repeated start condition, or to avoid triggering another I2C read
		   when reading the contents of the data register. */
		I2C_C1_REG(i2c_base_ptr) |= I2C_C1_TX_MASK;

		/* Perform the final data register read now that it's safe to do so. */
		*channel->received_data++ = I2C_D_REG(i2c_base_ptr);

		/* Do we have a repeated start? */
		if((channel->sequence < channel->sequence_end) && (*channel->sequence == I2C_RESTART)) {

		  /* Issue 6070: I2C: Repeat start cannot be generated if the I2Cx_F[MULT] field is set to a non-zero value. */
#ifdef ERRATA_1N96F_WORKAROUND
		  f_register = I2C_F_REG(i2c_base_ptr);
		  I2C_F_REG(i2c_base_ptr) = (f_register & 0x3f); /* Zero out the MULT bits (topmost 2 bits). */
#endif

		  I2C_C1_REG(i2c_base_ptr) |= I2C_C1_RSTA_MASK; /* Generate a repeated start condition. */

#ifdef ERRATA_1N96F_WORKAROUND
		  I2C_F_REG(i2c_base_ptr) = f_register;
#endif
		  /* A restart is processed immediately, so we need to get a new element from our sequence. This is safe, because
			 a sequence cannot end with a RESTART: there has to be something after it. Note that the only thing that can
			 come after a restart is an address write. */
		  channel->txrx = I2C_WRITING;
		  channel->sequence++;
		  element = *channel->sequence;
		  I2C_D_REG(i2c_base_ptr) = element;
		} else {
		  goto i2c_isr_stop;
		}
		break;

	  case 1:
		I2C_C1_REG(i2c_base_ptr) |= ~I2C_C1_TXAK_MASK; /* do not ACK the final read */
		*channel->received_data++ = I2C_D_REG(i2c_base_ptr);
		break;

	  default:
		*channel->received_data++ = I2C_D_REG(i2c_base_ptr);
		break;
	  }

	  channel->reads_ahead--;

	} else {                    /* channel->txrx == I2C_WRITING */
	  /* First, check if we are at the end of a sequence. */
	  if(channel->sequence == channel->sequence_end) {
		goto i2c_isr_stop;
	  }

	  if(status & I2C_S_RXAK_MASK) {
		/* We received a NACK. Generate a STOP condition and abort. */
		goto i2c_isr_error;
	  }

	  /* check next thing in our sequence */
	  element = *channel->sequence;

	  if(element == I2C_RESTART) {
		/* Do we have a restart? If so, generate repeated start and make sure TX is on. */
		I2C_C1_REG(i2c_base_ptr) |= I2C_C1_RSTA_MASK | I2C_C1_TX_MASK;
		/* A restart is processed immediately, so we need to get a new element from our sequence. This is safe, because a
		   sequence cannot end with a RESTART: there has to be something after it. */
		channel->sequence++;
		element = *channel->sequence;
		/* Note that the only thing that can come after a restart is a write. */
		I2C_D_REG(i2c_base_ptr) = element;
	  } else {
		if(element == I2C_READ) {
		  channel->txrx = I2C_READING;
		  /* How many reads do we have ahead of us (not including this one)? For reads we need to know the segment length
			 to correctly plan NACK transmissions. */
		  channel->reads_ahead = 1;        /* We already know about one read */
		  while(((channel->sequence + channel->reads_ahead) < channel->sequence_end) &&
				(*(channel->sequence + channel->reads_ahead) == I2C_READ)) {
			channel->reads_ahead++;
		  }
		  I2C_C1_REG(i2c_base_ptr) &= ~I2C_C1_TX_MASK; /* Switch to RX mode. */

		  if(channel->reads_ahead == 1) {
			I2C_C1_REG(i2c_base_ptr) |= I2C_C1_TXAK_MASK; /* do not ACK the final read */
		  }
		  /* Dummy read comes first, note that this is not valid data! This only triggers a read, actual data will come
			 in the next interrupt call and overwrite this. This is why we do not increment the received_data
			 pointer. */
		  *channel->received_data = I2C_D_REG(i2c_base_ptr);
		  channel->reads_ahead--;
		} else {
		  /* Not a restart, not a read, must be a write. */
		  I2C_D_REG(i2c_base_ptr) = element;
		}
	  }
	}

	channel->sequence++;
	continue;

  i2c_isr_stop:
	/* Generate STOP (set MST=0), switch to RX mode, and disable further interrupts. */
	I2C_C1_REG(i2c_base_ptr) &= ~(I2C_C1_MST_MASK | I2C_C1_IICIE_MASK | I2C_C1_TXAK_MASK);
	channel->status = I2C_AVAILABLE;
	/* Call the user-supplied callback function upon successful completion (if it exists). */
	if(channel->callback_fn) {
	  (*channel->callback_fn)(channel->user_data);
	}
	continue;

  i2c_isr_error:
	I2C_C1_REG(i2c_base_ptr) &= ~(I2C_C1_MST_MASK | I2C_C1_IICIE_MASK); /* Generate STOP and disable further interrupts. */
	channel->status = I2C_ERROR;
	continue;
  }
}
