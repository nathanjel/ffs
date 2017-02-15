#
# Main component makefile.
#

ifdef CONFIG_FFS_ENABLED

PARTITION_TABLE_ROOT := $(call dequote,$(if $(CONFIG_PARTITION_TABLE_CUSTOM),$(PROJECT_PATH),$(COMPONENT_PATH)))
PARTITION_TABLE_CSV_PATH := $(call dequote,$(abspath $(PARTITION_TABLE_ROOT)/$(subst $(quote),,$(CONFIG_PARTITION_TABLE_FILENAME))))

COMPONENT_EXTRA_CLEAN := $(COMPONENT_BUILD_DIR)/ffs_files.h
COMPONENT_EXTRA_CLEAN += $(BUILD_DIR_BASE)/ffs-flash.map
COMPONENT_EXTRA_CLEAN += $(BUILD_DIR_BASE)/ffs.binary

ffs.o: $(COMPONENT_BUILD_DIR)/ffs_files.h

ffs.o: CFLAGS += -I$(COMPONENT_BUILD_DIR)

$(BUILD_DIR_BASE)/ffs-flash.map: $(COMPONENT_BUILD_DIR)/ffs_files.h

$(BUILD_DIR_BASE)/ffs.binary: $(COMPONENT_BUILD_DIR)/ffs_files.h

$(COMPONENT_BUILD_DIR)/ffs_files.h: $(PROJECT_PATH)/$(call dequote,$(CONFIG_FFS_FILES_FOLDER_PATH)) $(PROJECT_PATH)/sdkconfig
	$(PYTHON) $(COMPONENT_PATH)/build.py \
		$(IDF_PATH)/components/partition_table/ \
		$(PROJECT_PATH)/$(call dequote,$(CONFIG_FFS_FILES_FOLDER_PATH)) \
		$(COMPONENT_BUILD_DIR)/ffs_files.h $(BUILD_DIR_BASE)/ffs-flash.map $(BUILD_DIR_BASE)/ffs.binary \
		$(PARTITION_TABLE_CSV_PATH) \
		$(CONFIG_FFS_FILES_PARTITION) $(CONFIG_FFS_FILES_DEF_GROWSIZE) \
		$(CONFIG_FFS_FILES_OPTIONS)

else

COMPONENT_OBJS := 

endif