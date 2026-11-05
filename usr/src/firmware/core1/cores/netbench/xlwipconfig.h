/* xlwipconfig.h -- Xilinx's lwIP copy (the firmware's BSP) includes this from
   lwip/prot/ethernet.h: no AXI Ethernet, no IEEE 1588 padding on core 1 */
#ifndef XLWIPCONFIG_H
#define XLWIPCONFIG_H
#define XLWIP_CONFIG_AXI_ETHERNET_ENABLE_1588 0
#endif
