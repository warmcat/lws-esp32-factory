#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
PROJECT_NAME := lws-esp32-factory
EXTRA_COMPONENT_DIRS := components

# 0 = OTA Application, 1 = Factory (Recovery) Application
LWS_IS_FACTORY_APPLICATION=1
export LWS_IS_FACTORY_APPLICATION
export A
export F

include $(IDF_PATH)/make/project.mk
include sdkconfig
include ${PWD}/components/libwebsockets/scripts/esp32.mk

CFLAGS+= -I$(PROJECT_PATH)/components/libwebsockets/plugins \
	 -I$(PROJECT_PATH)/components/libwebsockets/lib \
	 -I$(IDF_PATH)/components/heap/include \
	 -I$(IDF_PATH)/components/soc/include \
	 -I$(IDF_PATH)/components/vfs/include \
	 -DLWS_IS_FACTORY_APPLICATION=$(LWS_IS_FACTORY_APPLICATION) \
	 -I$(IDF_PATH)/components/soc/esp32/include/ \
	 -I$(IDF_PATH)/components/esp32/include
export IDF_PATH
