# mkt_gpio_driver

make

sudo insmod Module.ko
cat /dev/mydevice0
sudo rmmod Module.ko


             3V3  ( 1)  ( 2)  5V
SDA1 I2C  GPIO 2  ( 3)  ( 4)  5V
SCL1 I2C  GPIO 3  ( 5)  ( 6)  Ground
          GPIO 4  ( 7)  ( 8)  GPIO14
          Ground  ( 9)  (10)  GPIO15
          GPIO17  (11)  (12)  GPIO16
          GPIO27  (13)  (14)  Ground
          GPIO22  (15)  (16)  GPIO23
             3V3  (17)  (18)  GPIO24
          GPIO10  (19)  (20)  Ground
          GPIO 9  (21)  (22)  GPIO25
          GPIO11  (23)  (24)  GPIO 8
          Ground  (25)  (26)  GPIO 7
           ID_SD  (27)  (28)  ID_SC
          GPIO 5  (29)  (30)  Ground
          GPIO 6  (31)  (32)  GPIO12
          GPIO13  (33)  (34)  Ground
          GPIO19  (35)  (36)  GPIO16
          GPIO26  (37)  (38)  GPIO20
          Ground  (39)  (40)  GPIO21
          