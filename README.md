# Kinetis I2C Driver

This is a tiny driver that allows you to access I2C (IIC, I²C, or I squared C) on Freescale Kinetis microcontrollers. It works asynchronously (interrupt-driven), supports repeated start (restart) and does not depend on any large software framework.

# License

MIT. I believe in freedom, which means I believe in letting you do whatever you want with this code.

# Features

* Small.
* Works.
* Reads and writes.
* Implements repeated start.
* Uses the Bus Pirate convention.

# Limitations

This is not a fully-featured driver. Only master mode is implemented. Only 7-bit addressing is supported. Addressing is fully manual: it is your responsibility to shift the 7-bit I2C address to the left and add the R/W bit (actually, I see this as an advantage).

There is almost no error handling. You have to check for problems yourself.

# Rationale

I wanted to use Kinetis microcontrollers to communicate using I2C, but could not find a decent driver. The I2C module is very simple and needs quite a bit of code. That code is in CodeWarrior if you are using Processor Expert, but I did not like the Processor Expert approach. If you don’t want to use CodeWarrior or if you want to program starting with bare metal, you are on your own.

All examples I could find were overly simplistic and only worked in blocking mode. Well, I care about power consumption, so I only write asynchronous interrupt-driven code.

Obviously, once the Kinetis SDK gets written and actually supports all devices, this code will not be necessary. But I needed something now.

There is another reason for publishing the code. I wrote this and then had to put my Kinetis-related projects on hold. After several months I forgot having written this driver and started searching online for one… only to finally find it using Spotlight, on my hard drive. This is what happens if you work on too many projects. To avoid this happening in the future, I now intend to publish most things I write as soon as they are reasonably complete, so that I can find them online when I need them.

# Download

Get it directly from the [Github repository](https://github.com/jwr/kinetis_i2c).

# Usage

Here's an example of performing a write and then a read with repeated start (restart) from an MMA8451Q accelerometer with a 7-bit address of 0x1c.

```
  uint32_t status;
  uint16_t init_sequence[] = {0x3a, 0x0d, I2C_RESTART, 0x3b, I2C_READ};
  uint8_t device_id = 0;		/* will contain the device id after sequence has been processed */
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

  for(;;) {	   
    counter++;
  }
```

After successful transmission, `device_id` should contain the device id of the accelerometer (check with a debugger). The endless loop is there so that you can check the result of the I2C transmission. It is performed asynchronously, so without the loop the program would terminate before the transmission ended.

First, `i2c_init()` needs to be called. It takes three parameters: device number (devices are numbered starting from 0, most Kinetis microcontrollers have only one I2C device), `mult` and `icr`. The last two are raw values that will be written into the F register (see your device reference manual for details).

If you have more than one I2C module, remember to define `I2C_NUMBER_OF_DEVICES` appropriately in `i2c.h`.

Note that to successfully use I2C on Kinetis chips, you also have to:

1. Enable clock gating to the I2C module.
2. Enable clock gating to the PORT module that has the pins used for I2C.
3. Configure the PORT PCR registers so that I2C function is selected and the pins are set to open drain.
4. Set all I2C interrupt handlers in the interrupt vector table to the `i2c_isr()` function (if you use CodeWarrior, this is done in `Project_Settings/Startup_Code/kinetis_sysinit.c`.
5. Enable the appropriate IRQs.

Data transmission (both transmit and receive) is handled by `i2c_send_sequence()`. It sends a command/data sequence that can include restarts, writes and reads. Every transmission begins with a START, and ends with a STOP so you do not have to specify that. 

`i2c_send_sequence()` takes six parameters:

* `channel_number` is the I2C device number,
* `sequence` is the I2C operation sequence that should be performed. It can include any number of writes, restarts and reads. Note that the sequence is composed of `uint16_t`, not `uint8_t` elements. This is because we have to support out-of-band signalling of `I2C_RESTART` and `I2C_READ` operations, while still passing through 8-bit data.
* `sequence_length` is the number of sequence elements (not bytes). Sequences of arbitrary (well, 32-bit) length are supported, but there is an upper limit on the number of segments (restarts): no more than 42. This limit is imposed by the Linux ioctl() I2C interface. The minimum sequence length is (rather obviously) 2.
* `received_data` should point to a buffer that can hold as many bytes as there are `I2C_READ` operations in the   sequence. If there are no reads, 0 can be passed, as this parameter will not be used,
* `callback_fn` is a pointer to a function that will get called upon successful completion of the entire sequence. If 0 is   supplied, no function will be called. Note that the function will be called fron an interrupt handler, so it should do the absolute minimum possible (such as enqueue an event to be processed later, set a flag, exit sleep mode, etc).
* `user_data` is a pointer that will be passed to the `callback_fn`.

`i2c_send_sequence()` uses the Bus Pirate I2C convention, which I found to be very useful and compact. As an example, this
Bus Pirate sequence:

	 "[0x38 0x0c [ 0x39 r ]"

is specified as:

	 {0x38, 0x0c, I2C_RESTART, 0x39, I2C_READ};

in I2C terms, this sequence means:

1. Write 0x0c to device 0x1c (0x0c is usually the register address).
2. Do not release the bus.
3. Issue a repeated start.
4. Read one byte from device 0x1c (which would normally be the contents of register 0x0c on that device).

The sequence may read multiple bytes:

	{0x38, 0x16, I2C_RESTART, 0x39, I2C_READ, I2C_READ, I2C_READ};

This will normally read three bytes from device 0x1c starting at register 0x16. In this case you need to provide a pointer to a buffer than can hold three bytes.

Note that start and stop are added for you automatically, but addressing is fully manual: it is your responsibility to shift the 7-bit I2C address to the left and add the R/W bit. The examples above communicate with a device whose I2C address is 0x1c, which shifted left gives 0x38. For reads we use 0x39, which is `(0x1c<<1)|1`.

If you wonder why I consider the Bus Pirate convention useful, note that what you specify in the sequence is very close to the actual bytes on the wire. This makes debugging and reproducing other sequences easy. Also, you can use the Bus Pirate to prototype, and then easily convert the tested sequences into actual code.

# Devices

I only tested this code on FRDM-K20Z board with a Kinetis MK20DX128. It should work on all Kinetis devices that have one or more I2C modules, but I haven’t tried it. If you want to use more than one module, you will have to adjust `I2C_NUMBER_OF_DEVICES`.

If you have a Kinetis L device device with the 1N96F mask, enable the `ERRATA_1N96F_WORKAROUND` define. It enables a workaround for issue 6070: I2C: Repeat start cannot be generated if the `I2Cx_F[MULT]` field is set to a non-zero value.

# Building and Packaging

You can build the example in CodeWarrior by dropping it into a newly created bare metal project for your Kinetis chip. Place `main.c` and `i2c.c` in `Sources`, place `i2c.h` in `Project_Headers` and remember to set the appropriate interrupt vector in `Project_Settings/Startup_Code/kinetis_sysinit.c` to the `i2c_isr()` function.

Packaging? Come on. What packaging? Just put those two files in your project. Or put the git repo in as a subproject. Or package it any way you wish — but I'm afraid I won't be able to help.
