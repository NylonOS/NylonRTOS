# Makefile for access_point example
PROGRAM=main
EXTRA_COMPONENTS = extras/dhcpserver extras/rboot-ota extras/libesphttpd extras/spiffs extras/esp-cjson  extras/stdin_uart_interrupt extras/paho_mqtt_c extras/rboot-ota extras/mbedtls extras/http_client_ota extras/sntp
FLASH_SIZE = 32
# spiffs configuration
SPIFFS_BASE_ADDR = 0x200000
SPIFFS_SIZE = 0x010000

ESP_IP ?= 192.168.240.1

#Tag for OTA images. 0-27 characters. Change to eg your projects title.
LIBESPHTTPD_OTA_TAGNAME ?= generic

LIBESPHTTPD_MAX_CONNECTIONS ?= 8
LIBESPHTTPD_STACKSIZE ?= 1024
PROGRAM_CFLAGS += -DFREERTOS -DLIBESPHTTPD_OTA_TAGNAME="\"$(LIBESPHTTPD_OTA_TAGNAME)\"" -DFLASH_SIZE=$(FLASH_SIZE) 
EXTRA_CFLAGS +=  -I../.. -DMEMP_NUM_NETCONN=$(LIBESPHTTPD_MAX_CONNECTIONS) -DSYSPARAM_DEBUG=0 -DWIFI_PARAM_SAVE=0


include ../../esp-open-rtos/common.mk

$(eval $(call make_spiffs_image,files))

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
	
dump-flash:
	esptool.py -p $(ESPPORT) read_flash 0x1f7000 8192 r1.bin
	hexdump -C r1.bin
	esptool.py -p $(ESPPORT) read_flash 0x1f9000 8192 r2.bin
	hexdump -C r2.bin



