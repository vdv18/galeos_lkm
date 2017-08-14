Galeos LKM driver.

Version 0.1
- Read/Write register number (/sys/class/galeos/{device}/reg)
- Read/Write register data   (/sys/class/galeos/{device}/reg)
- Read driver version (*/driver_version)
- Read device version from 0x60 register (*/device_version)
- Read device type from 0x61 register (*/device_type)

Version 0.2
- Add Read/Write speed/pam and mode change. 
 (/sys/class/galeos/{device}/DSL/speed{0-3})
 (/sys/class/galeos/{device}/DSL/pam{0-3})
 (/sys/class/galeos/{device}/DSL/mode{0-3})
