#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := esp_cloud_ota

EXTRA_COMPONENT_DIRS += $(PROJECT_PATH)/../../components

include $(IDF_PATH)/make/project.mk
FW_VERSION ?= 1.0
CPPFLAGS += -DFW_VERSION=\"$(FW_VERSION)\"
