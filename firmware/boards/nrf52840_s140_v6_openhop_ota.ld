/* RAK4631 S140 v6 layout with a fixed dual-bank Ethernet OTA boundary. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  /* Bank 0 ends where the matching 0x62000-byte staging bank begins. */
  FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0x88000 - 0x26000

  /* SRAM required by the SoftDevice depends on the configured BLE services. */
  RAM (rwx) : ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM

  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
