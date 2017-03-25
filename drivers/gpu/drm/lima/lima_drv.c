#include <linux/module.h>
#include <linux/of_platform.h>

static int lima_pdev_probe(struct platform_device *pdev)
{
	return 0;
}

static int lima_pdev_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "arm,mali400" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver lima_platform_driver = {
	.probe      = lima_pdev_probe,
	.remove     = lima_pdev_remove,
	.driver     = {
		.name   = "lima",
		.of_match_table = dt_match,
	},
};

static int __init lima_init(void)
{
	int ret;

	ret = platform_driver_register(&lima_platform_driver);

	return ret;
}
module_init(lima_init);

static void __exit lima_exit(void)
{
	platform_driver_unregister(&lima_platform_driver);
}
module_exit(lima_exit);

MODULE_AUTHOR("Qiang Yu <yuq825@gmail.com>");
MODULE_DESCRIPTION("Lima DRM Driver");
MODULE_LICENSE("GPL v2");
