obj-m+=galeos.o

all: module

debug: CXXFLAGS += -DDEBUG -g
debug: CCFLAGS += -DDEBUG -g
debug: module


module:
	make -C /home/dmitriy/extfs/linux-2.6-imx/ M=$(PWD) modules

debug:
        make -C /home/dmitriy/extfs/linux-2.6-imx/ M=$(PWD) modules 

clean:
	make -C /home/dmitriy/extfs/linux-2.6-imx/ M=$(PWD) clean

