#ifndef _AMIGA_NE2000_H
#define _AMIGA_NE2000_H

/*
** NE2000 / DP8390 register offsets (multiply by 2 for Zorro II 16-bit access)
*/

#define NE_CR            0x00
#define NE_CLDA0         0x01
#define NE_CLDA1         0x02
#define NE_BNRY          0x03
#define NE_TSR           0x04
#define NE_NCR           0x05
#define NE_FIFO          0x06
#define NE_ISR           0x07
#define NE_CRDA0         0x08
#define NE_CRDA1         0x09
#define NE_RBCNT0        0x0a
#define NE_RBCNT1        0x0b
#define NE_RSR           0x0c
#define NE_TBCNT0        0x05
#define NE_TBCNT1        0x06
#define NE_CNTR0         0x0d
#define NE_CNTR1         0x0e
#define NE_CNTR2         0x0f
#define NE_IMR          0x0f

/*
** Page 0 registers (write-only)
*/
#define NE_PSTART        0x01
#define NE_PSTOP         0x02
#define NE_TPSR          0x04
#define NE_TBCNT0_W      0x05
#define NE_TBCNT1_W      0x06
#define NE_RSAR0         0x08
#define NE_RSAR1         0x09
#define NE_RBCR0_W       0x0a
#define NE_RBCR1_W       0x0b
#define NE_RCR           0x0c
#define NE_TCR           0x0d
#define NE_DCR           0x0e
#define NE_IMR           0x0f

/*
** Page 1 registers
*/
#define NE_PAR0          0x01
#define NE_PAR1          0x02
#define NE_PAR2          0x03
#define NE_PAR3          0x04
#define NE_PAR4          0x05
#define NE_PAR5          0x06
#define NE_CURR          0x07
#define NE_MAR0          0x08
#define NE_MAR1          0x09
#define NE_MAR2          0x0a
#define NE_MAR3          0x0b
#define NE_MAR4          0x0c
#define NE_MAR5          0x0d
#define NE_MAR6          0x0e
#define NE_MAR7          0x0f

/*
** Transmit Configuration Register (TCR) bits
*/
#define NE_TCR_NORMAL    0x00
#define NE_TCR_LB0       0x02
#define NE_TCR_LB1       0x04
#define NE_TCR_ATD       0x08
#define NE_TCR_OFST      0x10

/*
** Receive Configuration Register (RCR) bits
*/
#define NE_RCR_SEP       0x01
#define NE_RCR_AR        0x02
#define NE_RCR_AB        0x04
#define NE_RCR_AM        0x08
#define NE_RCR_PRO       0x10
#define NE_RCR_MON       0x20

/*
** Interrupt Mask Register (IMR) bits
*/
#define NE_IMR_PRXE      0x01
#define NE_IMR_PTXE      0x02
#define NE_IMR_RXEE      0x04
#define NE_IMR_TXEE      0x08
#define NE_IMR_OVWE      0x10
#define NE_IMR_CNTE      0x20
#define NE_IMR_RDCE      0x40

/*
** Command Register (CR) bits
*/
#define NE_CR_P0         0x00
#define NE_CR_P1         0x40
#define NE_CR_P2         0x80
#define NE_CR_PS         0xc0
#define NE_CR_STP        0x01
#define NE_CR_STA        0x02
#define NE_CR_TXP        0x04
#define NE_CR_RDMA_PULL  0x08
#define NE_CR_RDMA_STORE 0x10
#define NE_CR_RDMA_ABORT 0x20
#define NE_CR_NODMA     0x20

/*
** Interrupt Status Register (ISR) bits
*/
#define NE_ISR_PRX       0x01
#define NE_ISR_PTX       0x02
#define NE_ISR_RXE       0x04
#define NE_ISR_TXE       0x08
#define NE_ISR_OVW       0x10
#define NE_ISR_CNT       0x20
#define NE_ISR_RDC       0x40
#define NE_ISR_RST       0x80

/*
** Transmit Status Register (TSR) bits
*/
#define NE_TSR_PTX       0x01
#define NE_TSR_DFR       0x02
#define NE_TSR_COL       0x04
#define NE_TSR_ABT       0x08
#define NE_TSR_CRS       0x10
#define NE_TSR_FU        0x20
#define NE_TSR_CDH       0x40
#define NE_TSR_OWC       0x80

/*
** Receive Status Register (RSR) bits
*/
#define NE_RSR_PRX       0x01
#define NE_RSR_CRC       0x02
#define NE_RSR_FAE       0x04
#define NE_RSR_FO        0x08
#define NE_RSR_MPA       0x10
#define NE_RSR_PHY       0x20
#define NE_RSR_DIS       0x40
#define NE_RSR_DFR       0x80

/*
** Data Configuration Register (DCR) bits
*/
#define NE_DCR_WTS       0x01
#define NE_DCR_BOS       0x02
#define NE_DCR_LAS       0x04
#define NE_DCR_LS        0x08
#define NE_DCR_AR        0x10
#define NE_DCR_FT0       0x20
#define NE_DCR_FT1       0x40

/*
** Hydra board-specific constants
*/
#define NE8390_NIC_OFFSET     0xffe1
#define NE8390_ADDRPROM_OFFSET 0xffc0

#define NE8390_START_PG  0x00
#define NE8390_STOP_PG   0x40
#define NE8390_TX_PAGES  8
#define NE8390_RX_START_PG (NE8390_START_PG + NE8390_TX_PAGES)

#define NE8390_BOARD_ID 0x08490001	/* manufacturer 0x0849 (2121), product 1 */

#endif
