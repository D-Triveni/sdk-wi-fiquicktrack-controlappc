#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()

set(SOURCES_BASE ${CMAKE_CURRENT_SOURCE_DIR}/../)
# Enable of same options as upstream points to issues with Zephyr
# headers, so, for now, we disable them.
zephyr_library_compile_options_ifdef(CONFIG_WFA_QT_CONTROL_APP
	-Werror
	# False positive, not going away even with null check
	-Wno-format-overflow
)

zephyr_library_compile_definitions_ifdef(CONFIG_WFA_QT_CONTROL_APP
	CONFIG_ZEPHYR
)

zephyr_library_compile_definitions_ifdef(CONFIG_WFA_QT_FEAT_P2P
	CONFIG_FEAT_P2P
)

zephyr_library_compile_definitions_ifdef(CONFIG_WPA_QT_FEAT_WPS
	CONFIG_FEAT_WPS
	FEAT_EAP_WSC
)

zephyr_library_compile_definitions_ifdef(CONFIG_WFA_QT_FEAT_HS20
	CONFIG_FEAT_HS20
)

zephyr_include_directories_ifdef(CONFIG_WFA_QT_CONTROL_APP
	${SOURCES_BASE}
	${SOURCES_BASE}/zephyr/include
)

zephyr_library_sources_ifdef(CONFIG_WFA_QT_CONTROL_APP
	# Zephyr's port of the Indigo API
	${SOURCES_BASE}/zephyr/src/zephyr_dut_main.c
	${SOURCES_BASE}/zephyr/src/indigo_api_callback_dut.c
	${SOURCES_BASE}/zephyr/src/vendor_specific_dut.c
	${SOURCES_BASE}/zephyr/src/utils.c
	${SOURCES_BASE}/eloop.c
	${SOURCES_BASE}/indigo_api.c
	${SOURCES_BASE}/indigo_packet.c
)
