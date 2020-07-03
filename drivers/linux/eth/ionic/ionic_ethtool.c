// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/netdevice.h>

/* Normally we would #include <linux/sfp.h> here, but some of the
 * older distros don't have that file, and some that do have an
 * older version that doesn't include these definitions.
 */
enum {
	SFF8024_ID_UNK			= 0x00,
	SFF8024_ID_SFF_8472		= 0x02,
	SFF8024_ID_SFP			= 0x03,
	SFF8024_ID_DWDM_SFP		= 0x0b,
	SFF8024_ID_QSFP_8438		= 0x0c,
	SFF8024_ID_QSFP_8436_8636	= 0x0d,
	SFF8024_ID_QSFP28_8636		= 0x11,
};

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_ethtool.h"
#include "ionic_stats.h"

static const char ionic_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define IONIC_PRIV_F_SW_DBG_STATS	BIT(0)
	"sw-dbg-stats",
#define IONIC_PRIV_F_RDMA_SNIFFER	BIT(1)
	"rdma-sniffer",
#define IONIC_PRIV_F_SPLIT_INTR		BIT(2)
	"split-q-intr",
};

#define IONIC_PRIV_FLAGS_COUNT ARRAY_SIZE(ionic_priv_flags_strings)

static void ionic_get_stats_strings(struct ionic_lif *lif, u8 *buf)
{
	u32 i;

	for (i = 0; i < ionic_num_stats_grps; i++)
		ionic_stats_groups[i].get_strings(lif, &buf);
}

static void ionic_get_stats(struct net_device *netdev,
			    struct ethtool_stats *stats, u64 *buf)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u32 i;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return;

	memset(buf, 0, stats->n_stats * sizeof(*buf));
	for (i = 0; i < ionic_num_stats_grps; i++)
		ionic_stats_groups[i].get_values(lif, &buf);
}

static int ionic_get_stats_count(struct ionic_lif *lif)
{
	int i, num_stats = 0;

	for (i = 0; i < ionic_num_stats_grps; i++)
		num_stats += ionic_stats_groups[i].get_count(lif);

	return num_stats;
}

static int ionic_get_sset_count(struct net_device *netdev, int sset)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int count = 0;

	switch (sset) {
	case ETH_SS_STATS:
		count = ionic_get_stats_count(lif);
		break;
	case ETH_SS_PRIV_FLAGS:
		count = IONIC_PRIV_FLAGS_COUNT;
		break;
	}
	return count;
}

static void ionic_get_strings(struct net_device *netdev,
			      u32 sset, u8 *buf)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		ionic_get_stats_strings(lif, buf);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, ionic_priv_flags_strings,
		       IONIC_PRIV_FLAGS_COUNT * ETH_GSTRING_LEN);
		break;
	}
}

static void ionic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;

	strlcpy(drvinfo->driver, IONIC_DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, IONIC_DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, ionic->idev.dev_info.fw_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, ionic_bus_info(ionic),
		sizeof(drvinfo->bus_info));
}

static int ionic_get_regs_len(struct net_device *netdev)
{
	return (IONIC_DEV_INFO_REG_COUNT + IONIC_DEV_CMD_REG_COUNT) * sizeof(u32);
}

static void ionic_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			   void *p)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	unsigned int size;

	regs->version = IONIC_DEV_CMD_REG_VERSION;

	size = IONIC_DEV_INFO_REG_COUNT * sizeof(u32);
	memcpy_fromio(p, lif->ionic->idev.dev_info_regs->words, size);

	size = IONIC_DEV_CMD_REG_COUNT * sizeof(u32);
	memcpy_fromio(p, lif->ionic->idev.dev_cmd_regs->words, size);
}

static int ionic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *ks)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic *ionic = lif->ionic;
	int copper_seen = 0;

	ethtool_link_ksettings_zero_link_mode(ks, supported);

	/* The port_info data is found in a DMA space that the NIC keeps
	 * up-to-date, so there's no need to request the data from the
	 * NIC, we already have it in our memory space.
	 */

	switch (le16_to_cpu(idev->port_info->status.xcvr.pid)) {
		/* Copper */
	case IONIC_XCVR_PID_QSFP_100G_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseCR4_Full);
		copper_seen++;
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_CR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		copper_seen++;
		break;
#ifdef HAVE_ETHTOOL_25G_BITS
	case IONIC_XCVR_PID_SFP_25GBASE_CR_S:
	case IONIC_XCVR_PID_SFP_25GBASE_CR_L:
	case IONIC_XCVR_PID_SFP_25GBASE_CR_N:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		copper_seen++;
		break;
#endif
	case IONIC_XCVR_PID_SFP_10GBASE_AOC:
	case IONIC_XCVR_PID_SFP_10GBASE_CU:
#ifdef HAVE_ETHTOOL_NEW_10G_BITS
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseCR_Full);
#else
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
#endif
		copper_seen++;
		break;

		/* Fibre */
	case IONIC_XCVR_PID_QSFP_100G_SR4:
	case IONIC_XCVR_PID_QSFP_100G_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseSR4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_100G_CWDM4:
	case IONIC_XCVR_PID_QSFP_100G_PSM4:
	case IONIC_XCVR_PID_QSFP_100G_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_100G_ER4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100000baseLR4_ER4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_SR4:
	case IONIC_XCVR_PID_QSFP_40GBASE_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		break;
	case IONIC_XCVR_PID_QSFP_40GBASE_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		break;
#ifdef HAVE_ETHTOOL_25G_BITS
	case IONIC_XCVR_PID_SFP_25GBASE_SR:
	case IONIC_XCVR_PID_SFP_25GBASE_AOC:
	case IONIC_XCVR_PID_SFP_25GBASE_ACC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		break;
#endif
#ifdef HAVE_ETHTOOL_NEW_10G_BITS
	case IONIC_XCVR_PID_SFP_10GBASE_SR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_LR:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_LRM:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLRM_Full);
		break;
	case IONIC_XCVR_PID_SFP_10GBASE_ER:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseER_Full);
		break;
#else
	case IONIC_XCVR_PID_SFP_10GBASE_SR:
	case IONIC_XCVR_PID_SFP_10GBASE_LR:
	case IONIC_XCVR_PID_SFP_10GBASE_LRM:
	case IONIC_XCVR_PID_SFP_10GBASE_ER:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		break;
#endif
	case IONIC_XCVR_PID_QSFP_100G_ACC:
	case IONIC_XCVR_PID_QSFP_40GBASE_ER4:
	case IONIC_XCVR_PID_SFP_25GBASE_LR:
	case IONIC_XCVR_PID_SFP_25GBASE_ER:
		dev_info(lif->ionic->dev, "no decode bits for xcvr type pid=%d / 0x%x\n",
			 idev->port_info->status.xcvr.pid,
			 idev->port_info->status.xcvr.pid);
		break;
	case IONIC_XCVR_PID_UNKNOWN:
		/* This means there's no module plugged in */
		if (ionic->is_mgmt_nic)
			ethtool_link_ksettings_add_link_mode(ks, supported,
							     1000baseT_Full);
		break;
	default:
		dev_dbg(lif->ionic->dev, "unknown xcvr type pid=%d / 0x%x\n",
			idev->port_info->status.xcvr.pid,
			idev->port_info->status.xcvr.pid);
		break;
	}

	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

#ifdef ETHTOOL_FEC_NONE
	if (idev->port_info->status.fec_type == IONIC_PORT_FEC_TYPE_FC)
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_BASER);
	else if (idev->port_info->status.fec_type == IONIC_PORT_FEC_TYPE_RS)
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_RS);
#endif

	if (ionic->is_mgmt_nic)
		ethtool_link_ksettings_add_link_mode(ks, supported, Backplane);
	else
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);

	ethtool_link_ksettings_add_link_mode(ks, supported, Pause);

	if (idev->port_info->status.xcvr.phy == IONIC_PHY_TYPE_COPPER ||
	    copper_seen)
		ks->base.port = PORT_DA;
	else if (idev->port_info->status.xcvr.phy == IONIC_PHY_TYPE_FIBER)
		ks->base.port = PORT_FIBRE;
	else if (ionic->is_mgmt_nic)
		ks->base.port = PORT_OTHER;
	else
		ks->base.port = PORT_NONE;

	if (ks->base.port != PORT_NONE) {
		ks->base.speed = le32_to_cpu(lif->info->status.link_speed);

		if (le16_to_cpu(lif->info->status.link_status))
			ks->base.duplex = DUPLEX_FULL;
		else
			ks->base.duplex = DUPLEX_UNKNOWN;

		if (ionic_is_pf(lif->ionic) && !ionic->is_mgmt_nic) {
			ethtool_link_ksettings_add_link_mode(ks, supported,
							     Autoneg);

			if (idev->port_info->config.an_enable) {
				ethtool_link_ksettings_add_link_mode(ks,
								     advertising,
								     Autoneg);
				ks->base.autoneg = AUTONEG_ENABLE;
			}
		}
	}

	return 0;
}

static int ionic_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *ks)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic *ionic = lif->ionic;
	int err = 0;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	/* set autoneg */
	if (ks->base.autoneg != idev->port_info->config.an_enable) {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_port_autoneg(idev, ks->base.autoneg);
		err = ionic_dev_cmd_wait(ionic, devcmd_timeout);
		mutex_unlock(&ionic->dev_cmd_lock);
		if (err)
			return err;
	}

	/* set speed */
	if (ks->base.speed != le32_to_cpu(idev->port_info->config.speed)) {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_port_speed(idev, ks->base.speed);
		err = ionic_dev_cmd_wait(ionic, devcmd_timeout);
		mutex_unlock(&ionic->dev_cmd_lock);
		if (err)
			return err;
	}

	return 0;
}

static void ionic_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u8 pause_type;

	pause->autoneg = 0;

	pause_type = lif->ionic->idev.port_info->config.pause_type;
	if (pause_type) {
		pause->rx_pause = pause_type & IONIC_PAUSE_F_RX ? 1 : 0;
		pause->tx_pause = pause_type & IONIC_PAUSE_F_TX ? 1 : 0;
	}
}

static int ionic_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	u32 requested_pause;
	int err;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	if (pause->autoneg)
		return -EOPNOTSUPP;

	/* change both at the same time */
	requested_pause = IONIC_PORT_PAUSE_TYPE_LINK;
	if (pause->rx_pause)
		requested_pause |= IONIC_PAUSE_F_RX;
	if (pause->tx_pause)
		requested_pause |= IONIC_PAUSE_F_TX;

	if (requested_pause == lif->ionic->idev.port_info->config.pause_type)
		return 0;

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_port_pause(&lif->ionic->idev, requested_pause);
	err = ionic_dev_cmd_wait(ionic, devcmd_timeout);
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err)
		return err;

	return 0;
}

#ifdef ETHTOOL_FEC_NONE
static int ionic_get_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fec)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (lif->ionic->idev.port_info->status.fec_type) {
	case IONIC_PORT_FEC_TYPE_NONE:
		fec->active_fec = ETHTOOL_FEC_OFF;
		break;
	case IONIC_PORT_FEC_TYPE_RS:
		fec->active_fec = ETHTOOL_FEC_RS;
		break;
	case IONIC_PORT_FEC_TYPE_FC:
		fec->active_fec = ETHTOOL_FEC_BASER;
		break;
	default:
		fec->active_fec = ETHTOOL_FEC_NONE;
		break;
	}

	switch (lif->ionic->idev.port_info->config.fec_type) {
	case IONIC_PORT_FEC_TYPE_NONE:
		fec->fec = ETHTOOL_FEC_OFF;
		break;
	case IONIC_PORT_FEC_TYPE_RS:
		fec->fec = ETHTOOL_FEC_RS;
		break;
	case IONIC_PORT_FEC_TYPE_FC:
		fec->fec = ETHTOOL_FEC_BASER;
		break;
	default:
		fec->fec = ETHTOOL_FEC_NONE;
		break;
	}

	return 0;
}

static int ionic_set_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fec)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u8 fec_type;
	int ret = 0;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	if (lif->ionic->idev.port_info->config.an_enable) {
		netdev_err(netdev, "FEC request not allowed while autoneg is enabled\n");
		return -EINVAL;
	}

	switch (fec->fec) {
	case ETHTOOL_FEC_NONE:
		fec_type = IONIC_PORT_FEC_TYPE_NONE;
		break;
	case ETHTOOL_FEC_OFF:
		fec_type = IONIC_PORT_FEC_TYPE_NONE;
		break;
	case ETHTOOL_FEC_RS:
		fec_type = IONIC_PORT_FEC_TYPE_RS;
		break;
	case ETHTOOL_FEC_BASER:
		fec_type = IONIC_PORT_FEC_TYPE_FC;
		break;
	case ETHTOOL_FEC_AUTO:
	default:
		netdev_err(netdev, "FEC request 0x%04x not supported\n",
			   fec->fec);
		return -EINVAL;
	}

	if (fec_type != lif->ionic->idev.port_info->config.fec_type) {
		mutex_lock(&lif->ionic->dev_cmd_lock);
		ionic_dev_cmd_port_fec(&lif->ionic->idev, fec_type);
		ret = ionic_dev_cmd_wait(lif->ionic, devcmd_timeout);
		mutex_unlock(&lif->ionic->dev_cmd_lock);
	}

	return ret;
}

#endif /* ETHTOOL_FEC_NONE */
static int ionic_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coalesce)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	coalesce->tx_coalesce_usecs = lif->tx_coalesce_usecs;
	coalesce->rx_coalesce_usecs = lif->rx_coalesce_usecs;

	return 0;
}

static int ionic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coalesce)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_identity *ident;
	struct ionic_qcq *qcq;
	unsigned int i;
	u32 rx_coal;
	u32 tx_coal;

	if (coalesce->rx_max_coalesced_frames ||
	    coalesce->rx_coalesce_usecs_irq ||
	    coalesce->rx_max_coalesced_frames_irq ||
	    coalesce->tx_max_coalesced_frames ||
	    coalesce->tx_coalesce_usecs_irq ||
	    coalesce->tx_max_coalesced_frames_irq ||
	    coalesce->stats_block_coalesce_usecs ||
	    coalesce->use_adaptive_rx_coalesce ||
	    coalesce->use_adaptive_tx_coalesce ||
	    coalesce->pkt_rate_low ||
	    coalesce->rx_coalesce_usecs_low ||
	    coalesce->rx_max_coalesced_frames_low ||
	    coalesce->tx_coalesce_usecs_low ||
	    coalesce->tx_max_coalesced_frames_low ||
	    coalesce->pkt_rate_high ||
	    coalesce->rx_coalesce_usecs_high ||
	    coalesce->rx_max_coalesced_frames_high ||
	    coalesce->tx_coalesce_usecs_high ||
	    coalesce->tx_max_coalesced_frames_high ||
	    coalesce->rate_sample_interval)
		return -EINVAL;

	ident = &lif->ionic->ident;
	if (ident->dev.intr_coal_div == 0) {
		netdev_warn(netdev, "bad HW value in dev.intr_coal_div = %d\n",
			    ident->dev.intr_coal_div);
		return -EIO;
	}

	/* Tx normally shares Rx interrupt, so only change Rx */
	if (!test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state) &&
	    coalesce->tx_coalesce_usecs != lif->tx_coalesce_usecs) {
		netdev_warn(netdev, "only the rx-usecs can be changed\n");
		return -EINVAL;
	}

	/* Convert the usec request to a HW useable value.  If they asked
	 * for non-zero and it resolved to zero, bump it up
	 */
	rx_coal = ionic_coal_usec_to_hw(lif->ionic, coalesce->rx_coalesce_usecs);
	if (!rx_coal && coalesce->rx_coalesce_usecs)
		rx_coal = 1;
	tx_coal = ionic_coal_usec_to_hw(lif->ionic, coalesce->tx_coalesce_usecs);
	if (!tx_coal && coalesce->tx_coalesce_usecs)
		tx_coal = 1;

	if (rx_coal > IONIC_INTR_CTRL_COAL_MAX ||
	    tx_coal > IONIC_INTR_CTRL_COAL_MAX)
		return -ERANGE;

	/* Save the new values */
	lif->rx_coalesce_usecs = coalesce->rx_coalesce_usecs;
	if (rx_coal != lif->rx_coalesce_hw) {
		lif->rx_coalesce_hw = rx_coal;

		if (test_bit(IONIC_LIF_F_UP, lif->state)) {
			for (i = 0; i < lif->nxqs; i++) {
				qcq = lif->rxqcqs[i].qcq;
				ionic_intr_coal_init(lif->ionic->idev.intr_ctrl,
						     qcq->intr.index,
						     lif->rx_coalesce_hw);
			}
		}
	}

	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
		lif->tx_coalesce_usecs = coalesce->tx_coalesce_usecs;
	else
		lif->tx_coalesce_usecs = coalesce->rx_coalesce_usecs;
	if (tx_coal != lif->tx_coalesce_hw) {
		lif->tx_coalesce_hw = tx_coal;

		if (test_bit(IONIC_LIF_F_UP, lif->state)) {
			for (i = 0; i < lif->nxqs; i++) {
				qcq = lif->txqcqs[i].qcq;
				ionic_intr_coal_init(lif->ionic->idev.intr_ctrl,
						     qcq->intr.index,
						     lif->tx_coalesce_hw);
			}
		}
	}

	return 0;
}

static void ionic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	ring->tx_max_pending = IONIC_MAX_TX_DESC;
	ring->tx_pending = lif->ntxq_descs;
	ring->rx_max_pending = IONIC_MAX_RX_DESC;
	ring->rx_pending = lif->nrxq_descs;
}

static void ionic_set_ringsize(struct ionic_lif *lif, void *arg)
{
	struct ethtool_ringparam *ring = arg;

	lif->ntxq_descs = ring->tx_pending;
	lif->nrxq_descs = ring->rx_pending;
}

static int ionic_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	if (ring->rx_mini_pending || ring->rx_jumbo_pending) {
		netdev_info(netdev, "Changing jumbo or mini descriptors not supported\n");
		return -EINVAL;
	}

	if (!is_power_of_2(ring->tx_pending) ||
	    !is_power_of_2(ring->rx_pending)) {
		netdev_info(netdev, "Descriptor count must be a power of 2\n");
		return -EINVAL;
	}

	if (ring->tx_pending > IONIC_MAX_TX_DESC ||
	    ring->tx_pending < IONIC_MIN_TXRX_DESC) {
		netdev_info(netdev, "Tx descriptor count must be in the range [%d-%d]\n",
			    IONIC_MIN_TXRX_DESC, IONIC_MAX_TX_DESC);
		return -EINVAL;
	}

	if (ring->rx_pending > IONIC_MAX_RX_DESC ||
	    ring->rx_pending < IONIC_MIN_TXRX_DESC) {
		netdev_info(netdev, "Rx descriptor count must be in the range [%d-%d]\n",
			    IONIC_MIN_TXRX_DESC, IONIC_MAX_RX_DESC);
		return -EINVAL;
	}

	/* if nothing to do return success */
	if (ring->tx_pending == lif->ntxq_descs &&
	    ring->rx_pending == lif->nrxq_descs)
		return 0;

	if (ring->tx_pending != lif->ntxq_descs)
		netdev_info(netdev, "Changing Tx ring size from %d to %d\n",
			    lif->ntxq_descs, ring->tx_pending);

	if (ring->rx_pending != lif->nrxq_descs)
		netdev_info(netdev, "Changing Rx ring size from %d to %d\n",
			    lif->nrxq_descs, ring->rx_pending);

	return ionic_reset_queues(lif, ionic_set_ringsize, ring);
}

static void ionic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	/* report maximum channels */
	ch->max_combined = lif->ionic->ntxqs_per_lif;

	/* report current channels */
	ch->combined_count = lif->nxqs;
}

static void ionic_set_queuecount(struct ionic_lif *lif, void *arg)
{
	struct ethtool_channels *ch = arg;

	lif->nxqs = ch->combined_count;
}

static int ionic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int max_cnt;

	if (!ch->combined_count || ch->other_count ||
	    ch->rx_count || ch->tx_count)
		return -EINVAL;

	max_cnt = lif->ionic->ntxqs_per_lif;
	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
		max_cnt /= 2;

	if (ch->combined_count > max_cnt)
		return -EINVAL;

	if (ch->combined_count == lif->nxqs)
		return 0;

	netdev_info(netdev, "Changing queue count from %d to %d\n",
		    lif->nxqs, ch->combined_count);

	return ionic_reset_queues(lif, ionic_set_queuecount, ch);
}

static u32 ionic_get_priv_flags(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	u32 priv_flags = 0;

	if (test_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state))
		priv_flags |= IONIC_PRIV_F_SW_DBG_STATS;

	if (test_bit(IONIC_LIF_F_RDMA_SNIFFER, lif->state))
		priv_flags |= IONIC_PRIV_F_RDMA_SNIFFER;

	if (test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state))
		priv_flags |= IONIC_PRIV_F_SPLIT_INTR;

	return priv_flags;
}

static void ionic_set_split_intr(struct ionic_lif *lif, void *arg)
{
	int old_nxqs = lif->nxqs;
	int new_sqintr = !!arg;

	if (new_sqintr) {
		/* split the queues across the interrupts */
		lif->nxqs /= 2;
		set_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
	} else {
		/* combine Tx and Rx queues on the interrupts */
		lif->nxqs *= 2;
		clear_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
		lif->tx_coalesce_usecs = lif->rx_coalesce_usecs;
		lif->tx_coalesce_hw = lif->rx_coalesce_hw;
	}

	netdev_info(lif->netdev, "%s queue interrupts and changing queue count from %d to %d\n",
		    new_sqintr ? "Splitting" : "Sharing", old_nxqs, lif->nxqs);
}

static int ionic_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int sqintr, new_sqintr;
	int rdma;

	clear_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state);
	if (priv_flags & IONIC_PRIV_F_SW_DBG_STATS)
		set_bit(IONIC_LIF_F_SW_DEBUG_STATS, lif->state);

	rdma = test_bit(IONIC_LIF_F_RDMA_SNIFFER, lif->state);
	clear_bit(IONIC_LIF_F_RDMA_SNIFFER, lif->state);
	if (priv_flags & IONIC_PRIV_F_RDMA_SNIFFER)
		set_bit(IONIC_LIF_F_RDMA_SNIFFER, lif->state);

	if (rdma != test_bit(IONIC_LIF_F_RDMA_SNIFFER, lif->state))
		ionic_set_rx_mode(netdev);

	sqintr = test_bit(IONIC_LIF_F_SPLIT_INTR, lif->state);
	new_sqintr = !!(priv_flags & IONIC_PRIV_F_SPLIT_INTR);
	if (sqintr != new_sqintr) {
		void *arg = 0;

		if (ionic_use_eqs(lif)) {
			netdev_err(netdev, "Split interrupts is not supported when using EventQueues\n");
			return -EINVAL;
		}

		if (new_sqintr)
			arg = (void *)1;
		ionic_reset_queues(lif, ionic_set_split_intr, arg);
	}

	return 0;
}

static int ionic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *info, u32 *rules)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int err = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = lif->nxqs;
		break;
	default:
		netdev_err(netdev, "Command parameter %d is not supported\n",
			   info->cmd);
		err = -EOPNOTSUPP;
	}

	return err;
}

static u32 ionic_get_rxfh_indir_size(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	return le16_to_cpu(lif->ionic->ident.lif.eth.rss_ind_tbl_sz);
}

static u32 ionic_get_rxfh_key_size(struct net_device *netdev)
{
	return IONIC_RSS_HASH_KEY_SIZE;
}

#ifdef HAVE_RXFH_HASHFUNC
static int ionic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
#else
static int ionic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
#endif
{
	struct ionic_lif *lif = netdev_priv(netdev);
	unsigned int i, tbl_sz;

	if (indir) {
		tbl_sz = le16_to_cpu(lif->ionic->ident.lif.eth.rss_ind_tbl_sz);
		for (i = 0; i < tbl_sz; i++)
			indir[i] = lif->rss_ind_tbl[i];
	}

	if (key)
		memcpy(key, lif->rss_hash_key, IONIC_RSS_HASH_KEY_SIZE);

#ifdef HAVE_RXFH_HASHFUNC
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
#endif

	return 0;
}

#ifdef HAVE_RXFH_HASHFUNC
static int ionic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
#else
static int ionic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key)
#endif
{
	struct ionic_lif *lif = netdev_priv(netdev);
	int err;

#ifdef HAVE_RXFH_HASHFUNC
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;
#endif

	err = ionic_lif_rss_config(lif, lif->rss_types, key, indir);
	if (err)
		return err;

	return 0;
}

static int ionic_set_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     const void *data)
{
	struct ionic_lif *lif = netdev_priv(dev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		lif->rx_copybreak = *(u32 *)data;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ionic_get_tunable(struct net_device *netdev,
			     const struct ethtool_tunable *tuna, void *data)
{
	struct ionic_lif *lif = netdev_priv(netdev);

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = lif->rx_copybreak;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ionic_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)

{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_xcvr_status *xcvr;

	xcvr = &idev->port_info->status.xcvr;

	/* report the module data type and length */
	switch (xcvr->sprom[0]) {
	case SFF8024_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		break;
	case SFF8024_ID_QSFP_8436_8636:
	case SFF8024_ID_QSFP28_8636:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case SFF8024_ID_UNK:
		if (lif->ionic->is_mgmt_nic)
			netdev_info(netdev, "no xcvr on mgmt nic\n");
		else
			netdev_info(netdev, "no xcvr connected? type 0x%02x\n",
				    xcvr->sprom[0]);
		return -EINVAL;
	default:
		netdev_info(netdev, "unknown xcvr type 0x%02x\n",
			    xcvr->sprom[0]);
		modinfo->type = 0;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		break;
	}

	return 0;
}

static int ionic_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_xcvr_status *xcvr;
	char tbuf[sizeof(xcvr->sprom)];
	int count = 10;
	u32 len;

	/* The NIC keeps the module prom up-to-date in the DMA space
	 * so we can simply copy the module bytes into the data buffer.
	 */
	xcvr = &idev->port_info->status.xcvr;
	len = min_t(u32, sizeof(xcvr->sprom), ee->len);

	do {
		memcpy(data, xcvr->sprom, len);
		memcpy(tbuf, xcvr->sprom, len);

		/* Let's make sure we got a consistent copy */
		if (!memcmp(data, tbuf, len))
			break;

	} while (--count);

	if (!count)
		return -ETIMEDOUT;

	return 0;
}

static int ionic_nway_reset(struct net_device *netdev)
{
	struct ionic_lif *lif = netdev_priv(netdev);
	struct ionic *ionic = lif->ionic;
	int err = 0;

	if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		return -EBUSY;

	/* flap the link to force auto-negotiation */

	mutex_lock(&ionic->dev_cmd_lock);

	ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_DOWN);
	err = ionic_dev_cmd_wait(ionic, devcmd_timeout);

	if (!err) {
		ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_UP);
		err = ionic_dev_cmd_wait(ionic, devcmd_timeout);
	}

	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

static const struct ethtool_ops ionic_ethtool_ops = {
#ifdef ETHTOOL_COALESCE_USECS
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS,
#endif
	.get_drvinfo		= ionic_get_drvinfo,
	.get_regs_len		= ionic_get_regs_len,
	.get_regs		= ionic_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= ionic_get_link_ksettings,
	.set_link_ksettings	= ionic_set_link_ksettings,
	.get_coalesce		= ionic_get_coalesce,
	.set_coalesce		= ionic_set_coalesce,
	.get_ringparam		= ionic_get_ringparam,
	.set_ringparam		= ionic_set_ringparam,
	.get_channels		= ionic_get_channels,
	.set_channels		= ionic_set_channels,
	.get_strings		= ionic_get_strings,
	.get_ethtool_stats	= ionic_get_stats,
	.get_sset_count		= ionic_get_sset_count,
	.get_priv_flags		= ionic_get_priv_flags,
	.set_priv_flags		= ionic_set_priv_flags,
	.get_rxnfc		= ionic_get_rxnfc,
	.get_rxfh_indir_size	= ionic_get_rxfh_indir_size,
	.get_rxfh_key_size	= ionic_get_rxfh_key_size,
	.get_rxfh		= ionic_get_rxfh,
	.set_rxfh		= ionic_set_rxfh,
	.get_tunable		= ionic_get_tunable,
	.set_tunable		= ionic_set_tunable,
	.get_module_info	= ionic_get_module_info,
	.get_module_eeprom	= ionic_get_module_eeprom,
	.get_pauseparam		= ionic_get_pauseparam,
	.set_pauseparam		= ionic_set_pauseparam,
#ifdef ETHTOOL_FEC_NONE
	.get_fecparam		= ionic_get_fecparam,
	.set_fecparam		= ionic_set_fecparam,
#endif
	.nway_reset		= ionic_nway_reset,
};

void ionic_ethtool_set_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ionic_ethtool_ops;
}
