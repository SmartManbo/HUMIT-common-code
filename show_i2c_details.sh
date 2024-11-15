#!/bin/bash

# I2C buses need to be checked
buses=(0 1 2 3)

for bus in "${buses[@]}"; do
    # get clock frequency
    var=$(xxd /sys/class/i2c-adapter/i2c-$bus/of_node/clock-frequency | awk -F': ' '{print $2}')
    if [ -z "$var" ]; then
        echo "Failed to read clock frequency for i2c-$bus."
        continue
    fi
    var=${var//[[:blank:].\}]/}

    # print clock frequency in decimal
    printf "i2c-%d frequency = %d Hz\n" $bus $((16#$var))

    # detect all devices on a I2C bus
    echo "Devices on i2c-$bus:"
    if ! i2cdetect -y -r -a $bus; then
        echo "i2cdetect failed for i2c-$bus."
    fi
done