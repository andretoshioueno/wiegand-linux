/* wiegand-gpio.c
 *
 * Wiegand driver using GPIO an interrupts.
 *
 */

/* Standard headers for LKMs */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/gpio/consumer.h>
#include <asm/irq.h>

#define DRIVER_NAME	"wiegand"

struct card_data {
	unsigned int bitmap;
	int current_bit;
};

struct wiegand {
	struct input_dev *input;
	struct gpio_descs *input_gpios;
	struct timer_list timer;
	unsigned int irqs[2];
	struct card_data card_data;
};

void wiegand_clear(struct card_data *card_data)
{
	card_data->current_bit = 0;
	card_data->bitmap = 0;
}

void wiegand_init(struct card_data *card_data)
{
	wiegand_clear(card_data);
}

static bool check_parity(unsigned int bitmap,
			 int num_bits,
			 int end_parity
			)
{
	unsigned int mask;
	int i;
	int parity = 0;

	if (end_parity)
		mask = 0x01 << (num_bits/2 - 1);
	else
		mask = 0x01 << (num_bits-1);

	for (i = 0; i < num_bits/2; i++) {
		if (bitmap & mask) {
			parity++;
		}
		mask >>= 1;
	}
	return (parity % 2) == 1;
}

void wiegand_timer(unsigned long card_data_struct)
{
	struct card_data *card_data = (void *)(card_data_struct);
	struct wiegand *data = container_of(card_data, struct wiegand, card_data);

	/* TODO: check parity */
	/*
	//check the start parity
	if (check_parity(card_data->bitmap,
			 card_data->current_bit,
			 0)) {
		printk("start parity check failed\n");
		//return;
	}

	//check the end parity
	if (!check_parity(card_data->bitmap,
			 card_data->current_bit,
			 1)) {
		printk("end parity check failed\n");
		//return;
	}
	*/

	input_event(data->input, EV_MSC, MSC_RAW, card_data->bitmap);
	input_sync(data->input);

	//reset for next reading
	wiegand_clear(card_data);
}

irqreturn_t wiegand_isr0(int irq, void *dev_id)
{
	struct wiegand *data = (struct wiegand *)dev_id;
	struct card_data *card_data = &data->card_data;
	int value;

	del_timer(&data->timer);

	value = 0;
	card_data->bitmap <<= 1;
	card_data->bitmap |= value;

	card_data->current_bit++;

	//if we don't get another interrupt for 50ms we
	//assume the data is complete.
        mod_timer(&data->timer, jiffies + msecs_to_jiffies(50));
	return IRQ_HANDLED;
}

irqreturn_t wiegand_isr1(int irq, void *dev_id)
{
	struct wiegand *data = (struct wiegand *)dev_id;
	struct card_data *card_data = &data->card_data;
	int value;

	del_timer(&data->timer);

	value = 1;
	card_data->bitmap <<= 1;
	card_data->bitmap |= value;

	card_data->current_bit++;

	//if we don't get another interrupt for 50ms we
	//assume the data is complete.
        mod_timer(&data->timer, jiffies + msecs_to_jiffies(50));
	return IRQ_HANDLED;
}

int wiegand_probe(struct platform_device *pdev)
{
	//struct device_node *np = pdev->dev.of_node, *child;
	struct wiegand *data;
	int i;
	int ret = -1;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		pr_err("%s: Error allocating memory for driver data!\n",
			DRIVER_NAME);
		ret = -ENOMEM;
		goto ret_err_kzalloc;
	}

	data->input_gpios = devm_gpiod_get_array(&pdev->dev, NULL, GPIOD_IN);

	if (IS_ERR(data->input_gpios)) {
		dev_err(&pdev->dev, "unable to acquire input gpios\n");
		return PTR_ERR(data->input_gpios);
	}

	if (data->input_gpios->ndescs != 2) {
		dev_err(&pdev->dev, "Should have two gpios exactly\n");
		return -EINVAL;
	}

	for (i=0; i<data->input_gpios->ndescs; i++) {
		data->irqs[i] = gpiod_to_irq(data->input_gpios->desc[i]);
		if (data->irqs[i] < 0) {
			pr_err("%s: Unable to get irq number!\n", DRIVER_NAME);
			ret = data->irqs[i];
			goto ret_err_gpio_to_irq;
		}
	}

	ret = request_irq(data->irqs[0], wiegand_isr0,
		  IRQF_TRIGGER_FALLING,	DRIVER_NAME, data);

	if (ret < 0) {
		pr_err("%s: Error requesting irq0!\n", DRIVER_NAME);
		goto ret_err_request_irq;
	}
	ret = request_irq(data->irqs[1], wiegand_isr1,
		  IRQF_TRIGGER_FALLING,	DRIVER_NAME, data);

	if (ret < 0) {
		pr_err("%s: Error requesting irq1!\n", DRIVER_NAME);
		goto ret_err_request_irq;
	}

	if ((data->input = devm_input_allocate_device(&pdev->dev)) == NULL) {
		pr_err("%s: Error allocating input device!\n", DRIVER_NAME);
		ret = -ENOMEM;
		goto ret_err_input_allocate_device;
	}

	data->input->name = "Wiegand gpio Driver";
        data->input->dev.parent = &pdev->dev;
        data->input->id.bustype = BUS_HOST;
	input_set_capability(data->input, EV_MSC, MSC_RAW);

	if ((ret = input_register_device(data->input))) {
		pr_err("%s: Error registering input device!\n", DRIVER_NAME);
		goto err_input_register_device;
	}

	platform_set_drvdata(pdev, data);

	wiegand_init(&data->card_data);

	//setup the timer
	setup_timer(&data->timer, wiegand_timer,
		    (unsigned long)&data->card_data);

	pr_info("wiegand ready\n");

err_input_register_device:
ret_err_input_allocate_device:
ret_err_request_irq:
ret_err_gpio_to_irq:
ret_err_kzalloc:
	return ret;
}

int wiegand_remove(struct platform_device *pdev)
{
	struct wiegand *data = platform_get_drvdata(pdev);
	int i;

	input_unregister_device(data->input);

	for (i=0; i<data->input_gpios->ndescs; i++) {
		free_irq(data->irqs[i], data);
	}

	del_timer(&data->timer);
	input_free_device(data->input);

	return 0;
}


#ifdef CONFIG_OF
static const struct of_device_id wiegand_dt_match[] = {
        { .compatible = "verveworks, wiegand" },
        {},
};
MODULE_DEVICE_TABLE(of, wiegand_dt_match);
#endif

static struct platform_driver wiegand_driver = {
	.probe          = wiegand_probe,
	.remove         = wiegand_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(wiegand_dt_match),
	},
};

module_platform_driver(wiegand_driver);

MODULE_DESCRIPTION("Wiegand GPIO driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("VerveWorks Pty. Ltd.");
