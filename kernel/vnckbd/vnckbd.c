/*
 *  Keyboard driver for Android VNC server
 *
 *  Copyright (c) 2010 Danke Xie
 *
 *  Based on vnckbd.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#define __UNUSED __attribute__((unused))

static unsigned int vnc_keycode[KEY_MAX]; /* allow all keycodes */

struct vnckbd {
	unsigned int keycode[ARRAY_SIZE(vnc_keycode)];
	struct input_dev *input;
	int suspended; /*. need? */
	spinlock_t lock;
};

#ifdef CONFIG_KEYBOARD_VNC_SELF
struct platform_device vnc_keyboard_device = {
	.name	= "vnc-keyboard",
	.id	= -1,
};
#endif /* CONFIG_KEYBOARD_VNC_SELF */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void __UNUSED vnckbd_scankeyboard(struct platform_device *dev)
{
	struct vnckbd *vnckbd = platform_get_drvdata(dev);
	unsigned long flags;
	int scancode = 0;
	int pressed = 0;

	spin_lock_irqsave(&vnckbd->lock, flags);

	if (vnckbd->suspended)
		goto out;

	/* scan keys */

	/* report pressed key */
	if (0) {
		input_report_key(vnckbd->input,
				vnckbd->keycode[scancode],
				pressed);
	}

	input_sync(vnckbd->input);

 out:
	spin_unlock_irqrestore(&vnckbd->lock, flags);
}

#ifdef CONFIG_PM
static int vnckbd_suspend(struct platform_device *dev, pm_message_t state)
{
	struct vnckbd *vnckbd = platform_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&vnckbd->lock, flags);
	vnckbd->suspended = 1;
	spin_unlock_irqrestore(&vnckbd->lock, flags);

	return 0;
}

static int vnckbd_resume(struct platform_device *dev)
{
	struct vnckbd *vnckbd = platform_get_drvdata(dev);

	vnckbd->suspended = 0;

	return 0;
}
#else
#define vnckbd_suspend		NULL
#define vnckbd_resume		NULL
#endif

static int __devinit vnckbd_probe(struct platform_device *pdev) {

	int i;
	struct vnckbd *vnckbd;
	struct input_dev *input_dev;
	int error;

	printk(KERN_INFO "input: VNC Keyboard probe\n");

	vnckbd = kzalloc(sizeof(struct vnckbd), GFP_KERNEL);
	if (!vnckbd)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(vnckbd);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, vnckbd);

	spin_lock_init(&vnckbd->lock);

	vnckbd->input = input_dev;

	input_set_drvdata(input_dev, vnckbd);
	input_dev->name = "VNC keyboard";
	input_dev->phys = "vnckbd/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT(EV_KEY);
	input_dev->keycode = vnckbd->keycode;
	input_dev->keycodesize = sizeof(unsigned int);
	input_dev->keycodemax = ARRAY_SIZE(vnc_keycode);

	/* one-to-one mapping from scancode to keycode */
	for (i = 0; i < ARRAY_SIZE(vnc_keycode); i++)
		vnc_keycode[i] = i;

	memcpy(vnckbd->keycode, vnc_keycode, sizeof(vnc_keycode));

	for (i = 0; i < ARRAY_SIZE(vnc_keycode); i++)
		__set_bit(vnckbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	error = input_register_device(input_dev);
	if (error) {
		printk(KERN_ERR "vnckbd: Unable to register input device, "
				"error: %d\n", error);
		goto fail;
	}

	printk(KERN_INFO "input: VNC Keyboard Registered\n");

	return 0;

fail:

	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
	kfree(vnckbd);

	return error;
}

static int __devexit vnckbd_remove(struct platform_device *dev)
{
	struct vnckbd *vnckbd = platform_get_drvdata(dev);

	input_unregister_device(vnckbd->input);

	kfree(vnckbd);

	return 0;
}

static struct platform_driver vnckbd_driver = {
	.probe		= vnckbd_probe,
	.remove		= __devexit_p(vnckbd_remove),
	.suspend	= vnckbd_suspend,
	.resume		= vnckbd_resume,
	.driver		= {
		.name	= "vnc-keyboard",
		.owner	= THIS_MODULE,
	},
};

static int __devinit vnckbd_init(void)
{
	int rc;

	rc = platform_driver_register(&vnckbd_driver);
	if (rc) return rc;

#ifdef CONFIG_KEYBOARD_VNC_SELF
	rc = platform_device_register(&vnc_keyboard_device);
#endif

	return rc;
}

static void __exit vnckbd_exit(void)
{
	platform_driver_unregister(&vnckbd_driver);
}

module_init(vnckbd_init);
module_exit(vnckbd_exit);

MODULE_AUTHOR("Danke Xie <danke.xie@gmail.com>");
MODULE_DESCRIPTION("VNC Keyboard Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:vnc-keyboard");
