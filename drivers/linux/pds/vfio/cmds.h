// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Pensando Systems, Inc */

#ifndef _CMDS_H_
#define _CMDS_H_

#include <linux/types.h>

#include "pds_lm.h"

struct pds_vfio_pci_device;

int
pds_vfio_register_client_cmd(struct pds_vfio_pci_device *pds_vfio);
void
pds_vfio_unregister_client_cmd(struct pds_vfio_pci_device *pds_vfio);

int
pds_vfio_dirty_status_cmd(struct pds_vfio_pci_device *pds_vfio,
			  u64 regions_dma, u8 *max_regions,
			  u8 *num_regions);
int
pds_vfio_dirty_enable_cmd(struct pds_vfio_pci_device *pds_vfio,
			  u64 regions_dma, u8 num_regions);
int
pds_vfio_dirty_disable_cmd(struct pds_vfio_pci_device *pds_vfio);
int
pds_vfio_dirty_seq_ack_cmd(struct pds_vfio_pci_device *pds_vfio,
			   u64 sgl_dma, u16 num_sge, u32 offset,
			   u32 total_len, bool read_seq);

int
pds_vfio_suspend_device_cmd(struct pds_vfio_pci_device *pds_vfio);
int
pds_vfio_resume_device_cmd(struct pds_vfio_pci_device *pds_vfio);
int
pds_vfio_get_lm_status_cmd(struct pds_vfio_pci_device *pds_vfio, u64 *size);
int
pds_vfio_get_lm_state_cmd(struct pds_vfio_pci_device *pds_vfio);
int
pds_vfio_set_lm_state_cmd(struct pds_vfio_pci_device *pds_vfio);

void
pds_vfio_send_host_vf_lm_status_cmd(struct pds_vfio_pci_device *pds_vfio,
				    enum pds_lm_host_vf_status vf_status);
#endif /* _CMDS_H_ */
