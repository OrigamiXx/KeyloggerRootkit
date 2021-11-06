#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x108896cd, "module_layout" },
	{ 0x9ed554b3, "unregister_keyboard_notifier" },
	{ 0x173b2744, "dev_queue_xmit" },
	{ 0x51a37efa, "skb_push" },
	{ 0x2333c23b, "skb_put" },
	{ 0x99ca481e, "__alloc_skb" },
	{ 0x87e62777, "dev_get_by_name" },
	{ 0x3290bcd9, "init_net" },
	{ 0x96554810, "register_keyboard_notifier" },
	{ 0xc5850110, "printk" },
	{ 0xc959d152, "__stack_chk_fail" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "F3D16762DA3D86D2006AAAB");
