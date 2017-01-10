#define FFS_FILE_LIST \
ota0_bin, \
power_dat, \
ota1_bin, \
defaults_json, \
config_factory_cfg, \
config_device_cfg, \
secure_node_crt, \
secure_node_key, \

#define FFS_FILE_METADATA \
		{ "/ota0.bin", ota0_bin, 0, 1048576, 0x110000, 0x0 }, \
		{ "/power.dat", power_dat, 0, 851968, 0x330000, 0x0 }, \
		{ "/ota1.bin", ota1_bin, 0, 1048576, 0x210000, 0x0 }, \
		{ "/defaults.json", defaults_json, 292, 4096, 0x310000, 0x0 }, \
		{ "/config/factory.cfg", config_factory_cfg, 0, 4096, 0x310000, 0x1000 }, \
		{ "/config/device.cfg", config_device_cfg, 0, 4096, 0x310000, 0x2000 }, \
		{ "/secure/node.crt", secure_node_crt, 1419, 4096, 0x310000, 0x3000 }, \
		{ "/secure/node.key", secure_node_key, 1704, 4096, 0x310000, 0x4000 }, \

