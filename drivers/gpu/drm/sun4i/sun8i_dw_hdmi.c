/*
 * Copyright (c) 2017, Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/bridge/dw_hdmi.h>

#include "sun4i_crtc.h"
#include "sun4i_tcon.h"

#define SUN8I_HDMI_PHY_REG_POL		0x0000

#define SUN8I_HDMI_PHY_REG_READ_EN	0x0010
#define SUN8I_HDMI_PHY_REG_READ_EN_MAGIC	0x54524545

#define SUN8I_HDMI_PHY_REG_UNSCRAMBLE	0x0014
#define SUN8I_HDMI_PHY_REG_UNSCRAMBLE_MAGIC	0x42494E47

#define SUN8I_HDMI_PHY_REG_CTRL		0x0020
#define SUN8I_HDMI_PHY_REG_UNK1		0x0024
#define SUN8I_HDMI_PHY_REG_UNK2		0x0028
#define SUN8I_HDMI_PHY_REG_PLL		0x002c
#define SUN8I_HDMI_PHY_REG_CLK		0x0030
#define SUN8I_HDMI_PHY_REG_UNK3		0x0034

#define SUN8I_HDMI_PHY_REG_STATUS	0x0038
#define SUN8I_HDMI_PHY_REG_STATUS_READY		BIT(7)
#define SUN8I_HDMI_PHY_REG_STATUS_HPD		BIT(19)

#define to_sun8i_dw_hdmi(x)	container_of(x, struct sun8i_dw_hdmi, x)
#define set_bits(p, v)		writel(readl(p) | (v), p)

struct sun8i_dw_hdmi {
	struct clk *clk_ddc;
	struct clk *clk_hdmi;
	struct device *dev;
	struct drm_encoder encoder;
	void __iomem *phy_base;
	struct dw_hdmi_plat_data plat_data;
	struct reset_control *rst_ddc;
	struct reset_control *rst_hdmi;
};

static u32 sun8i_dw_hdmi_get_divider(int clk_khz)
{
	/*
	 * Due to missing documentaion of HDMI PHY, we know correct
	 * settings only for following four PHY dividers. Select one
	 * based on clock speed.
	 */
	if (clk_khz <= 27000)
		return 11;
	else if (clk_khz <= 74250)
		return 4;
	else if (clk_khz <= 148500)
		return 2;
	else
		return 1;
}

static void sun8i_dw_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;

	DRM_DEBUG_DRIVER("Disabling HDMI Output\n");

	sun4i_tcon_channel_disable(tcon, 1);
}

static void sun8i_dw_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;

	DRM_DEBUG_DRIVER("Enabling HDMI Output\n");

	sun4i_tcon_channel_enable(tcon, 1);
}

static void sun8i_dw_hdmi_encoder_mode_set(struct drm_encoder *encoder,
					   struct drm_display_mode *mode,
					   struct drm_display_mode *adj_mode)
{
	struct sun8i_dw_hdmi *hdmi = to_sun8i_dw_hdmi(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);
	struct sun4i_tcon *tcon = crtc->tcon;
	u32 div;

	sun4i_tcon1_mode_set(tcon, mode);

	div = sun8i_dw_hdmi_get_divider(mode->crtc_clock);
	clk_set_rate(hdmi->clk_hdmi, mode->crtc_clock * 1000 * div);
	clk_set_rate(tcon->sclk1, mode->crtc_clock * 1000);
}

static const struct drm_encoder_helper_funcs sun8i_dw_hdmi_encoder_helper_funcs = {
	.mode_set = sun8i_dw_hdmi_encoder_mode_set,
	.enable   = sun8i_dw_hdmi_encoder_enable,
	.disable  = sun8i_dw_hdmi_encoder_disable,
};

static int sun8i_dw_hdmi_phy_init(struct dw_hdmi *hdmi_data, void *data,
				  struct drm_display_mode *mode)
{
	struct sun8i_dw_hdmi *hdmi = (struct sun8i_dw_hdmi *)data;
	u32 div = sun8i_dw_hdmi_get_divider(mode->crtc_clock);
	u32 val;

	/*
	 * Unfortunately, we don't know much about those magic
	 * numbers. They are taken from Allwinner BSP driver.
	 */

	val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
	writel(val & ~0xf000, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);

	switch (div) {
	case 1:
		writel(0x30dc5fc0, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
		writel(0x800863C0, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CLK);
		mdelay(10);
		writel(1, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK3);
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(25));
		mdelay(200);
		val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);
		val = (val & 0x1f800) >> 11;
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL,
			 BIT(31) | BIT(30));
		if (val < 0x3d)
			set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL,
				 val + 2);
		else
			set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, 
				 0x3f);
		mdelay(100);
		writel(0x01FFFF7F, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
		writel(0x8063b000, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK1);
		writel(0x0F8246B5, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK2);
		break;
	case 2:
		writel(0x39dc5040, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
		writel(0x80084381, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CLK);
		mdelay(10);
		writel(1, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK3);
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(25));
		mdelay(100);
		val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);
		val = (val & 0x1f800) >> 11;
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL,
			 BIT(31) | BIT(30));
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, val);
		writel(0x01FFFF7F, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
		writel(0x8063a800, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK1);
		writel(0x0F81C485, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK2);
		break;
	case 4:
		writel(0x39dc5040, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
		writel(0x80084343, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CLK);
		mdelay(10);
		writel(1, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK3);
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(25));
		mdelay(100);
		val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);
		val = (val & 0x1f800) >> 11;
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL,
			 BIT(31) | BIT(30));
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, val);
		writel(0x01FFFF7F, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
		writel(0x8063b000, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK1);
		writel(0x0F81C405, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK2);
		break;
	case 11:
		writel(0x39dc5040, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
		writel(0x8008430a, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CLK);
		mdelay(10);
		writel(1, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK3);
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(25));
		mdelay(100);
		val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);
		val = (val & 0x1f800) >> 11;
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL,
			 BIT(31) | BIT(30));
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, val);
		writel(0x01FFFF7F, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
		writel(0x8063b000, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK1);
		writel(0x0F81C405, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK2);
		break;
	}

	/*
	 * Condition in original code is a bit weird. This is attempt
	 * to make it more reasonable and it works. It could be that
	 * bits and conditions are related and should be separated.
	 */
	if (!((mode->flags & DRM_MODE_FLAG_PHSYNC) &&
	      (mode->flags & DRM_MODE_FLAG_PVSYNC))) {
		set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_POL, 0x300);
	}

	return 0;
}

static void sun8i_dw_hdmi_phy_disable(struct dw_hdmi *hdmi_data, void *data)
{
	struct sun8i_dw_hdmi *hdmi = (struct sun8i_dw_hdmi *)data;

	writel(7, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
	writel(0, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
}

static enum drm_connector_status sun8i_dw_hdmi_phy_read_hpd(struct dw_hdmi *hdmi_data,
							    void *data)
{
	struct sun8i_dw_hdmi *hdmi = (struct sun8i_dw_hdmi *)data;
	u32 reg_val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);

	return !!(reg_val & SUN8I_HDMI_PHY_REG_STATUS_HPD) ?
		connector_status_connected : connector_status_disconnected;
}

static const struct dw_hdmi_phy_ops sun8i_dw_hdmi_phy_ops = {
	.init = &sun8i_dw_hdmi_phy_init,
	.disable = &sun8i_dw_hdmi_phy_disable,
	.read_hpd = &sun8i_dw_hdmi_phy_read_hpd,
};

static void sun8i_dw_hdmi_pre_init(void *data)
{
	struct sun8i_dw_hdmi *hdmi = (struct sun8i_dw_hdmi *)data;
	u32 timeout = 20;
	u32 val;

	/*
	 * HDMI PHY settings are taken as-is from Allwinner BSP code.
	 * There is no documentation.
	 */
	writel(0, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(0));
	udelay(5);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(16));
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(1));
	udelay(10);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(2));
	udelay(5);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(3));
	udelay(40);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(19));
	udelay(100);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(18));
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, 7 << 4);

	/* Note that Allwinner code doesn't fail in case of timeout */
	while (!(readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS) &
		SUN8I_HDMI_PHY_REG_STATUS_READY)) {
		if (!timeout--) {
			dev_warn(hdmi->dev, "HDMI PHY init timeout!\n");
			break;
		}
		udelay(100);
	}

	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, 0xf << 8);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL, BIT(7));

	writel(0x39dc5040, hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL);
	writel(0x80084343, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CLK);
	mdelay(10);
	writel(1, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK3);
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(25));
	mdelay(100);
	val = readl(hdmi->phy_base + SUN8I_HDMI_PHY_REG_STATUS);
	val = (val & 0x1f800) >> 11;
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, BIT(31) | BIT(30));
	set_bits(hdmi->phy_base + SUN8I_HDMI_PHY_REG_PLL, val);
	writel(0x01FF0F7F, hdmi->phy_base + SUN8I_HDMI_PHY_REG_CTRL);
	writel(0x80639000, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK1);
	writel(0x0F81C405, hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNK2);

	/* enable read access to HDMI controller */
	writel(SUN8I_HDMI_PHY_REG_READ_EN_MAGIC,
	       hdmi->phy_base + SUN8I_HDMI_PHY_REG_READ_EN);

	/* descramble register offsets */
	writel(SUN8I_HDMI_PHY_REG_UNSCRAMBLE_MAGIC,
	       hdmi->phy_base + SUN8I_HDMI_PHY_REG_UNSCRAMBLE);
}

static const struct drm_encoder_funcs sun8i_dw_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int sun8i_dw_hdmi_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct sun8i_dw_hdmi *hdmi;
	struct resource *res;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = &hdmi->plat_data;
	hdmi->dev = &pdev->dev;
	encoder = &hdmi->encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm,
							     dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	/* resource 0 is the memory region for the core controller */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	hdmi->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdmi->phy_base))
		return PTR_ERR(hdmi->phy_base);

	hdmi->clk_hdmi = devm_clk_get(dev, "isfr");
	if (IS_ERR(hdmi->clk_hdmi)) {
		dev_err(dev, "Could not get hdmi clock\n");
		return PTR_ERR(hdmi->clk_hdmi);
	}

	hdmi->clk_ddc = devm_clk_get(dev, "iddc");
	if (IS_ERR(hdmi->clk_ddc)) {
		dev_err(dev, "Could not get ddc clock\n");
		return PTR_ERR(hdmi->clk_ddc);
	}

	hdmi->rst_hdmi = devm_reset_control_get(dev, "hdmi");
	if (IS_ERR(hdmi->rst_hdmi)) {
		dev_err(dev, "Could not get hdmi reset control\n");
		return PTR_ERR(hdmi->rst_hdmi);
	}

	hdmi->rst_ddc = devm_reset_control_get(dev, "ddc");
	if (IS_ERR(hdmi->rst_ddc)) {
		dev_err(dev, "Could not get dw-hdmi reset control\n");
		return PTR_ERR(hdmi->rst_ddc);
	}

	ret = clk_prepare_enable(hdmi->clk_ddc);
	if (ret) {
		dev_err(dev, "Cannot enable DDC clock: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(hdmi->rst_hdmi);
	if (ret) {
		dev_err(dev, "Could not deassert hdmi reset control\n");
		goto err_ddc_clk;
	}

	ret = reset_control_deassert(hdmi->rst_ddc);
	if (ret) {
		dev_err(dev, "Could not deassert ddc reset control\n");
		goto err_assert_hdmi_reset;
	}

	drm_encoder_helper_add(encoder, &sun8i_dw_hdmi_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &sun8i_dw_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	plat_data->pre_init = &sun8i_dw_hdmi_pre_init,
	plat_data->pre_init_data = hdmi;
	plat_data->phy_ops = &sun8i_dw_hdmi_phy_ops,
	plat_data->phy_name = "sun8i_dw_hdmi_phy",
	plat_data->phy_data = hdmi;

	ret = dw_hdmi_bind(pdev, encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (ret)
		goto cleanup_encoder;

	return 0;

cleanup_encoder:
	drm_encoder_cleanup(encoder);
	reset_control_assert(hdmi->rst_ddc);
err_assert_hdmi_reset:
	reset_control_assert(hdmi->rst_hdmi);
err_ddc_clk:
	clk_disable_unprepare(hdmi->clk_ddc);

	return ret;
}

static void sun8i_dw_hdmi_unbind(struct device *dev, struct device *master,
				 void *data)
{
	return dw_hdmi_unbind(dev);
}

static const struct component_ops sun8i_dw_hdmi_ops = {
	.bind	= sun8i_dw_hdmi_bind,
	.unbind	= sun8i_dw_hdmi_unbind,
};

static int sun8i_dw_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_dw_hdmi_ops);
}

static int sun8i_dw_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_dw_hdmi_ops);

	return 0;
}

static const struct of_device_id sun8i_dw_hdmi_dt_ids[] = {
	{ .compatible = "allwinner,h3-dw-hdmi" },
	{},
};
MODULE_DEVICE_TABLE(of, sun8i_dw_hdmi_dt_ids);

struct platform_driver sun8i_dw_hdmi_pltfm_driver = {
	.probe  = sun8i_dw_hdmi_probe,
	.remove = sun8i_dw_hdmi_remove,
	.driver = {
		.name = "sun8i-dw-hdmi",
		.of_match_table = sun8i_dw_hdmi_dt_ids,
	},
};
module_platform_driver(sun8i_dw_hdmi_pltfm_driver);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner H3 DW HDMI bridge");
MODULE_LICENSE("GPL");
